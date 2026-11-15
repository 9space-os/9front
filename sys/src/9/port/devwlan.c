/*
 * nfront: #ω, the radio's side of etherwlan's interfaces (etherwlan.c):
 * wlanN/data, the frames, wlanN/ctl.  Only the host owner opens them.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/wifi.h"
#include "../port/wlan.h"

enum {
	Qdir	= 0,
	Qwlan,
	Qdata,
	Qctl,
};

#define QID(c, t)	((c)<<4 | (t))
#define QCTLR(q)	((int)((q).path>>4))
#define QTYPE(q)	((int)((q).path & 15))

static int
wlangen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	int t;

	t = QTYPE(c->qid);
	if(t == Qdir){
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, Qdir), 0, QTDIR);
			devdir(c, q, "#ω", 0, eve, 0555, dp);
			return 1;
		}
		if(s >= nwlan)
			return -1;
		snprint(up->genbuf, sizeof up->genbuf, "wlan%d", wlans[s].edev->ctlrno);
		mkqid(&q, QID(s, Qwlan), 0, QTDIR);
		devdir(c, q, up->genbuf, 0, eve, 0555, dp);
		return 1;
	}
	if(s == DEVDOTDOT){
		mkqid(&q, QID(0, Qdir), 0, QTDIR);
		devdir(c, q, "#ω", 0, eve, 0555, dp);
		return 1;
	}
	switch(s){
	case 0:
		mkqid(&q, QID(QCTLR(c->qid), Qdata), 0, 0);
		devdir(c, q, "data", 0, eve, 0660, dp);
		return 1;
	case 1:
		mkqid(&q, QID(QCTLR(c->qid), Qctl), 0, 0);
		devdir(c, q, "ctl", 0, eve, 0660, dp);
		return 1;
	}
	return -1;
}

static Chan*
radioattach(char *spec)
{
	return devattach(L'ω', spec);
}

static Walkqid*
radiowalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, wlangen);
}

static int
radiostat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, wlangen);
}

static Chan*
radioopen(Chan *c, int omode)
{
	Wlan *ctlr;

	if(!iseve())
		error(Eperm);
	c = devopen(c, omode, nil, 0, wlangen);
	if(QTYPE(c->qid) == Qdata){
		ctlr = &wlans[QCTLR(c->qid)];
		if(incref(&ctlr->radio) != 1){
			decref(&ctlr->radio);
			c->flag &= ~COPEN;
			error(Einuse);
		}
		qreopen(ctlr->txq);
	}
	return c;
}

static void
radioclose(Chan *c)
{
	Wlan *ctlr;

	if(QTYPE(c->qid) == Qdata && (c->flag & COPEN) != 0){
		ctlr = &wlans[QCTLR(c->qid)];
		qhangup(ctlr->txq, "radio gone");
		/*
		 * the radio's state (the association, the block ack
		 * agreements in its hardware) is gone with it: wifi.c's
		 * association too, the next radio's is made anew
		 */
		if(decref(&ctlr->radio) == 0 && ctlr->wifi != nil)
			wifiradiogone(ctlr->wifi);
	}
}

static long
radioread(Chan *c, void *a, long n, vlong off)
{
	char *buf, *s, *e;
	Wlan *ctlr;
	long r;
	int i;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, nil, 0, wlangen);
	ctlr = &wlans[QCTLR(c->qid)];
	switch(QTYPE(c->qid)){
	case Qdata:
		return qread(ctlr->txq, a, n);
	case Qctl:
		buf = smalloc(READSTR);
		if(waserror()){
			free(buf);
			nexterror();
		}
		s = buf;
		e = buf + READSTR;
		s = seprint(s, e, "ether %d attached %d radio %ld\n",
			ctlr->edev->ctlrno, ctlr->attached, ctlr->radio.ref);
		s = seprint(s, e, "ea %E\n", ctlr->edev->ea);
		if(ctlr->ncaps > 0){
			s = seprint(s, e, "caps ");
			for(i = 0; i < ctlr->ncaps; i++)
				s = seprint(s, e, "%.2ux", ctlr->caps[i]);
			s = seprint(s, e, "\n");
		}
		seprint(s, e, "tx %lud rx %lud dropped %lud\n", ctlr->ntx, ctlr->nrx, ctlr->ndrop);
		r = readstr(off, a, n, buf);
		poperror();
		free(buf);
		return r;
	}
	error(Egreg);
	return -1;
}

static int
hexval(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static long
radiowrite(Chan *c, void *a, long n, vlong)
{
	uchar *p, ea[Eaddrlen], caps[256];
	Cmdbuf *cb;
	Wlan *ctlr;
	Block *b;
	int i, h, l;

	ctlr = &wlans[QCTLR(c->qid)];
	switch(QTYPE(c->qid)){
	case Qdata:
		/* a frame received */
		if(n < Hdrrx + 10 || n > Hdrrx + Maxframe)
			error(Ebadarg);
		if(ctlr->wifi == nil)
			return n;	/* the interface is not up: nobody listens */
		p = a;
		b = allocb(n);
		b->rp += 9;	/* room for the signal and the timestamp before the frame */
		b->wp = b->rp;
		memmove(b->wp, p + Hdrrx, n - Hdrrx);
		b->wp += n - Hdrrx;
		if(p[3] & Ftsf){
			memmove(b->rp - 8, p + 4, 8);
			b->flag |= Btimestamp;
		}
		/* the signal in dBm (0: the radio has none) */
		if(p[2] != 0){
			b->rp[-9] = p[2];
			b->flag |= Bsignal;
			ctlr->wifi->sigs = 1;
		}
		ctlr->nrx++;
		wifiiq(ctlr->wifi, b);
		return n;
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		if(cb->nf == 2 && strcmp(cb->f[0], "ea") == 0 && strlen(cb->f[1]) == 2*Eaddrlen){
			for(i = 0; i < Eaddrlen; i++){
				h = hexval(cb->f[1][2*i]);
				l = hexval(cb->f[1][2*i+1]);
				if(h < 0 || l < 0)
					error(Ebadarg);
				ea[i] = h<<4 | l;
			}
			memmove(ctlr->edev->ea, ea, Eaddrlen);
			memmove(ctlr->edev->Netif.addr, ea, Eaddrlen);	/* the addr file's */
		} else if(cb->nf == 2 && strcmp(cb->f[0], "caps") == 0){
			/* the radio's capability elements, checked as elements */
			l = strlen(cb->f[1]);
			if(l % 2 != 0 || l/2 > sizeof ctlr->caps)
				error(Ebadarg);
			for(i = 0; i < l/2; i++){
				h = hexval(cb->f[1][2*i]);
				if(h < 0 || hexval(cb->f[1][2*i+1]) < 0)
					error(Ebadarg);
				caps[i] = h<<4 | hexval(cb->f[1][2*i+1]);
			}
			for(h = 0; h+2 <= l/2; h += 2+caps[h+1])
				;
			if(h != l/2)
				error("caps: not elements");
			qlock(ctlr);
			memmove(ctlr->caps, caps, l/2);
			ctlr->ncaps = l/2;
			if(ctlr->wifi != nil){
				memmove(ctlr->wifi->caps, caps, l/2);
				ctlr->wifi->ncaps = l/2;
			}
			qunlock(ctlr);
		} else
			error(Ebadctl);
		poperror();
		free(cb);
		return n;
	}
	error(Egreg);
	return -1;
}

Dev wlandevtab = {
	L'ω',
	"wlan",

	devreset,
	devinit,
	devshutdown,
	radioattach,
	radiowalk,
	radiostat,
	radioopen,
	devcreate,
	radioclose,
	radioread,
	devbread,
	radiowrite,
	devbwrite,
	devremove,
	devwstat,
};
