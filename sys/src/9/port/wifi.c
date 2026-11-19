#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../port/netif.h"
#include "../port/etherif.h"
#include "../port/wifi.h"

#include <libsec.h>

typedef struct SNAP SNAP;
struct SNAP
{
	uchar	dsap;
	uchar	ssap;
	uchar	control;
	uchar	orgcode[3];
	uchar	type[2];
};

enum {
	WIFIHDRSIZE = 2+2+3*6+2,
	SNAPHDRSIZE = 8,
};

static char Sconn[] = "connecting";
static char Sauth[] = "authenticated";
static char Sneedauth[] = "need authentication";
static char Sunauth[] = "unauthenticated";

static char Sassoc[] = "associated";
static char Sunassoc[] = "unassociated";
static char Sblocked[] = "blocked";	/* no keys negotiated. only pass EAPOL frames */

static uchar basicrates[] = {
	0x80 | 2,	/* 1.0	Mb/s */
	0x80 | 4,	/* 2.0	Mb/s */
	0x80 | 11,	/* 5.5	Mb/s */
	0x80 | 22,	/* 11.0	Mb/s */

	0
};

/*
 * 9space: the channels wifsproc scans (wifi->scan), in order.  The 2.4
 * GHz ones as before, 1, 6 and 11 first; the 5 GHz ones (20 MHz) between
 * them, so that the common channels come round about as often as they
 * did: 36-48 and 149-165 after 1, 6, 11 and 3, 8, 13, then the channels
 * with radar detection (DFS, 52-144), 1, 6 and 11 again in their middle.
 */
uchar wifiscan2g[] = {
	1, 6, 11, 3, 8, 13, 5, 10, 2, 7, 12, 4, 9,
	0
};

uchar wifiscan5g[] = {
	1, 6, 11,
	36, 40, 44, 48,
	3, 8, 13,
	149, 153, 157, 161, 165,
	5, 10, 2, 7, 12, 4, 9,
	52, 56, 60, 64,
	1, 6, 11,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	0
};

/*
 * 9space: channels with radar detection (DFS): a station may only listen
 * there until it has heard an access point, so the scan sends no probe
 * requests on them.  wifi.c has no regulatory domain otherwise (it
 * probes on 12 and 13 too).
 */
static int
passivechan(int channel)
{
	return channel >= 52 && channel <= 144;
}

/* 9space: the rates of wifi->rates valid on a channel: no 802.11b (DSSS) rates on 5 GHz */
static ulong
chanrates(Wifi *wifi, int channel)
{
	ulong valid;
	int i, r;

	valid = 0;
	if(wifi->rates == nil)
		return 0;
	for(i = 0; i < 32 && wifi->rates[i] != 0; i++){
		r = wifi->rates[i] & 0x7f;
		if(channel > 14 && (r == 2 || r == 4 || r == 11 || r == 22))
			continue;
		valid |= 1UL<<i;
	}
	return valid;
}

/* 9space: the driver's transmit, noting the channel it tunes to */
static void
radiotx(Wifi *wifi, Wnode *wn, Block *b)
{
	if(wn->channel != 0)
		wifi->channel = wn->channel;
	(*wifi->transmit)(wifi, wn, b);
}

static Block* wifidecrypt(Wifi *, Wnode *, Block *);
static void wifirecv(Wifi*, Block*);	/* 9space */
static void wifitx(Wifi*, Wnode*, Block*);
static void amsdu(Wifi*, Block*);
static Block* wifiencrypt(Wifi *, Wnode *, Block *);
static void freewifikeys(Wifi *, Wnode *);

static uchar*
srcaddr(Wifipkt *w)
{
	if((w->fc[1] & 0x02) == 0)
		return w->a2;
	if((w->fc[1] & 0x01) == 0)
		return w->a3;
	return w->a4;
}
static uchar*
dstaddr(Wifipkt *w)
{
	if((w->fc[1] & 0x01) != 0)
		return w->a3;
	return w->a1;
}

int
wifihdrlen(Wifipkt *w)
{
	int n;

	n = WIFIHDRSIZE;
	if((w->fc[0] & 0x0c) == 0x08)
		if((w->fc[0] & 0xf0) == 0x80){	/* QOS */
			n += 2;
			if(w->fc[1] & 0x80)
				n += 4;
		}
	if((w->fc[1] & 3) == 0x03)
		n += Eaddrlen;
	return n;
}

static uvlong
getts(uchar *d)
{
	return	(uvlong)d[0] |
		(uvlong)d[1]<<8 |
		(uvlong)d[2]<<16 |
		(uvlong)d[3]<<24 |
		(uvlong)d[4]<<32 |
		(uvlong)d[5]<<40 |
		(uvlong)d[6]<<48 |
		(uvlong)d[7]<<56;
}

/*
 * 9space: a frame for wifiproc, woken for it: it may wait with a
 * timeout (the block ack reorder's, batick), so not in qbread
 */
static void
iqpass(Wifi *wifi, Block *b)
{
	qpass(wifi->iq, b);
	wakeup(&wifi->iqr);
}

static void
wifirecv(Wifi *wifi, Block *b)
{
	SNAP s;
	Wifipkt h, *w;
	Etherpkt *e;
	int hdrlen;

	if(b->flag & Btimestamp)
		assert(b->rp - b->base >= 8);
	if(BLEN(b) < WIFIHDRSIZE)
		goto drop;
	w = (Wifipkt*)b->rp;
	hdrlen = wifihdrlen(w);
	if(BLEN(b) < hdrlen)
		goto drop;
	if(memcmp(srcaddr(w), wifi->ether->ea, Eaddrlen) == 0)
		goto drop;
	if(w->fc[1] & 0x40){
		/* encrypted */
		iqpass(wifi, b);
		return;
	}
	switch(w->fc[0] & 0x0c){
	case 0x00:	/* management */
		if((w->fc[1] & 3) != 0x00)	/* STA->STA */
			break;
		iqpass(wifi, b);
		return;
	case 0x04:	/* control */
		break;
	case 0x08:	/* data */
		b->flag &= ~Btimestamp;
		b->rp += hdrlen;
		switch(w->fc[0] & 0xf0){
		default:
			goto drop;
		case 0x80:	/* QOS */
		case 0x00:
			break;
		}
		/* 9space: an A-MSDU (QoS control bit 7): its subframes */
		if((w->fc[0] & 0x80) != 0 && (((uchar*)w)[hdrlen-2] & 0x80) != 0){
			amsdu(wifi, b);
			return;
		}
		if(BLEN(b) < SNAPHDRSIZE)
			break;
		memmove(&s, b->rp, SNAPHDRSIZE);
		if(s.dsap != 0xAA || s.ssap != 0xAA || s.control != 3)
			break;
		if(s.orgcode[0] != 0 || s.orgcode[1] != 0 || s.orgcode[2] != 0)
			break;
		b->rp += SNAPHDRSIZE-ETHERHDRSIZE;
		h = *w;
		e = (Etherpkt*)b->rp;
		memmove(e->d, dstaddr(&h), Eaddrlen);
		memmove(e->s, srcaddr(&h), Eaddrlen);
		memmove(e->type, s.type, 2);
		etheriq(wifi->ether, b);
		return;
	}
drop:
	freeb(b);
}

/*
 * 9space: an A-MSDU's subframes, each an ethernet frame (802.11-2020
 * 9.3.2.2.2: DA, SA, length, the MSDU with its LLC/SNAP, padded to 4)
 */
static void
amsdu(Wifi *wifi, Block *b)
{
	Etherpkt *e;
	Block *nb;
	SNAP s;
	int n;

	while(BLEN(b) >= 14){
		n = b->rp[12]<<8 | b->rp[13];
		if(n < SNAPHDRSIZE || 14 + n > BLEN(b))
			break;
		memmove(&s, b->rp+14, SNAPHDRSIZE);
		if(s.dsap == 0xAA && s.ssap == 0xAA && s.control == 3
		&& s.orgcode[0] == 0 && s.orgcode[1] == 0 && s.orgcode[2] == 0){
			/* 9space: iallocb: wifirecv runs in the driver's interrupt too (wifiiq) */
			if((nb = iallocb(ETHERHDRSIZE + n - SNAPHDRSIZE)) == nil)
				break;
			e = (Etherpkt*)nb->wp;
			memmove(e->d, b->rp, Eaddrlen);
			memmove(e->s, b->rp+6, Eaddrlen);
			memmove(e->type, s.type, 2);
			memmove(nb->wp + ETHERHDRSIZE, b->rp + 14 + SNAPHDRSIZE, n - SNAPHDRSIZE);
			nb->wp += ETHERHDRSIZE + n - SNAPHDRSIZE;
			etheriq(wifi->ether, nb);
		}
		n = 14 + n;
		if(n < BLEN(b))
			n = (n + 3) & ~3;
		if(n > BLEN(b))
			break;
		b->rp += n;
	}
	freeb(b);
}

/*
 * 9space: the receive side of A-MPDU, as mac80211's reorder buffer does
 * it (ieee80211_sta_manage_reorder_buf): QoS data of a TID with a block
 * ack agreement waits until the frames before it are there, or the wait
 * is over (Batimeout), so that wifi.c (and CCMP's replay check) sees
 * them in order of their sequence numbers.
 */
enum {
	Batimeout = 100,	/* ms */
};

static int
seqdiff(int a, int b)
{
	return (a - b) & 0xfff;
}

/* the frames from the head on, up to the first missing one, to *l */
static Block**
bainorder(Wba *ba, Block **l)
{
	Block *b;

	while((b = ba->buf[ba->head % Nba]) != nil){
		ba->buf[ba->head % Nba] = nil;
		ba->n--;
		*l = b;
		l = &b->next;
		ba->head = (ba->head + 1) & 0xfff;
	}
	ba->since = ba->n > 0 ? MACHP(0)->PMach.ticks : 0;
	return l;
}

/* the head n on, the frames before it to *l, held ones in order */
static Block**
baadvance(Wba *ba, int n, Block **l)
{
	Block *b;

	for(; n > 0; n--){
		if((b = ba->buf[ba->head % Nba]) != nil){
			ba->buf[ba->head % Nba] = nil;
			ba->n--;
			*l = b;
			l = &b->next;
		}
		ba->head = (ba->head + 1) & 0xfff;
	}
	return l;
}

static void	inorder(Wifi*, Block*);

/* the frames in order on (in wifiproc) */
static void
barelease(Wifi *wifi, Block *b)
{
	Block *next;

	for(; b != nil; b = next){
		next = b->next;
		b->next = nil;
		inorder(wifi, b);
	}
}

/*
 * an agreement over: the frames held, in order (release), or dropped
 * (the association is over: not in wifiproc, the keys gone)
 */
static void
baoff(Wifi *wifi, int tid, int release)
{
	Block *out, **l;
	Wba *ba;

	ba = &wifi->rxba[tid];
	out = nil;
	l = &out;
	lock(&wifi->balock);
	if(ba->on){
		l = baadvance(ba, Nba, l);
		ba->on = 0;
		ba->n = 0;
		ba->since = 0;
	}
	*l = nil;
	unlock(&wifi->balock);
	if(release)
		barelease(wifi, out);
	else
		freeblist(out);
}

/* held too long: the missing ones are given up */
static void
batick(Wifi *wifi)
{
	Block *out, **l;
	Wba *ba;
	int i;

	for(i = 0; i < Ntid; i++){
		ba = &wifi->rxba[i];
		out = nil;
		l = &out;
		lock(&wifi->balock);
		if(ba->on && ba->n > 0 && ba->since != 0
		&& TK2MS(MACHP(0)->PMach.ticks - ba->since) >= Batimeout){
			while(ba->buf[ba->head % Nba] == nil){
				ba->head = (ba->head + 1) & 0xfff;
				ba->expired++;
			}
			l = bainorder(ba, l);
		}
		*l = nil;
		unlock(&wifi->balock);
		barelease(wifi, out);
	}
}

/* some agreement holds frames: wifiproc waits for batick at most 20 ms */
static int
baheld(Wifi *wifi)
{
	int i;

	for(i = 0; i < Ntid; i++)
		if(wifi->rxba[i].on && wifi->rxba[i].n > 0)
			return 1;
	return 0;
}

/*
 * a frame from the radio: QoS data of the access point's under a block
 * ack agreement to its window, the rest on (wifirecv)
 */
static void
bareorder(Wifi *wifi, Block *b)
{
	Block *out, **l;
	Wifipkt *w;
	Wnode *wn;
	Wba *ba;
	int hdrlen, tid, seq, d;

	if(!wifi->ba || BLEN(b) < WIFIHDRSIZE)
		goto pass;
	w = (Wifipkt*)b->rp;
	/*
	 * a block ack request (control, subtype 8; 802.11-2020 9.3.1.7):
	 * the window of its TID moves to its starting sequence number,
	 * the frames before go on (as net80211's ieee80211_recv_bar)
	 */
	if(w->fc[0] == 0x84){
		if(BLEN(b) >= 20 && (wn = wifi->bss) != nil && memcmp(w->a2, wn->bssid, Eaddrlen) == 0){
			tid = b->rp[17]>>4 & 15;
			seq = (b->rp[18] | b->rp[19]<<8) >> 4;
			ba = &wifi->rxba[tid];
			out = nil;
			l = &out;
			lock(&wifi->balock);
			if(ba->on && (d = seqdiff(seq, ba->head)) < 2048){
				l = baadvance(ba, d, l);
				l = bainorder(ba, l);
			}
			*l = nil;
			unlock(&wifi->balock);
			barelease(wifi, out);
		}
		freeb(b);
		return;
	}
	if((w->fc[0] & 0x8c) != 0x88 || (w->fc[1] & 3) != 0x02 || (w->a1[0] & 1) != 0)
		goto pass;
	hdrlen = wifihdrlen(w);
	if(BLEN(b) < hdrlen || (wn = wifi->bss) == nil || memcmp(w->a2, wn->bssid, Eaddrlen) != 0)
		goto pass;
	tid = b->rp[hdrlen-2] & 15;
	ba = &wifi->rxba[tid];
	if(!ba->on)
		goto pass;
	seq = (w->seq[0] | w->seq[1]<<8) >> 4;
	out = nil;
	l = &out;
	lock(&wifi->balock);
	if(!ba->on){
		unlock(&wifi->balock);
		goto pass;
	}
	d = seqdiff(seq, ba->head);
	if(d >= 2048){
		/* before the window: had or given up */
		unlock(&wifi->balock);
		freeb(b);
		return;
	}
	if(d >= ba->win)
		l = baadvance(ba, d - ba->win + 1, l);
	if(ba->buf[seq % Nba] != nil){
		/* a retry of one held */
		*l = nil;
		unlock(&wifi->balock);
		barelease(wifi, out);
		freeb(b);
		return;
	}
	ba->buf[seq % Nba] = b;
	ba->n++;
	if(seq != ba->head)
		ba->held++;
	l = bainorder(ba, l);
	*l = nil;
	unlock(&wifi->balock);
	barelease(wifi, out);
	return;
pass:
	inorder(wifi, b);
}
/*
 * 9space: QoS data under a block ack agreement and block ack requests
 * from the access point: the reorder's (wifiproc)
 */
static int
baframe(Wifi *wifi, Block *b)
{
	Wifipkt *w;
	Wnode *wn;
	int hdrlen;

	if(!wifi->ba || BLEN(b) < WIFIHDRSIZE)
		return 0;
	w = (Wifipkt*)b->rp;
	if(w->fc[0] == 0x84)
		return BLEN(b) >= 20 && (wn = wifi->bss) != nil && memcmp(w->a2, wn->bssid, Eaddrlen) == 0;
	if((w->fc[0] & 0x8c) != 0x88 || (w->fc[1] & 3) != 0x02 || (w->a1[0] & 1) != 0)
		return 0;
	hdrlen = wifihdrlen(w);
	if(BLEN(b) < hdrlen || (wn = wifi->bss) == nil || memcmp(w->a2, wn->bssid, Eaddrlen) != 0)
		return 0;
	return wifi->rxba[b->rp[hdrlen-2] & 15].on;
}

/*
 * a frame from the radio, in the driver's interrupt: what the block
 * ack reorder holds goes to wifiproc, which reorders, decrypts and
 * passes on in one proc (the CCMP replay check sees them in order);
 * the rest as 9front's wifiiq (wifirecv)
 */
void
wifiiq(Wifi *wifi, Block *b)
{
	if(baframe(wifi, b)){
		iqpass(wifi, b);
		return;
	}
	wifirecv(wifi, b);
}


/* 9space: a block ack action frame (category 3) to the access point */
static void
sendba(Wifi *wifi, Wnode *wn, uchar *body, int n)
{
	Wifipkt *w;
	Block *b;

	b = allocb(WIFIHDRSIZE + n);
	w = (Wifipkt*)b->wp;
	w->fc[0] = 0xd0;	/* action */
	w->fc[1] = 0x00;
	memmove(w->a1, wn->bssid, Eaddrlen);
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, wn->bssid, Eaddrlen);
	b->wp += WIFIHDRSIZE;
	memmove(b->wp, body, n);
	b->wp += n;
	wifitx(wifi, wn, b);
}

/*
 * 9space: the block ack action frames (802.11-2020 9.6.4): an ADDBA
 * request is agreed to where the radio answers aggregates (wifi->ba;
 * buffer size at most Nba, no A-MSDU in them), declined (37) where not;
 * an ADDBA response to ours, a DELBA end one
 */
static void
recvba(Wifi *wifi, Wnode *wn, uchar *d, int len)
{
	uchar r[9];
	int param, tid, size, st;
	Wba *ba;

	if(len < 2 || d[0] != 3 || wn != wifi->bss)
		return;
	switch(d[1]){
	case 0:	/* ADDBA request */
		if(len < 9)
			return;
		param = d[3] | d[4]<<8;
		tid = param>>2 & 15;
		size = param>>6;
		if(size == 0 || size > Nba)
			size = Nba;
		st = wifi->ba && wn->qos && (param & 2) != 0 ? 0 : 37;
		/* 9space: the driver's radio answers with block acks (etheriwl) */
		if(st == 0 && wifi->baagree != nil && (*wifi->baagree)(wifi, tid, (d[7] | d[8]<<8) >> 4, size, 1) < 0)
			st = 37;
		if(st == 0){
			ba = &wifi->rxba[tid];
			baoff(wifi, tid, 1);
			lock(&wifi->balock);
			ba->head = (d[7] | d[8]<<8) >> 4;
			ba->win = size;
			ba->n = 0;
			ba->since = 0;
			ba->held = ba->expired = 0;
			ba->on = 1;
			unlock(&wifi->balock);
		}
		r[0] = 3;
		r[1] = 1;	/* ADDBA response */
		r[2] = d[2];	/* dialog token */
		r[3] = st;
		r[4] = st>>8;
		param = (param & 2) | tid<<2 | size<<6;	/* no A-MSDU */
		r[5] = param;
		r[6] = param>>8;
		r[7] = d[5];	/* timeout */
		r[8] = d[6];
		sendba(wifi, wn, r, 9);
		if(wifi->debug)
			print("#l%d: addba from %E: tid %d size %d: %s\n", wifi->ether->ctlrno,
				wn->bssid, tid, size, st == 0 ? "agreed" : "declined");
		break;
	case 1:	/* ADDBA response */
		if(len < 9 || d[2] != wn->batoken)
			return;
		param = d[5] | d[6]<<8;
		tid = param>>2 & 15;
		if((d[3] | d[4]<<8) == 0){
			/* 9space: the driver's radio aggregates from the TID's next sequence number */
			if(wifi->batxagree != nil){
				int ssn, n;

				ssn = wn->qseq[tid] & 0xfff;
				if((n = (*wifi->batxagree)(wifi, tid, ssn, 1)) < 0)
					break;
				if(n != ssn && (wn->qseq[tid] & 0xfff) == ssn)
					wn->qseq[tid] = n;	/* the radio skips one (its scheduler's bug) */
			}
			wn->txba |= 1<<tid;
		}
		break;
	case 2:	/* DELBA */
		if(len < 6)
			return;
		param = d[2] | d[3]<<8;
		tid = param>>12 & 15;
		if(param & (1<<11)){
			baoff(wifi, tid, 1);	/* the access point's, our receiving */
			if(wifi->baagree != nil)
				(*wifi->baagree)(wifi, tid, 0, 0, 0);
		} else {
			if((wn->txba & 1<<tid) && wifi->batxagree != nil)
				(*wifi->batxagree)(wifi, tid, 0, 0);
			wn->txba &= ~(1<<tid);
		}
		break;
	}
}

/*
 * 9space: our ADDBA request for TID 0 (best effort), once associated with
 * WMM and HT where the radio aggregates: buffer size 64, immediate block
 * ack, no A-MSDU, starting at the TID's next sequence number
 */
static void
sendaddba(Wifi *wifi, Wnode *wn)
{
	uchar r[9];
	int param;

	if(!wifi->ba || !wifi->batx || !wn->qos || !wn->ht)
		return;
	r[0] = 3;
	r[1] = 0;	/* ADDBA request */
	r[2] = ++wn->batoken;
	param = 2 | 0<<2 | Nba<<6;
	r[3] = param;
	r[4] = param>>8;
	r[5] = 0;	/* no timeout */
	r[6] = 0;
	r[7] = wn->qseq[0]<<4;
	r[8] = wn->qseq[0]>>4;
	sendba(wifi, wn, r, 9);
}

static void
wifitx(Wifi *wifi, Wnode *wn, Block *b)
{
	Wifipkt *w;
	uint seq;

	wn->lastsend = MACHP(0)->PMach.ticks;

	w = (Wifipkt*)b->rp;
	/* 9space: QoS data numbered per TID (802.11 10.3.2.14), the rest as before */
	if((w->fc[0] & 0x8c) == 0x88)
		seq = wn->qseq[((uchar*)w)[wifihdrlen(w)-2] & 15]++;
	else
		seq = incref(&wifi->txseq);
	seq <<= 4;

	w->dur[0] = 1;
	w->dur[1] = 0;
	w->seq[0] = seq;
	w->seq[1] = seq>>8;

	if((w->fc[0] & 0x0c) != 0x00){
		b = wifiencrypt(wifi, wn, b);
		if(b == nil)
			return;
	}

	if((wn->txcount++ & 255) == 255 && wn->actrate != nil && wn->actrate != wn->maxrate){
		uchar *a, *p;

		for(a = wn->maxrate, p = wifi->rates; *p; p++){
			if(*p < *a && *p > *wn->actrate && (wn->validrates & (1UL << p-wifi->rates)) != 0)
				a = p;
		}
		wn->actrate = a;
	}

	radiotx(wifi, wn, b);
}

static Wnode*
nodelookup(Wifi *wifi, uchar *bssid, int new)
{
	Wnode *wn, *nn;

	if(memcmp(bssid, wifi->ether->Netif.bcast, Eaddrlen) == 0)
		return nil;
	if((wn = wifi->bss) != nil){
		if(memcmp(wn->bssid, bssid, Eaddrlen) == 0)
			return wn;
	}
	if((nn = wifi->node) == wn)
		nn++;
	for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(wn == wifi->bss)
			continue;
		if(memcmp(wn->bssid, bssid, Eaddrlen) == 0)
			return wn;
		if((s32int)(wn->lastsend - nn->lastsend) < 0	/* GCC port: was (long), 32 bits in kencc */
		|| (s32int)(wn->lastseen - nn->lastseen) < 0)	/* GCC port: was (long), 32 bits in kencc */
			nn = wn;
	}
	if(!new)
		return nil;
	freewifikeys(wifi, nn);
	memset(nn, 0, sizeof(Wnode));
	memmove(nn->bssid, bssid, Eaddrlen);
	return nn;
}

void
wifitxfail(Wifi *wifi, Block *b)
{
	Wifipkt *w;
	Wnode *wn;

	if(b == nil)
		return;
	w = (Wifipkt*)b->rp;
	wn = nodelookup(wifi, w->a1, 0);
	if(wn == nil)
		return;
	wn->txerror++;
	if(wn->actrate != nil && wn->minrate != wn->actrate){
		uchar *a, *p;

		for(a = wn->minrate, p = wifi->rates; *p; p++){
			if(*p > *a && *p < *wn->actrate && (wn->validrates & (1UL << p-wifi->rates)) != 0)
				a = p;
		}
		wn->actrate = a;
	}
}

static uchar*
putrates(uchar *p, uchar *rates, ulong valid, ulong basic)
{
	uchar r[32];
	int n, i, m;

	/*
	 * 9space: the basic rates first, the first 8 in the supported
	 * rates element, the others in the extended supported rates
	 * element (9front repeated all of them there)
	 */
	valid |= basic;
	n = 0;
	for(i = 0; i < 32 && rates[i] != 0; i++)
		if(basic & (1UL<<i))
			r[n++] = rates[i] | 0x80;
	for(i = 0; i < 32 && rates[i] != 0; i++)
		if((valid & ~basic) & (1UL<<i))
			r[n++] = rates[i] & 0x7f;

	if(n > 0){
		/* supported rates */
		m = n > 8 ? 8 : n;
		*p++ = 1;
		*p++ = m;
		memmove(p, r, m);
		p += m;
	}
	if(n > 8){
		/* extended supported rates */
		*p++ = 50;
		*p++ = n - 8;
		memmove(p, r + 8, n - 8);
		p += n - 8;
	}

	return p;
}

static void
wifiprobe(Wifi *wifi, Wnode *wn)
{
	Wifipkt *w;
	Block *b;
	uchar *p;
	int n;

	n = strlen(wifi->essid);
	if(n == 0){
		/* no specific essid, just tell driver to tune channel */
		radiotx(wifi, wn, nil);
		return;
	}

	b = allocb(WIFIHDRSIZE + 512);
	w = (Wifipkt*)b->wp;

	w->fc[0] = 0x40;	/* probe request */
	w->fc[1] = 0x00;	/* STA->STA */
	memmove(w->a1, wifi->ether->Netif.bcast, Eaddrlen);	/* ??? */
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, wifi->ether->Netif.bcast, Eaddrlen);
	b->wp += WIFIHDRSIZE;
	p = b->wp;

	*p++ = 0;	/* set */
	*p++ = n;
	memmove(p, wifi->essid, n);
	p += n;

	p = putrates(p, wifi->rates, wn->validrates, wn->basicrates);

	if(wn->channel <= 14){	/* 9space: the 2.4 GHz (DSSS) band's only */
		*p++ = 3;	/* ds parameter set */
		*p++ = 1;
		*p++ = wn->channel;
	}

	b->wp = p;
	wifitx(wifi, wn, b);
}

static void
sendauth(Wifi *wifi, Wnode *bss)
{
	Wifipkt *w;
	Block *b;
	uchar *p;

	b = allocb(WIFIHDRSIZE + 3*2);
	w = (Wifipkt*)b->wp;
	w->fc[0] = 0xB0;	/* auth request */
	w->fc[1] = 0x00;	/* STA->STA */
	memmove(w->a1, bss->bssid, Eaddrlen);	/* ??? */
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, bss->bssid, Eaddrlen);
	b->wp += WIFIHDRSIZE;
	p = b->wp;
	*p++ = 0;	/* alg */
	*p++ = 0;
	*p++ = 1;	/* seq */
	*p++ = 0;
	*p++ = 0;	/* status */
	*p++ = 0;
	b->wp = p;

	bss->aid = 0;

	wifitx(wifi, bss, b);
}

/*
 * 9space: the radio's capability elements the access point offers too
 * (HT, VHT, HE), then WMM's information element where it has WMM and
 * the radio sends QoS data (wifi->wmm): QoS data from then on (bss->qos).  As OpenBSD's ieee80211_get_assoc_req
 * adds them (ieee80211_add_htcaps, ieee80211_add_wme_info); the radio
 * says what it has, wifi.c only what the access point offers.
 */
static uchar*
putcaps(Wifi *wifi, Wnode *bss, uchar *p)
{
	uchar *c, *e;
	int ok;

	bss->qos = 0;
	c = wifi->caps;
	e = c + wifi->ncaps;
	for(; c+2 <= e && c+2+c[1] <= e; c += 2+c[1]){
		switch(c[0]){
		case 45:
			ok = bss->ht;
			break;
		case 191:
			ok = bss->vht && bss->channel > 14;
			break;
		case 255:
			ok = bss->he && c[1] > 0 && c[2] == 35;
			break;
		default:
			ok = 0;
		}
		if(ok){
			memmove(p, c, 2+c[1]);
			p += 2+c[1];
		}
	}
	if(bss->wmm && wifi->wmm){
		/* WMM information element, version 1, no U-APSD */
		*p++ = 221;
		*p++ = 7;
		*p++ = 0x00;
		*p++ = 0x50;
		*p++ = 0xf2;
		*p++ = 0x02;
		*p++ = 0x00;	/* subtype: information */
		*p++ = 0x01;	/* version */
		*p++ = 0x00;	/* QoS info */
		bss->qos = 1;
	}
	return p;
}

static void
sendassoc(Wifi *wifi, Wnode *bss)
{
	Wifipkt *w;
	Block *b;
	uchar *p;
	int cap, n;

	b = allocb(WIFIHDRSIZE + 512);
	w = (Wifipkt*)b->wp;
	w->fc[0] = 0x00;	/* assoc request */
	w->fc[1] = 0x00;	/* STA->STA */
	memmove(w->a1, bss->bssid, Eaddrlen);	/* ??? */
	memmove(w->a2, wifi->ether->ea, Eaddrlen);
	memmove(w->a3, bss->bssid, Eaddrlen);

	b->wp += WIFIHDRSIZE;
	p = b->wp;

	/* capinfo */
	cap = 1;				// ESS
	cap |= (1<<5);				// Short Preamble
	cap |= (1<<10) & bss->cap;		// Short Slot Time
	*p++ = cap;
	*p++ = cap>>8;

	/* interval */
	*p++ = 16;
	*p++ = 16>>8;

	n = strlen(bss->ssid);
	*p++ = 0;	/* SSID */
	*p++ = n;
	memmove(p, bss->ssid, n);
	p += n;

	p = putrates(p, wifi->rates, bss->validrates, bss->basicrates);

	n = bss->rsnelen;
	if(n > 0){
		memmove(p, bss->rsne, n);
		p += n;
	}

	p = putcaps(wifi, bss, p);

	b->wp = p;
	wifitx(wifi, bss, b);
}

static void
setstatus(Wifi *wifi, Wnode *wn, char *new)
{
	char *old;

	old = wn->status;
	wn->status = new;
	/*
	 * 9space: the link is up once the access point is ours, not at
	 * wifsproc's next round (up to 500 ms later): what is sent in
	 * between was dropped (devether's oq while the link is down)
	 */
	if(wn == wifi->bss && (new == Sassoc || new == Sblocked))
		ethersetlink(wifi->ether, 1);
	/* 9space: our block ack agreement, once the port is open */
	if(wn == wifi->bss && new == Sassoc && old != Sassoc)
		sendaddba(wifi, wn);
	if(wifi->debug && new != old)
		print("#l%d: status %E: %.12ld %.12ld: %s -> %s (from pc=%#p)\n",
			wifi->ether->ctlrno, 
			wn->bssid, 
			TK2MS(MACHP(0)->PMach.ticks), TK2MS(MACHP(0)->PMach.ticks - wn->lastsend),
			old, new,
			getcallerpc(&wifi));
}

static void
recvassoc(Wifi *wifi, Wnode *wn, uchar *d, int len)
{
	uint s;

	if(len < 2+2+2)
		return;

	d += 2;	/* caps */
	s = d[0] | d[1]<<8;
	d += 2;
	switch(s){
	case 0x00:
		wn->aid = d[0] | d[1]<<8;
		if(wn->rsnelen > 0)
			setstatus(wifi, wn, Sblocked);
		else
			setstatus(wifi, wn, Sassoc);
		break;
	default:
		wn->aid = 0;
		setstatus(wifi, wn, Sunassoc);
	}
}

static void
recvbeacon(Wifi *wifi, Wnode *wn, uchar *d, int len)
{
	static uchar wpa1oui[4] = { 0x00, 0x50, 0xf2, 0x01 };
	static uchar wmmoui[4] = { 0x00, 0x50, 0xf2, 0x02 };	/* 9space */
	uchar *e, *x, *p, t;
	int rsnset, dschan, htchan;

	len -= 8+2+2;
	if(len < 0)
		return;

	/* timestamp */
	wn->ts = getts(d);
	d += 8;
	wn->ival = d[0] | d[1]<<8;
	d += 2;
	wn->cap = d[0] | d[1]<<8;
	d += 2;

	wn->dtimcount = 0;
	wn->dtimperiod = 1;
	wn->wmm = wn->ht = wn->vht = wn->he = 0;	/* 9space: as this beacon says */
	wn->htcap = wn->htprot = wn->htampdu = 0;
	wn->htrxmcs[0] = wn->htrxmcs[1] = 0;

	rsnset = 0;
	dschan = htchan = 0;
	for(e = d + len; d+2 <= e; d = x){
		d += 2;
		x = d + d[-1];
		if(x > e)			
			break;	/* truncated */
		t = d[-2];
		switch(t){
		case 0:		/* SSID */
			len = 0;
			while(len < Essidlen && d+len < x && d[len] != 0)
				len++;
			if(len == 0)
				continue;
			if(len != strlen(wn->ssid) || strncmp(wn->ssid, (char*)d, len) != 0){
				strncpy(wn->ssid, (char*)d, len);
				wn->ssid[len] = 0;
			}
			break;
		case 1:		/* supported rates */
		case 50:	/* extended rates */
			if(wifi->rates == nil)
				break;
			while(d < x){
				t = *d | 0x80;
				for(p = wifi->rates; *p != 0; p++){
					if(*p == t){
						wn->validrates |= 1UL << p-wifi->rates;
						if(*d & 0x80)
							wn->basicrates |= 1UL << p-wifi->rates;
						if(wn->minrate == nil || t < *wn->minrate)
							wn->minrate = p;
						if(wn->maxrate == nil || t > *wn->maxrate)
							wn->maxrate = p;
						break;
					}
				}
				d++;
			}
			if(wn->actrate == nil)
				wn->actrate = wn->maxrate;
			break;
		case 3:		/* DSPARAMS */
			if(d != x)
				dschan = d[0];
			break;
		case 61:	/* 9space: HT operation: primary channel, info 1, info 2 (protection in its low bits) */
			if(d != x)
				htchan = d[0];
			if(x - d >= 3)
				wn->htprot = d[2] & 3;
			break;
		case 45:	/* 9space: HT capabilities */
			wn->ht = 1;
			if(x - d >= 5){	/* info (2), A-MPDU parameters (1), MCS set (16) */
				wn->htcap = d[0] | d[1]<<8;
				wn->htampdu = d[2];
				wn->htrxmcs[0] = d[3];
				wn->htrxmcs[1] = d[4];
			}
			break;
		case 191:	/* 9space: VHT capabilities */
			wn->vht = 1;
			break;
		case 255:	/* 9space: an extension: 35 HE capabilities */
			if(d != x && d[0] == 35)
				wn->he = 1;
			break;
		case 5:
			if(x - d < 2)
				break;
			wn->dtimcount = d[0];
			if(d[1] > 0)
				wn->dtimperiod = d[1];
			break;
		case 221:	/* vendor specific */
			len = x - d;
			/* 9space: WMM's information or parameter element (Wi-Fi Alliance) */
			if(len >= 5 && memcmp(d, wmmoui, sizeof(wmmoui)) == 0 && (d[4] == 0 || d[4] == 1)){
				wn->wmm = 1;
				break;
			}
			if(rsnset || len < sizeof(wpa1oui) || memcmp(d, wpa1oui, sizeof(wpa1oui)) != 0)
				break;
			/* no break */
		case 48:	/* RSN information */
			len = x - &d[-2];
			memmove(wn->brsne, &d[-2], len);
			wn->brsnelen = len;
			rsnset = 1;
			break;
		}
	}

	/*
	 * 9space: on 5 GHz the DS parameter set is optional; then the HT
	 * operation element's primary channel, else the channel the radio
	 * is tuned to
	 */
	if(dschan != 0)
		wn->channel = dschan;
	else if(htchan != 0)
		wn->channel = htchan;
	else if(wn->channel == 0)
		wn->channel = wifi->channel;
}

static void
freewifikeys(Wifi *wifi, Wnode *wn)
{
	int i;

	wlock(&wifi->crypt);
	for(i=0; i<nelem(wn->rxkey); i++){
		secfree(wn->rxkey[i]);
		wn->rxkey[i] = nil;
	}
	for(i=0; i<nelem(wn->txkey); i++){
		secfree(wn->txkey[i]);
		wn->txkey[i] = nil;
	}
	wunlock(&wifi->crypt);
}

static void
wifideauth(Wifi *wifi, Wnode *wn)
{
	Ether *ether;
	Netfile *f;
	int i;

	/* deassociate node, clear keys */
	setstatus(wifi, wn, Sunauth);
	freewifikeys(wifi, wn);
	wn->aid = 0;

	if(wn != wifi->bss)
		return;

	/* 9space: the block ack agreements are over */
	for(i = 0; i < Ntid; i++)
		baoff(wifi, i, 0);
	wn->txba = 0;

	/* notify driver about node aid association */
	radiotx(wifi, wn, nil);

	/* notify aux/wpa with a zero length packet that we got deassociated from the ap */
	ether = wifi->ether;
	for(i=0; i<ether->Netif.nfile; i++){
		f = ether->Netif.f[i];
		if(f == nil || !f->inuse || f->type != 0x888e || waserror())
			continue;
		qflush(f->in);
		qwrite(f->in, 0, 0);
		poperror();
	}
	qflush(ether->oq);
}

/*
 * 9space: the radio went away (etherwlan: its data file closed) with its
 * state: the association is over, the scan starts again for the next one
 */
void
wifiradiogone(Wifi *wifi)
{
	Wnode *wn;

	if((wn = wifi->bss) != nil){
		wifideauth(wifi, wn);
		wifi->bss = nil;
	}
}

/* check if a node qualifies as our bss matching bssid and essid */
static int
goodbss(Wifi *wifi, Wnode *wn)
{
	if(wifi->ether->Netif.bypass)
		return 0;	/* we'r bypassed, dont try to associate */ 
	if(memcmp(wifi->bssid, wifi->ether->Netif.bcast, Eaddrlen) != 0){
		if(memcmp(wifi->bssid, wn->bssid, Eaddrlen) != 0)
			return 0;	/* bssid doesnt match */
	} else if(wifi->essid[0] == 0)
		return 0;	/* both bssid and essid unspecified */
	if(wifi->essid[0] != 0 && strcmp(wifi->essid, wn->ssid) != 0)
		return 0;	/* essid doesnt match */
	return 1;
}

/*
 * 9space: the node to join, where the driver gives the signal: none
 * before the scan went once over all its channels since the first that
 * matches was heard (candstep), then the strongest that matches, heard
 * in that pass (a mesh's access point rather than its far repeater; by
 * steps, not time: a pass over 2.4 and 5 GHz takes some 7 s), as
 * OpenBSD's ieee80211_node_choose_bss picks it: the strongest on 5 GHz
 * where its signal reaches IEEE80211_RSSI_THRES_5GHZ (-70 dBm), even if
 * one on 2.4 GHz is stronger (the 5 GHz band is less crowded, its
 * channels wider), else the strongest on either; without the signal wn,
 * the first that matches, as before.  None while the one chosen may
 * still answer (a second chosen meanwhile, a beacon of the node passed
 * over, would race it: the first authentication answered wins)
 */
enum {
	Rssithres5 = -70,	/* dBm, OpenBSD's IEEE80211_RSSI_THRES_5GHZ */
};

static Wnode*
bestbss(Wifi *wifi, Wnode *wn)
{
	Wnode *n, *best, *best5;
	ulong now;

	if(!wifi->sigs)
		return wn;
	if(!wifi->hascand){
		wifi->candstep = wifi->scanstep;
		wifi->hascand = 1;
	}
	if(wifi->scanstep - wifi->candstep < wifi->scanlen)
		return nil;	/* not yet on every channel since */
	now = MACHP(0)->PMach.ticks;
	if(TK2MS(now - wifi->lastauth) <= 1000)
		return nil;	/* the chosen one's answer to come: no second (its auth would win if first) */
	best = best5 = nil;
	for(n = wifi->node; n != &wifi->node[nelem(wifi->node)]; n++){
		if(!n->hasrssi || n->seenstep - wifi->candstep > wifi->scanstep - wifi->candstep
		|| !goodbss(wifi, n))
			continue;
		if(TK2MS(now - n->lastsend) <= 1000)
			continue;	/* tried just now */
		if(best == nil || n->rssi > best->rssi)
			best = n;
		if(n->channel > 14 && (best5 == nil || n->rssi > best5->rssi))
			best5 = n;
	}
	if(best5 != nil && (best5->rssi >= Rssithres5 || best5->rssi >= best->rssi))
		best = best5;
	if(best != nil && wifi->debug)
		print("#l%d: the strongest: %E %d dBm\n", wifi->ether->ctlrno, best->bssid, best->rssi);
	return best;
}

/*
 * 9space: roaming (Wifi.roam): the associated node's signal below the
 * threshold for 5 s, doubled for each time the scan chose the same
 * node again (at most 5 min); the scan that follows chooses the
 * strongest (bestbss). OpenBSD scans in the background instead
 * (ieee80211_bgscan), which needs the firmware's scan.
 */
enum {
	Roamdefault	= -75,	/* dBm: below it a scan costs less than the link (OpenBSD scans in the background below -60/-70) */
	Roamwait	= 5,	/* s */
	Roammaxfail	= 64,
};

static int
roamweak(Wifi *wifi, Wnode *wn, ulong now)
{
	if(!wifi->sigs || wifi->roam == 0 || wn->status != Sassoc || !wn->hasrssi
	|| memcmp(wifi->bssid, wifi->ether->Netif.bcast, Eaddrlen) != 0){
		wifi->weaksince = 0;
		return 0;	/* off, not associated, or a bssid asked for */
	}
	if(wn->rssi >= wifi->roam){
		wifi->weaksince = 0;
		return 0;
	}
	if(wifi->weaksince == 0){
		wifi->weaksince = now;
		return 0;
	}
	if(TK2SEC(now - wifi->weaksince) < Roamwait*(1+wifi->roamfail))
		return 0;
	wifi->weaksince = 0;
	return 1;
}

enum {
	Bgscanmax	= 15,	/* s: a background scan's end, at the latest */
	Roamhyst	= 8,	/* dB: how much stronger another node must be */
};

/*
 * 9space: the strongest node of the network heard during the background
 * scan, if it is Roamhyst stronger than ours (on 5 GHz where it reaches
 * Rssithres5, as bestbss); nil if none
 */
static Wnode*
roambetter(Wifi *wifi, Wnode *cur)
{
	Wnode *n, *best, *best5;

	best = best5 = nil;
	for(n = wifi->node; n != &wifi->node[nelem(wifi->node)]; n++){
		if(n == cur || !n->hasrssi || n->channel == 0 || !goodbss(wifi, n))
			continue;
		if((s32int)(n->lastseen - wifi->bgscanstart) < 0)	/* GCC port: 32 bits, as the ticks */
			continue;	/* not heard in this scan */
		if(best == nil || n->rssi > best->rssi)
			best = n;
		if(n->channel > 14 && (best5 == nil || n->rssi > best5->rssi))
			best5 = n;
	}
	if(best5 != nil && (best5->rssi >= Rssithres5 || best5->rssi >= best->rssi))
		best = best5;
	if(best == nil || best->rssi < cur->rssi + Roamhyst)
		return nil;
	return best;
}

/* 9space: the driver's background scan ended (from its interrupt) */
void
wifibgscandone(Wifi *wifi)
{
	wifi->bgscanning = 0;
}

/* 9space: after a roaming scan: the same node again waits longer */
static void
roamchose(Wifi *wifi, Wnode *wn)
{
	static uchar zeros[Eaddrlen];

	if(memcmp(wifi->roamfrom, zeros, Eaddrlen) == 0)
		return;
	if(memcmp(wifi->roamfrom, wn->bssid, Eaddrlen) == 0){
		if(wifi->roamfail < Roammaxfail)
			wifi->roamfail = wifi->roamfail? 2*wifi->roamfail: 1;
	} else
		wifi->roamfail = 0;
	memset(wifi->roamfrom, 0, Eaddrlen);
	if(wifi->debug)
		print("#l%d: roamed to %E (wait now %d s)\n", wifi->ether->ctlrno, wn->bssid, Roamwait*(1+wifi->roamfail));
}

static int
iqready(void *a)
{
	Wifi *wifi;

	wifi = a;
	return qcanread(wifi->iq) || qisclosed(wifi->iq);
}

/* an encrypted frame, decrypted and on (wifiproc's) */
static void
decryptrecv(Wifi *wifi, Block *b)
{
	Wifipkt *w;
	Wnode *wn;

	w = (Wifipkt*)b->rp;
	if((wn = nodelookup(wifi, w->a2, 0)) == nil){
		freeb(b);
		return;
	}
	wn->lastseen = MACHP(0)->PMach.ticks;
	if((b->flag & Bsignal) && wn == wifi->bss && wn->hasrssi)
		wn->rssi = (7*wn->rssi + (schar)b->rp[-9]) / 8;	/* 9space: its data's signal too (roaming) */
	if((b = wifidecrypt(wifi, wn, b)) == nil)
		return;
	w = (Wifipkt*)b->rp;
	if(w->fc[1] & 0x40){
		freeb(b);
		return;
	}
	b->flag &= ~Btimestamp;
	wifirecv(wifi, b);
}

/* 9space: a frame the block ack reorder lets go, in wifiproc */
static void
inorder(Wifi *wifi, Block *b)
{
	if(BLEN(b) >= WIFIHDRSIZE && (((Wifipkt*)b->rp)->fc[1] & 0x40)){
		decryptrecv(wifi, b);
		return;
	}
	wifirecv(wifi, b);
}

static void
wifiproc(void *arg)
{
	Wifi *wifi;
	Wifipkt *w;
	Wnode *wn;
	Block *b;

	b = nil;
	wifi = arg;
	while(waserror())
		;
	for(;;){
		if(b != nil){
			freeb(b);
			b = nil;
			continue;
		}
		/*
		 * 9space: frames held for the block ack reorder wait at most
		 * Batimeout (batick): then no longer than 20 ms for the queue
		 */
		if(!iqready(wifi)){
			if(baheld(wifi))
				tsleep(&wifi->iqr, iqready, wifi, 20);
			else
				sleep(&wifi->iqr, iqready, wifi);
		}
		batick(wifi);
		if(!qcanread(wifi->iq)){
			if(qisclosed(wifi->iq))
				break;
			continue;
		}
		if((b = qbread(wifi->iq, 100000)) == nil)
			break;
		if(baframe(wifi, b)){
			bareorder(wifi, b);
			b = nil;
			continue;
		}
		w = (Wifipkt*)b->rp;
		if(w->fc[1] & 0x40){
			/* encrypted */
			decryptrecv(wifi, b);
			b = nil;
			continue;
		}
		/* management */
		if((w->fc[0] & 0x0c) != 0x00)
			continue;

		switch(w->fc[0] & 0xf0){
		case 0x50:	/* probe response */
			if(wifi->debug)
				print("#l%d: got probe from %E\n", wifi->ether->ctlrno, w->a3);
			/* no break */
		case 0x80:	/* beacon */
			if((wn = nodelookup(wifi, w->a3, 1)) == nil)
				continue;
			wn->lastseen = MACHP(0)->PMach.ticks;
			if(b->flag & Btimestamp)
				wn->rs = getts(b->rp - 8);
			if(b->flag & Bsignal){
				/* 9space: the signal, smoothed (3/4 the old) */
				if(wn->hasrssi)
					wn->rssi = (3*wn->rssi + (schar)b->rp[-9]) / 4;
				else
					wn->rssi = (schar)b->rp[-9];
				wn->hasrssi = 1;
				wn->seenstep = wifi->scanstep;
			}
			b->rp += wifihdrlen(w);
			recvbeacon(wifi, wn, b->rp, BLEN(b));

			if(wifi->bss == nil
			&& TK2MS(MACHP(0)->PMach.ticks - wn->lastsend) > 1000
			&& goodbss(wifi, wn)
			&& (wn = bestbss(wifi, wn)) != nil){
				setstatus(wifi, wn, Sconn);
				sendauth(wifi, wn);
				wifi->lastauth = wn->lastsend;
			}
			continue;
		}

		if(memcmp(w->a1, wifi->ether->ea, Eaddrlen))
			continue;
		if((wn = nodelookup(wifi, w->a3, 0)) == nil)
			continue;
		wn->lastseen = MACHP(0)->PMach.ticks;
		if(b->flag & Btimestamp)
			wn->rs = getts(b->rp - 8);
		switch(w->fc[0] & 0xf0){
		case 0x10:	/* assoc response */
		case 0x30:	/* reassoc response */
			b->rp += wifihdrlen(w);
			recvassoc(wifi, wn, b->rp, BLEN(b));
			/* notify driver about node aid association */
			if(wn == wifi->bss)
				radiotx(wifi, wn, nil);
			break;
		case 0xb0:	/* auth */
			if(wifi->debug)
				print("#l%d: got auth from %E\n", wifi->ether->ctlrno, wn->bssid);
			if(wn->brsnelen > 0 && wn->rsnelen == 0)
				setstatus(wifi, wn, Sneedauth);
			else
				setstatus(wifi, wn, Sauth);
			if(wifi->bss == nil && goodbss(wifi, wn)){
				wifi->bss = wn;
				roamchose(wifi, wn);	/* 9space */
				if(wn->status == Sauth)
					sendassoc(wifi, wn);
			}
			break;
		case 0xc0:	/* deauth */
			if(wifi->debug)
				print("#l%d: got deauth from %E\n", wifi->ether->ctlrno, wn->bssid);
			wifideauth(wifi, wn);
			break;
		case 0xd0:	/* 9space: action */
			b->rp += wifihdrlen(w);
			recvba(wifi, wn, b->rp, BLEN(b));
			break;
		}
	}
	pexit("wifi in queue closed", 1);
}

static void
wifietheroq(Wifi *wifi, Block *b)
{
	Etherpkt e;
	Wifipkt h;
	int hdrlen;
	Wnode *wn;
	SNAP *s;

	if(BLEN(b) < ETHERHDRSIZE)
		goto drop;
	if((wn = wifi->bss) == nil)
		goto drop;

	memmove(&e, b->rp, ETHERHDRSIZE);
	b->rp += ETHERHDRSIZE;
	if(wn->status == Sblocked){
		/* only pass EAPOL frames when port is blocked */
		if((e.type[0]<<8 | e.type[1]) != 0x888e)
			goto drop;
	} else if(wn->status != Sassoc)
		goto drop;

	h.fc[0] = wn->qos ? 0x88 : 0x08;	/* data; 9space: QoS data with WMM */
	memmove(h.a1, wn->bssid, Eaddrlen);
	if(memcmp(e.s, wifi->ether->ea, Eaddrlen) == 0) {
		h.fc[1] = 0x01;	/* STA->AP */
	} else {
		h.fc[1] = 0x03;	/* AP->AP (WDS) */
		memmove(h.a2, wifi->ether->ea, Eaddrlen);
	}
	memmove(dstaddr(&h), e.d, Eaddrlen);
	memmove(srcaddr(&h), e.s, Eaddrlen);

	hdrlen = wifihdrlen(&h);
	b = padblock(b, hdrlen + SNAPHDRSIZE);
	memmove(b->rp, &h, hdrlen);
	if(wn->qos){
		/* 9space: QoS control after the header: TID 0 (best effort), normal ack */
		b->rp[hdrlen-2] = 0;
		b->rp[hdrlen-1] = 0;
	}
	s = (SNAP*)(b->rp + hdrlen);
	s->dsap = s->ssap = 0xAA;
	s->control = 0x03;
	s->orgcode[0] = 0;
	s->orgcode[1] = 0;
	s->orgcode[2] = 0;
	memmove(s->type, e.type, 2);

	wifitx(wifi, wn, b);
	return;
drop:
	freeb(b);
}

static void
wifoproc(void *arg)
{
	Ether *ether;
	Wifi *wifi;
	Block *b;

	wifi = arg;
	ether = wifi->ether;
	while(waserror())
		;
	while((b = qbread(ether->oq, 1000000)) != nil)
		wifietheroq(wifi, b);
	pexit("ether out queue closed", 1);
}

static void
wifsproc(void *arg)
{
	Ether *ether;
	Wifi *wifi;
	Wnode wnscan;
	Wnode *wn;
	ulong now, tmout;
	uchar *rate, *scan, *list;
	int dwell, i;

	wifi = arg;
	ether = wifi->ether;

	wn = &wnscan;
	memset(wn, 0, sizeof(*wn));
	memmove(wn->bssid, ether->Netif.bcast, Eaddrlen);

	scan = nil;
	i = 0;
	while(waserror())
		;
Scan:
	wifi->hascand = 0;	/* 9space: a new choice after a full pass (bestbss) */
	scan = nil;
	/*
	 * scan for access point
	 * 9space: the channels of wifi->scan; on channels with radar
	 * detection only listen (a beacon, 100 TU, in 200 ms); a probe
	 * response comes fast: 100 ms on the others on 5 GHz
	 */
	while(wifi->bss == nil){
		/*
		 * 9space: an authentication just sent (roaming to a node
		 * chosen after a background scan): its answer comes on that
		 * node's channel, the radio stays there for a second
		 */
		if(TK2MS(MACHP(0)->PMach.ticks - wifi->lastauth) < 1000){
			tsleep(&up->sleep, return0, 0, 100);
			continue;
		}
		ethersetlink(ether, 0);
		list = wifi->scan != nil ? wifi->scan : wifiscan2g;
		if(list != scan || list[i] == 0){
			scan = list;	/* the driver may set it after wifiattach */
			i = 0;
			for(wifi->scanlen = 0; scan[wifi->scanlen] != 0; wifi->scanlen++)
				;
		}
		wifi->scanstep++;	/* 9space: the channels tuned to (bestbss) */
		wnscan.channel = scan[i++];
		wnscan.validrates = chanrates(wifi, wnscan.channel);
		dwell = 200;
		if(passivechan(wnscan.channel))
			radiotx(wifi, &wnscan, nil);
		else {
			wifiprobe(wifi, &wnscan);
			if(wnscan.channel > 14 && wifi->essid[0] != 0)
				dwell = 100;
		}
		do {
			tsleep(&up->sleep, return0, 0, dwell);
			now = MACHP(0)->PMach.ticks;
		} while(TK2MS(now-wifi->lastauth) < 1000);
	}

	/* maintain access point */
	tmout = 0;
	while((wn = wifi->bss) != nil){
		if(wn->status == Sassoc || wn->status == Sblocked){
			if((rate = wn->actrate) != nil)
				ethersetspeed(ether, ((*rate & 0x7f)+3)/4);
			ethersetlink(ether, 1);
		}
		now = MACHP(0)->PMach.ticks;
		if(wn->status != Sneedauth && TK2SEC(now - wn->lastseen) > 20
		|| goodbss(wifi, wn) == 0){
			wifideauth(wifi, wn);
			wifi->bss = nil;
			break;
		}
		/*
		 * 9space: with the driver's background scan the weak signal
		 * starts one; after it the strongest node heard then, if
		 * clearly better, is joined directly (roambetter); without
		 * one the node is left and the scan below chooses again
		 */
		if(wifi->bgscanning && TK2SEC(now - wifi->bgscanstart) > Bgscanmax)
			wifi->bgscanning = 0;	/* no word of its end: given up */
		if(wifi->bgscanstart != 0 && !wifi->bgscanning){
			Wnode *nn;

			nn = roambetter(wifi, wn);
			wifi->bgscanstart = 0;
			if(nn == nil){
				if(wifi->roamfail < Roammaxfail)
					wifi->roamfail = wifi->roamfail? 2*wifi->roamfail: 1;
				if(wifi->debug)
					print("#l%d: background scan: none better (wait now %d s)\n",
						wifi->ether->ctlrno, Roamwait*(1+wifi->roamfail));
			} else {
				if(wifi->debug)
					print("#l%d: roaming: %E %d dBm to %E %d dBm channel %d\n", wifi->ether->ctlrno,
						wn->bssid, wn->rssi, nn->bssid, nn->rssi, nn->channel);
				memmove(wifi->roamfrom, wn->bssid, Eaddrlen);
				wifideauth(wifi, wn);
				wifi->bss = nil;
				setstatus(wifi, nn, Sconn);
				sendauth(wifi, nn);
				wifi->lastauth = nn->lastsend;
				break;
			}
		}
		if(roamweak(wifi, wn, now)){
			if(wifi->bgscan != nil){
				if(!wifi->bgscanning && wifi->bgscanstart == 0){
					wifi->bgscanstart = now;
					wifi->bgscanning = 1;
					if((*wifi->bgscan)(wifi) < 0){
						wifi->bgscanning = 0;
						wifi->bgscanstart = 0;
					} else if(wifi->debug)
						print("#l%d: background scan: %E at %d dBm\n", wifi->ether->ctlrno, wn->bssid, wn->rssi);
				}
			} else {
				if(wifi->debug)
					print("#l%d: roaming: %E at %d dBm\n", wifi->ether->ctlrno, wn->bssid, wn->rssi);
				memmove(wifi->roamfrom, wn->bssid, Eaddrlen);
				wifideauth(wifi, wn);
				wifi->bss = nil;
				break;
			}
		}
		if(TK2MS(now - wn->lastsend) > 1000){
			if((wn->status == Sauth || wn->status == Sblocked) && (++tmout & 7) == 0)
				wifideauth(wifi, wn);	/* stuck in auth, start over */
			if(wn->status == Sconn || wn->status == Sunauth)
				sendauth(wifi, wn);
			if(wn->status == Sauth)
				sendassoc(wifi, wn);
		}
		tsleep(&up->sleep, return0, 0, 500);
	}
	goto Scan;
}

Wifi*
wifiattach(Ether *ether, void (*transmit)(Wifi*, Wnode*, Block*))
{
	char name[32];
	Wifi *wifi;

	wifi = malloc(sizeof(Wifi));
	if(wifi == nil)
		error(Enomem);
	wifi->iq = qopen(ether->Netif.limit, 0, 0, 0);
	if(wifi->iq == nil){
		free(wifi);
		error(Enomem);
	}
	if(ether->dmat == nil)
		ether->dmat = malloc(sizeof(DMAT));
	wifi->ether = ether;
	wifi->transmit = transmit;

	wifi->rates = basicrates;

	wifi->essid[0] = 0;
	memmove(wifi->bssid, ether->Netif.bcast, Eaddrlen);

	wifi->lastauth = MACHP(0)->PMach.ticks;
	wifi->roam = Roamdefault;	/* 9space: only where the driver gives the signal (sigs) */

	snprint(name, sizeof(name), "#l%dwifi", ether->ctlrno);
	kproc(name, wifiproc, wifi);
	snprint(name, sizeof(name), "#l%dwifo", ether->ctlrno);
	kproc(name, wifoproc, wifi);
	snprint(name, sizeof(name), "#l%dwifs", ether->ctlrno);
	kproc(name, wifsproc, wifi);

	return wifi;
}

static char *ciphers[] = {
	[0]	"clear",
	[TKIP]	"tkip",
	[CCMP]	"ccmp",
};

static Wkey*
parsekey(char *s)
{
	static char Ebadkey[] = "bad key";
	uchar key[32];
	int len, cipher;
	char *e;
	Wkey *k;

	for(cipher=0; cipher<nelem(ciphers); cipher++){
		if(strncmp(s, ciphers[cipher], len = strlen(ciphers[cipher])) == 0){
			if(cipher == 0)	/* clear */
				return nil;
			if(s[len] == ':'){
				s += len+1;
				break;
			}
		}
	}
	if(cipher >= nelem(ciphers))
		error(Ebadkey);

	if((e = strchr(s, '@')) == nil)
		e = strchr(s, 0);

	len = dec16(key, sizeof(key), s, e - s);

	switch(cipher){
	case TKIP:
		if(len != 32)
			error(Ebadkey);
		k = secalloc(sizeof(Wkey) + len);
		memmove(k->key, key, len);
		break;
	case CCMP:
		if(len != 16)
			error(Ebadkey);
		k = secalloc(sizeof(Wkey) + sizeof(AESstate));
		setupAESstate((AESstate*)k->key, key, len, nil);
		break;
	default:
		error(Ebadkey);
	}

	memset(key, 0, sizeof(key));

	if(*e++ == '@')
		k->tsc = strtoull(e, nil, 16);
	k->len = len;
	k->cipher = cipher;

	return k;
}

void
wificfg(Wifi *wifi, char *opt)
{
	char *p, buf[64];
	int n;

	if(strncmp(opt, "debug=", 6))
	if(strncmp(opt, "essid=", 6))
	if(strncmp(opt, "bssid=", 6))
		return;
	if((p = strchr(opt, '=')) == nil)
		return;
	if(waserror())
		return;
	n = snprint(buf, sizeof(buf), "%.*s %q", utfnlen(opt, p - opt), opt, p+1);
	wifictl(wifi, buf, n);
	poperror();
}

enum {
	CMdebug,
	CMessid,
	CMauth,
	CMbssid,
	CMrxkey0,
	CMrxkey1,
	CMrxkey2,
	CMrxkey3,
	CMrxkey4,
	CMtxkey0,
	CMroam,		/* 9space */
};

static Cmdtab wifictlmsg[] =
{
	CMdebug,	"debug",	0,
	CMroam,		"roam",		2,	/* 9space: roam dBm (0: never) */
	CMessid,	"essid",	0,
	CMauth,		"auth",		0,
	CMbssid,	"bssid",	0,

	CMrxkey0,	"rxkey0",	0,	/* group keys */
	CMrxkey1,	"rxkey1",	0,
	CMrxkey2,	"rxkey2",	0,
	CMrxkey3,	"rxkey3",	0,

	CMrxkey4,	"rxkey",	0,	/* peerwise keys */
	CMtxkey0,	"txkey",	0,

	CMtxkey0,	"txkey0",	0,
};

long
wifictl(Wifi *wifi, void *buf, long n)
{
	uchar addr[Eaddrlen];
	Cmdbuf *cb;
	Cmdtab *ct;
	Wnode *wn;
	Wkey *k, **kk;

	cb = nil;
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(wifi->debug)
		print("#l%d: wifictl: %.*s\n", wifi->ether->ctlrno, utfnlen(buf, n), buf);
	memmove(addr, wifi->ether->Netif.bcast, Eaddrlen);
	wn = wifi->bss;
	cb = parsecmd(buf, n);
	ct = lookupcmd(cb, wifictlmsg, nelem(wifictlmsg));
	if(ct->index >= CMauth){
		if(cb->nf > 1 && (ct->index == CMbssid || ct->index >= CMrxkey0)){
			if(parseether(addr, cb->f[1]) == 0){
				cb->f++;
				cb->nf--;
				wn = nodelookup(wifi, addr, 0);
			}
		}
		if(wn == nil && ct->index != CMbssid)
			error("missing node");
	}
	switch(ct->index){
	case CMroam:
		wifi->roam = atoi(cb->f[1]);
		wifi->weaksince = 0;
		wifi->roamfail = 0;
		break;
	case CMdebug:
		if(cb->f[1] != nil)
			wifi->debug = atoi(cb->f[1]);
		else
			wifi->debug ^= 1;
		print("#l%d: debug: %d\n", wifi->ether->ctlrno, wifi->debug);
		break;
	case CMessid:
		if(cb->f[1] != nil)
			strncpy(wifi->essid, cb->f[1], Essidlen);
		else
			wifi->essid[0] = 0;
	Findbss:
		wn = wifi->bss;
		if(wn != nil){
			if(goodbss(wifi, wn))
				break;
			wifideauth(wifi, wn);
		}
		wifi->bss = nil;
		if(wifi->essid[0] == 0 && memcmp(wifi->bssid, wifi->ether->Netif.bcast, Eaddrlen) == 0)
			break;
		if(wifi->sigs){
			/*
			 * 9space: the radio gives the signal: the beacons choose
			 * (bestbss) after a full pass, not the first of the nodes
			 * known from before to answer
			 */
			wifi->hascand = 0;
			break;
		}
		for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++)
			if(goodbss(wifi, wn)){
				setstatus(wifi, wn, Sconn);
				sendauth(wifi, wn);
			}
		break;
	case CMbssid:
		memmove(wifi->bssid, addr, Eaddrlen);
		goto Findbss;
	case CMauth:
		freewifikeys(wifi, wn);
		if(cb->f[1] == nil)
			wn->rsnelen = 0;
		else
			wn->rsnelen = dec16(wn->rsne, sizeof(wn->rsne), cb->f[1], strlen(cb->f[1]));
		if(wn->aid == 0){
			setstatus(wifi, wn, Sconn);
			sendauth(wifi, wn);
		} else {
			setstatus(wifi, wn, Sauth);
			sendassoc(wifi, wn);
		}
		break;
	case CMrxkey0: case CMrxkey1: case CMrxkey2: case CMrxkey3: case CMrxkey4:
	case CMtxkey0:
		if(cb->f[1] == nil)
			error(Ebadarg);
		k = parsekey(cb->f[1]);
		memset(cb->f[1], 0, strlen(cb->f[1]));
		if(ct->index < CMtxkey0)
			kk = &wn->rxkey[ct->index - CMrxkey0];
		else
			kk = &wn->txkey[ct->index - CMtxkey0];
		wlock(&wifi->crypt);
		secfree(*kk);
		*kk = k;
		wunlock(&wifi->crypt);
		if(ct->index >= CMtxkey0 && wn->status == Sblocked)
			setstatus(wifi, wn, Sassoc);
		break;
	}
	poperror();
	free(cb);
	return n;
}

char*
wifistat(Wifi *wifi, char *p, char *e)
{
	static uchar zeros[Eaddrlen];
	char essid[Essidlen+1];
	Wnode *wn;
	Wkey *k;
	long now;
	int i;

	if(wifi == nil || p >= e)
		return p;

	wn = wifi->bss;
	if(wn != nil){
		strncpy(essid, wn->ssid, Essidlen);
		essid[Essidlen] = 0;
		p = seprint(p, e, "essid: %s\n", essid);
		p = seprint(p, e, "bssid: %E\n", wn->bssid);
		p = seprint(p, e, "status: %s\n", wn->status);
		p = seprint(p, e, "channel: %.2d\n", wn->channel);
		/* 9space: what the access point offers, and whether our data is QoS (WMM) */
		p = seprint(p, e, "offers:%s%s%s%s\n", wn->wmm ? " wmm" : "", wn->ht ? " ht" : "",
			wn->vht ? " vht" : "", wn->he ? " he" : "");
		p = seprint(p, e, "qos: %s\n", wn->qos ? "wmm" : "none");
		if(wn->hasrssi)
			p = seprint(p, e, "signal: %d dBm\n", wn->rssi);
		/* 9space: the block ack agreements, the frames CCMP dropped as replays */
		for(i = 0; i < Ntid; i++){
			Wba *ba = &wifi->rxba[i];
			if(ba->on)
				p = seprint(p, e, "rxba: tid %d win %d held %lud expired %lud\n",
					i, ba->win, ba->held, ba->expired);
		}
		if(wn->txba)
			p = seprint(p, e, "txba: %#ux\n", wn->txba);
		p = seprint(p, e, "replays: %lud\n", wifi->replays);

		/* only print key ciphers and key length */
		rlock(&wifi->crypt);
		for(i = 0; i<nelem(wn->rxkey); i++){
			if((k = wn->rxkey[i]) != nil)
				p = seprint(p, e, "rxkey%d: %s:[%d]\n", i,
					ciphers[k->cipher], k->len);
		}
		for(i = 0; i<nelem(wn->txkey); i++){
			if((k = wn->txkey[i]) != nil)
				p = seprint(p, e, "txkey%d: %s:[%d]\n", i,
					ciphers[k->cipher], k->len);
		}
		runlock(&wifi->crypt);

		if(wn->brsnelen > 0){
			p = seprint(p, e, "brsne: ");
			for(i=0; i<wn->brsnelen; i++)
				p = seprint(p, e, "%.2X", wn->brsne[i]);
			p = seprint(p, e, "\n");
		}
	} else {
		p = seprint(p, e, "essid: %s\n", wifi->essid);
		p = seprint(p, e, "bssid: %E\n", wifi->bssid);
	}

	now = MACHP(0)->PMach.ticks;
	for(wn = wifi->node; wn != &wifi->node[nelem(wifi->node)]; wn++){
		if(wn->lastseen == 0)
			continue;
		strncpy(essid, wn->ssid, Essidlen);
		essid[Essidlen] = 0;
		/* 9space: the signal (dBm, 0: none), before the essid */
		p = seprint(p, e, "node: %E %.4x %-11ld %.2d %4d %s\n",
			wn->bssid, wn->cap, TK2MS(now - wn->lastseen), wn->channel,
			wn->hasrssi ? wn->rssi : 0, essid);
	}
	return p;
}

static void tkipencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static int tkipdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static void ccmpencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);
static int ccmpdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc);

static Block*
wifiencrypt(Wifi *wifi, Wnode *wn, Block *b)
{
	uvlong tsc;
	int n, kid;
	Wifipkt *w;
	Wkey *k;

	rlock(&wifi->crypt);

	kid = 0;
	k = wn->txkey[kid];
	if(k == nil){
		runlock(&wifi->crypt);
		return b;
	}

	n = wifihdrlen((Wifipkt*)b->rp);

	b = padblock(b, 8);
	b = padblock(b, -(8+4));

	w = (Wifipkt*)b->rp;
	memmove(w, b->rp+8, n);
	b->rp += n;

	tsc = ++k->tsc;

	switch(k->cipher){
	case TKIP:
		b->rp[0] = tsc>>8;
		b->rp[1] = (b->rp[0] | 0x20) & 0x7f;
		b->rp[2] = tsc;
		b->rp[3] = kid<<6 | 0x20;
		b->rp[4] = tsc>>16;
		b->rp[5] = tsc>>24;
		b->rp[6] = tsc>>32;
		b->rp[7] = tsc>>40;
		b->rp += 8;
		tkipencrypt(k, w, b, tsc);
		break;
	case CCMP:
		b->rp[0] = tsc;
		b->rp[1] = tsc>>8;
		b->rp[2] = 0;
		b->rp[3] = kid<<6 | 0x20;
		b->rp[4] = tsc>>16;
		b->rp[5] = tsc>>24;
		b->rp[6] = tsc>>32;
		b->rp[7] = tsc>>40;
		b->rp += 8;
		ccmpencrypt(k, w, b, tsc);
		break;
	}
	runlock(&wifi->crypt);

	b->rp = (uchar*)w;
	w->fc[1] |= 0x40;
	return b;
}

static Block*
wifidecrypt(Wifi *wifi, Wnode *wn, Block *b)
{
	uvlong tsc;
	int n, kid;
	Wifipkt *w;
	Wkey *k;

	rlock(&wifi->crypt);

	w = (Wifipkt*)b->rp;
	n = wifihdrlen(w);
	b->rp += n;
	if(BLEN(b) < 8+8)
		goto drop;

	kid = b->rp[3]>>6;
	if((b->rp[3] & 0x20) == 0)
		goto drop;
	if((w->a1[0] & 1) == 0)
		kid = 4;	/* use peerwise key for non-unicast */

	k = wn->rxkey[kid];
	if(k == nil)
		goto drop;
	switch(k->cipher){
	case TKIP:
		tsc =	(uvlong)b->rp[7]<<40 |
			(uvlong)b->rp[6]<<32 |
			(uvlong)b->rp[5]<<24 |
			(uvlong)b->rp[4]<<16 |
			(uvlong)b->rp[0]<<8 |
			(uvlong)b->rp[2];
		b->rp += 8;
		if(tsc <= k->tsc){
			wifi->replays++;	/* 9space */
			goto drop;
		}
		if(tkipdecrypt(k, w, b, tsc) != 0)
			goto drop;
		break;
	case CCMP:
		tsc =	(uvlong)b->rp[7]<<40 |
			(uvlong)b->rp[6]<<32 |
			(uvlong)b->rp[5]<<24 |
			(uvlong)b->rp[4]<<16 |
			(uvlong)b->rp[1]<<8 |
			(uvlong)b->rp[0];
		b->rp += 8;
		if(tsc <= k->tsc){
			wifi->replays++;	/* 9space */
			goto drop;
		}
		if(ccmpdecrypt(k, w, b, tsc) != 0)
			goto drop;
		break;
	default:
	drop:
		runlock(&wifi->crypt);
		freeb(b);
		return nil;
	}
	runlock(&wifi->crypt);

	k->tsc = tsc;
	b->rp -= n;
	memmove(b->rp, w, n);
	w = (Wifipkt*)b->rp;
	w->fc[1] &= ~0x40;
	return b;
}

static u16int Sbox[256] = {
	0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
	0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
	0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
	0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
	0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
	0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
	0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
	0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
	0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
	0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
	0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
	0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
	0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
	0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
	0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
	0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
	0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
	0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
	0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
	0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
	0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
	0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
	0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
	0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
	0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
	0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
	0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
	0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
	0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
	0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
	0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
	0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A
};

static void
tkipk2tk(uchar key[16], u16int tk[8])
{
	tk[0] = (u16int)key[1]<<8 | key[0];
	tk[1] = (u16int)key[3]<<8 | key[2];
	tk[2] = (u16int)key[5]<<8 | key[4];
	tk[3] = (u16int)key[7]<<8 | key[6];
	tk[4] = (u16int)key[9]<<8 | key[8];
	tk[5] = (u16int)key[11]<<8 | key[10];
	tk[6] = (u16int)key[13]<<8 | key[12];
	tk[7] = (u16int)key[15]<<8 | key[14];
}

static void
tkipphase1(u32int tscu, uchar ta[Eaddrlen], u16int tk[8], u16int p1k[5])
{
	u16int *k, i, x0, x1, x2;

	p1k[0] = tscu;
	p1k[1] = tscu>>16;
	p1k[2] = (u16int)ta[1]<<8 | ta[0];
	p1k[3] = (u16int)ta[3]<<8 | ta[2];
	p1k[4] = (u16int)ta[5]<<8 | ta[4];

	for(i=0; i<8; i++){
		k = &tk[i & 1];

		x0 = p1k[4] ^ k[0];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[0] += x2;
		x0 = p1k[0] ^ k[2];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[1] += x2;
		x0 = p1k[1] ^ k[4];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[2] += x2;
		x0 = p1k[2] ^ k[6];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[3] += x2;
		x0 = p1k[3] ^ k[0];
		x1 = Sbox[x0 >> 8];
		x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
		p1k[4] += x2;

		p1k[4] += i;
	}
}

static void
tkipphase2(u16int tscl, u16int p1k[5], u16int tk[8], uchar rc4key[16])
{
	u16int ppk[6], x0, x1, x2;

	ppk[0] = p1k[0];
	ppk[1] = p1k[1];
	ppk[2] = p1k[2];
	ppk[3] = p1k[3];
	ppk[4] = p1k[4];
	ppk[5] = p1k[4] + tscl;

	x0 = ppk[5] ^ tk[0];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[0] += x2;
	x0 = ppk[0] ^ tk[1];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[1] += x2;
	x0 = ppk[1] ^ tk[2];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[2] += x2;
	x0 = ppk[2] ^ tk[3];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[3] += x2;
	x0 = ppk[3] ^ tk[4];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[4] += x2;
	x0 = ppk[4] ^ tk[5];
	x1 = Sbox[x0 >> 8];
	x2 = Sbox[x0 & 0xFF] ^ ((x1>>8) | (x1<<8));
	ppk[5] += x2;

	x2 = ppk[5] ^ tk[6];
	ppk[0] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[0] ^ tk[7];
	ppk[1] += (x2 >> 1) | (x2 << 15);

	x2 = ppk[1];
	ppk[2] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[2];
	ppk[3] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[3];
	ppk[4] += (x2 >> 1) | (x2 << 15);
	x2 = ppk[4];
	ppk[5] += (x2 >> 1) | (x2 << 15);

	rc4key[0] = tscl >> 8;
	rc4key[1] = (rc4key[0] | 0x20) & 0x7F;
	rc4key[2] = tscl;
	rc4key[3] = (ppk[5] ^ tk[0]) >> 1;
	rc4key[4]  = ppk[0];
	rc4key[5]  = ppk[0] >> 8;
	rc4key[6]  = ppk[1];
	rc4key[7]  = ppk[1] >> 8;
	rc4key[8]  = ppk[2];
	rc4key[9]  = ppk[2] >> 8;
	rc4key[10] = ppk[3];
	rc4key[11] = ppk[3] >> 8;
	rc4key[12] = ppk[4];
	rc4key[13] = ppk[4] >> 8;
	rc4key[14] = ppk[5];
	rc4key[15] = ppk[5] >> 8;
}

typedef struct MICstate MICstate;
struct MICstate
{
	u32int	l;
	u32int	r;
	u32int	m;
	u32int	n;
};

static void
micsetup(MICstate *s, uchar key[8])
{
	s->l =	(u32int)key[0] |
		(u32int)key[1]<<8 |
		(u32int)key[2]<<16 |
		(u32int)key[3]<<24;
	s->r =	(u32int)key[4] |
		(u32int)key[5]<<8 |
		(u32int)key[6]<<16 |
		(u32int)key[7]<<24;
	s->m = 0;
	s->n = 0;
}

static void
micupdate(MICstate *s, uchar *data, ulong len)
{
	u32int l, r, m, n, e;

	l = s->l;
	r = s->r;
	m = s->m;
	n = s->n;
	e = n + len;
	while(n != e){
		m >>= 8;
		m |= (u32int)*data++ << 24;
		if(++n & 3)
			continue;
		l ^= m;
		r ^= (l << 17) | (l >> 15);
		l += r;
		r ^= ((l & 0x00FF00FFUL)<<8) | ((l & 0xFF00FF00UL)>>8);
		l += r;
		r ^= (l << 3) | (l >> 29);
		l += r;
		r ^= (l >> 2) | (l << 30);
		l += r;
	}
	s->l = l;
	s->r = r;
	s->m = m;
	s->n = n;
}

static void
micfinish(MICstate *s, uchar mic[8])
{
	static uchar pad[8] = { 0x5a, 0x00,  0x00,  0x00,  0x00,  0x00,  0x00,  0x00, };

	micupdate(s, pad, sizeof(pad));

	mic[0] = s->l;
	mic[1] = s->l>>8;
	mic[2] = s->l>>16;
	mic[3] = s->l>>24;
	mic[4] = s->r;
	mic[5] = s->r>>8;
	mic[6] = s->r>>16;
	mic[7] = s->r>>24;
}

static uchar pad4[4] = { 0x00, 0x00, 0x00, 0x00, };

static void
tkipencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	u16int tk[8], p1k[5];
	uchar seed[16];
	RC4state rs;
	MICstate ms;
	ulong crc;

	micsetup(&ms, k->key+24);
	micupdate(&ms, dstaddr(w), Eaddrlen);
	micupdate(&ms, srcaddr(w), Eaddrlen);
	micupdate(&ms, pad4, 4);
	micupdate(&ms, b->rp, BLEN(b));
	micfinish(&ms, b->wp);
	b->wp += 8;

	crc = ethercrc(b->rp, BLEN(b));
	crc = ~crc;
	b->wp[0] = crc;
	b->wp[1] = crc>>8;
	b->wp[2] = crc>>16;
	b->wp[3] = crc>>24;
	b->wp += 4;

	tkipk2tk(k->key, tk);
	tkipphase1(tsc >> 16, w->a2, tk, p1k);
	tkipphase2(tsc & 0xFFFF, p1k, tk, seed);
	setupRC4state(&rs, seed, sizeof(seed));
	rc4(&rs, b->rp, BLEN(b));
}

static int
tkipdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar seed[16], mic[8];
	u16int tk[8], p1k[5];
	RC4state rs;
	MICstate ms;
	ulong crc;

	if(BLEN(b) < 8+4)
		return -1;

	tkipk2tk(k->key, tk);
	tkipphase1(tsc >> 16, w->a2, tk, p1k);
	tkipphase2(tsc & 0xFFFF, p1k, tk, seed);
	setupRC4state(&rs, seed, sizeof(seed));
	rc4(&rs, b->rp, BLEN(b));

	b->wp -= 4;
	crc =	(ulong)b->wp[0] |
		(ulong)b->wp[1]<<8 |
		(ulong)b->wp[2]<<16 |
		(ulong)b->wp[3]<<24;
	crc = ~crc;
	crc ^= ethercrc(b->rp, BLEN(b));

	b->wp -= 8;
	micsetup(&ms, k->key+16);
	micupdate(&ms, dstaddr(w), Eaddrlen);
	micupdate(&ms, srcaddr(w), Eaddrlen);
	micupdate(&ms, pad4, 4);
	micupdate(&ms, b->rp, BLEN(b));
	micfinish(&ms, mic);

	return tsmemcmp(b->wp, mic, 8) | crc;
}

static uchar*
putbe(uchar *p, int L, uint v)
{
	while(--L >= 0)
		*p++ = (v >> L*8) & 0xFF;
	return p;
}

static void
xblock(int L, int M, uchar *N, uchar *a, int la, int lm, uchar t[16], AESstate *s)
{
	uchar l[8], *p, *x, *e;

	assert(M >= 4 && M <= 16);
	assert(L >= 2 && L <= 4);

	t[0] = ((la > 0)<<6) | ((M-2)/2)<<3 | (L-1);	/* flags */
	memmove(&t[1], N, 15-L);
	putbe(&t[16-L], L, lm);
	aes_encrypt(s->ekey, s->rounds, t, t);
	
	if(la > 0){
		assert(la < 0xFF00);
		for(p = l, e = putbe(l, 2, la), x = t; p < e; x++, p++)
			*x ^= *p;
		for(e = a + la; a < e; x = t){
			for(; a < e && x < &t[16]; x++, a++)
				*x ^= *a;
			aes_encrypt(s->ekey, s->rounds, t, t);
		}
	}
}

static uchar*
sblock(int L, uchar *N, uint i, uchar b[16], AESstate *s)
{
	b[0] = L-1;	/* flags */
	memmove(&b[1], N, 15-L);
	putbe(&b[16-L], L, i);
	aes_encrypt(s->ekey, s->rounds, b, b);
	return b;
};

static void
aesCCMencrypt(int L, int M, uchar *N /* N[15-L] */,
	uchar *a /* a[la] */, int la,
	uchar *m /* m[lm+M] */, int lm,
	AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);

	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);

		*((u32int*)&t[0]) ^= *((u32int*)&m[0]);
		*((u32int*)&m[0]) ^= *((u32int*)&b[0]);
		*((u32int*)&t[4]) ^= *((u32int*)&m[4]);
		*((u32int*)&m[4]) ^= *((u32int*)&b[4]);
		*((u32int*)&t[8]) ^= *((u32int*)&m[8]);
		*((u32int*)&m[8]) ^= *((u32int*)&b[8]);
		*((u32int*)&t[12]) ^= *((u32int*)&m[12]);
		*((u32int*)&m[12]) ^= *((u32int*)&b[12]);

		aes_encrypt(s->ekey, s->rounds, t, t);
	}
	if(lm > 0){
		for(p = sblock(L, N, i, b, s), x = t; p < &b[lm]; x++, m++, p++){
			*x ^= *m;
			*m ^= *p;
		}
		aes_encrypt(s->ekey, s->rounds, t, t);
	}

	for(p = sblock(L, N, 0, b, s), x = t; p < &b[M]; x++, p++)
		*x ^= *p;

	memmove(m, t, M);
}

static int
aesCCMdecrypt(int L, int M, uchar *N /* N[15-L] */,
	uchar *a /* a[la] */, int la,
	uchar *m /* m[lm+M] */, int lm,
	AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);

	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);

		*((u32int*)&m[0]) ^= *((u32int*)&b[0]);
		*((u32int*)&t[0]) ^= *((u32int*)&m[0]);
		*((u32int*)&m[4]) ^= *((u32int*)&b[4]);
		*((u32int*)&t[4]) ^= *((u32int*)&m[4]);
		*((u32int*)&m[8]) ^= *((u32int*)&b[8]);
		*((u32int*)&t[8]) ^= *((u32int*)&m[8]);
		*((u32int*)&m[12]) ^= *((u32int*)&b[12]);
		*((u32int*)&t[12]) ^= *((u32int*)&m[12]);

		aes_encrypt(s->ekey, s->rounds, t, t);
	}
	if(lm > 0){
		for(p = sblock(L, N, i, b, s), x = t; p < &b[lm]; x++, m++, p++){
			*m ^= *p;
			*x ^= *m;
		}
		aes_encrypt(s->ekey, s->rounds, t, t);
	}

	for(p = sblock(L, N, 0, b, s), x = t; p < &b[M]; x++, p++)
		*x ^= *p;

	return tsmemcmp(m, t, M);
}

static int
setupCCMP(Wifipkt *w, uvlong tsc, uchar nonce[13], uchar auth[32])
{
	uchar *p, *qc;
	int qos;

	/*
	 * 9space: QoS data as OpenBSD's ieee80211_ccmp_phase1 (802.11-2012
	 * 11.4.3.3): its TID in the nonce and after the AAD, the subtype's
	 * QoS bit kept, the order bit masked; 9front masked the whole
	 * subtype and left the QoS control out, which only non-QoS frames
	 * survive
	 */
	qos = (w->fc[0] & 0x8c) == 0x88;
	qc = (uchar*)w + wifihdrlen(w) - 2;
	nonce[0] = ((w->fc[0] & 0x0c) == 0x00) << 4;
	if(qos)
		nonce[0] |= qc[0] & 0x0f;
	memmove(&nonce[1], w->a2, Eaddrlen);
	nonce[7]  = tsc >> 40;
	nonce[8]  = tsc >> 32;
	nonce[9]  = tsc >> 24;
	nonce[10] = tsc >> 16;
	nonce[11] = tsc >> 8;
	nonce[12] = tsc;

	p = auth;
	*p++ = (w->fc[0] & (((w->fc[0] & 0x0c) == 0x08) ? 0x8f : 0xff));
	*p++ = (w->fc[1] & (qos ? ~0xb8 : ~0x38)) | 0x40;
	memmove(p, w->a1, Eaddrlen); p += Eaddrlen;
	memmove(p, w->a2, Eaddrlen); p += Eaddrlen;
	memmove(p, w->a3, Eaddrlen); p += Eaddrlen;
	*p++ = w->seq[0] & 0x0f;
	*p++ = 0;
	if((w->fc[1] & 3) == 0x03) {
		memmove(p, w->a4, Eaddrlen);
		p += Eaddrlen;
	}
	if(qos){
		*p++ = qc[0] & 0x0f;
		*p++ = 0;
	}

	return p - auth;
}

static void
ccmpencrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar auth[32], nonce[13];

	aesCCMencrypt(2, 8, nonce, auth,
		setupCCMP(w, tsc, nonce, auth),
		b->rp, BLEN(b), (AESstate*)k->key);
	b->wp += 8;
}

static int
ccmpdecrypt(Wkey *k, Wifipkt *w, Block *b, uvlong tsc)
{
	uchar auth[32], nonce[13];

	if(BLEN(b) < 8)
		return -1;

	b->wp -= 8;
	return aesCCMdecrypt(2, 8, nonce, auth,
		setupCCMP(w, tsc, nonce, auth),
		b->rp, BLEN(b), (AESstate*)k->key);
}
