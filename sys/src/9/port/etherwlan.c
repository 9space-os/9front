/*
 * nfront: an 802.11 interface whose radio is a program: wifi.c (scanning,
 * authentication, association, its ciphers; aux/wpa for WPA) over a
 * device driven by a user-level driver (aux/rtw89: Linux's rtw89 on the
 * LinuxKPI) or a simulation (tests/wlan).  plan9.ini: etherN=type=wlan
 * [ea=...] [essid=...].  The radio's side, #ω/wlanN:
 *
 *	data	read: a frame wifi.c sends, or a tune request (no frame):
 *		  channel[2] rate[1] flags[1] aid[2] bssid[6] frame...
 *		  (little endian; rate in 500kb/s; flags: Ftune, no frame,
 *		  only the channel); the header tells the radio the
 *		  channel and the access point it is to be on
 *		write: a frame received:
 *		  channel[2] rssi[1] flags[1] tsf[8] frame...
 *		  (rssi in dBm, signed; flags: Ftsf: tsf is the frame's
 *		  timestamp)
 *		One open at a time: the radio.  Frames while there is none
 *		are dropped.
 *	ctl	write: ea 0123456789ab (the station's address, before the
 *		interface is used); caps hex... (the radio's capability
 *		elements: HT, VHT, HE; wifi.c sends those the access point
 *		offers too); read: the state
 *
 * Only the host owner opens them.
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

Wlan wlans[Nwlan];
int nwlan;

/* the rates wifi.c may choose (500 kb/s): 802.11b, then 802.11a/g's OFDM */
static uchar wlanrates[] = {
	0x80 | 2,
	0x80 | 4,
	0x80 | 11,
	0x80 | 22,

	0x80 | 12,
	0x80 | 18,
	0x80 | 24,
	0x80 | 36,
	0x80 | 48,
	0x80 | 72,
	0x80 | 96,
	0x80 | 108,
	0
};

static void
put16(uchar *p, int v)
{
	p[0] = v;
	p[1] = v>>8;
}

/*
 * the rate of a frame: management and group addressed frames the
 * node's lowest, data its current one (wifi.c's rate adaption); without
 * them (scanning) the band's lowest, 1 Mb/s (2.4 GHz) or 6 Mb/s (5 GHz)
 */
static int
txrate(Wnode *wn, Block *b)
{
	Wifipkt *w;
	uchar *r;

	r = wn->actrate;
	if(b != nil && BLEN(b) >= 10){
		w = (Wifipkt*)b->rp;
		if((w->fc[0] & 0x0c) != 0x08 || (w->a1[0] & 1) != 0)
			r = wn->minrate;
	}
	if(r != nil)
		return *r & 0x7f;
	return wn->channel > 14 ? 12 : 2;
}

/* wifi.c's: a frame for the node, or (b nil) tune to its channel */
static void
transmit(Wifi *wifi, Wnode *wn, Block *b)
{
	Wlan *ctlr;
	Block *h;
	int n;

	ctlr = wifi->ether->ctlr;
	if(ctlr->radio.ref == 0){
		ctlr->ndrop++;
		freeb(b);
		return;
	}
	n = b != nil ? BLEN(b) : 0;
	h = allocb(Hdrtx + n);
	put16(h->wp, wn->channel);
	h->wp[2] = txrate(wn, b);
	h->wp[3] = b == nil ? Ftune : 0;
	put16(h->wp+4, wn->aid);
	memmove(h->wp+6, wn->bssid, Eaddrlen);
	h->wp += Hdrtx;
	if(b != nil){
		memmove(h->wp, b->rp, n);
		h->wp += n;
		freeb(b);
	}
	ctlr->ntx++;
	if(qpass(ctlr->txq, h) < 0)
		ctlr->ndrop++;
}

static void
wlanattach(Ether *edev)
{
	Wlan *ctlr;
	int i;

	ctlr = edev->ctlr;
	eqlock(ctlr);
	if(waserror()){
		qunlock(ctlr);
		nexterror();
	}
	if(!ctlr->attached){
		if(ctlr->wifi == nil){
			ctlr->wifi = wifiattach(edev, transmit);
			ctlr->wifi->rates = wlanrates;
			ctlr->wifi->scan = wifiscan5g;	/* 2.4 and 5 GHz */
			memmove(ctlr->wifi->caps, ctlr->caps, ctlr->ncaps);
			ctlr->wifi->ncaps = ctlr->ncaps;
		}
		for(i = 0; i < edev->ISAConf.nopt; i++)
			wificfg(ctlr->wifi, edev->ISAConf.opt[i]);
		ctlr->attached = 1;
	}
	qunlock(ctlr);
	poperror();
}

static long
wlanctl(Ether *edev, void *buf, long n)
{
	Wlan *ctlr;

	ctlr = edev->ctlr;
	if(ctlr->wifi == nil)
		error("not attached");
	return wifictl(ctlr->wifi, buf, n);
}

static char*
wlanifstat(void *a, char *s, char *e)
{
	Ether *edev = a;
	Wlan *ctlr = edev->ctlr;

	if(ctlr->wifi == nil)
		return seprint(s, e, "not attached\n");
	return wifistat(ctlr->wifi, s, e);
}

static void
wlanpromiscuous(void*, int)
{
}

static void
wlanmulticast(void*, uchar*, int)
{
}

static int
wlanpnp(Ether *edev)
{
	Wlan *ctlr;

	/* only an interface plan9.ini names (type=wlan), not every free one devether probes */
	if(edev->ISAConf.type == nil || cistrcmp(edev->ISAConf.type, "wlan") != 0 || nwlan >= Nwlan)
		return -1;
	ctlr = &wlans[nwlan];
	ctlr->txq = qopen(256*1024, Qmsg, 0, 0);
	if(ctlr->txq == nil)
		return -1;
	ctlr->edev = edev;
	nwlan++;
	edev->ctlr = ctlr;
	edev->ISAConf.port = 0;
	edev->ISAConf.irq = -1;
	edev->tbdf = BUSUNKNOWN;
	edev->Netif.arg = edev;
	edev->attach = wlanattach;
	edev->ctl = wlanctl;
	edev->Netif.promiscuous = wlanpromiscuous;
	edev->Netif.multicast = wlanmulticast;
	edev->Netif.ifstat = wlanifstat;
	edev->Netif.mbps = 54;
	return 0;
}

void
etherwlanlink(void)
{
	addethercard("wlan", wlanpnp);
}

