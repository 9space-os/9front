/*
 * tests/wlan: wlansim, the radio of an etherwlan interface (#ω/wlanN,
 * etherwlan.c) and three access points on it:
 *
 *	nfront		02:00:00:00:00:aa	channel 6 (2.4 GHz), open
 *	nfront5		02:00:00:00:00:bb	channel 36 (5 GHz), open; its
 *					beacons have no DS parameter set,
 *					the HT operation element says the
 *					channel; WMM and HT
 *	nfrontwpa	02:00:00:00:00:cc	channel 100 (5 GHz, radar
 *					detection: the station only listens),
 *					WPA2-PSK (CCMP), passphrase
 *					nfront-secret; its beacons tell no
 *					channel at all; WMM and HT
 *
 * Each sends beacons while the interface is tuned to its channel and
 * answers probe requests, authentication and association; a WPA2 one
 * then runs the authenticator's side of the 4-way handshake (PTK from
 * the PMK, MIC with HMAC-SHA1 (key descriptor version 2), the GTK
 * wrapped with AES key wrap) and ciphers its data frames with CCMP.
 * Behind them is a host, 10.0.9.1 (ethernet 02:00:00:00:00:01): ARP and
 * ICMP echo.  Prints what happens on stderr, and what is wrong: probe
 * requests on a channel with radar detection, 802.11b rates on 5 GHz.
 * As the radio it offers HT (ctl caps); an access point with WMM sees
 * whether the association request has WMM's element and the HT
 * capabilities, and then exchanges QoS data (CCMP with the TID).  Then it
 * asks for a block ack agreement (A-MPDU, TID 0) and agrees to the
 * station's; under its agreement each echo reply comes with sequence
 * number s+1 before a frame with s (an ARP reply, an older CCMP packet
 * number: only wifi.c's reordering lets it through), every third one
 * without s at all (the wait must expire), the first as an A-MSDU.
 *
 * The CCMP code (aesCCMencrypt, aesCCMdecrypt, setupCCMP) is adapted
 * from 9front's /sys/src/9/port/wifi.c, the PRF from factotum's
 * wpapsk.c (/sys/src/cmd/auth/factotum).
 *
 * usage: wlansim [-d] [#ω/wlan1]
 */
#include <u.h>
#include <libc.h>
#include <ip.h>
#include <libsec.h>

enum {
	Hdrtx	= 12,
	Hdrrx	= 12,
	Ftune	= 1,
	Nframe	= 4096,

	/* EAPOL-Key key information */
	Kvers2	= 2,	/* HMAC-SHA1 MIC, AES key wrap */
	Fptk	= 1<<3,
	Fins	= 1<<6,
	Fack	= 1<<7,
	Fmic	= 1<<8,
	Fsec	= 1<<9,
	Fenc	= 1<<12,

	/* offsets in the key descriptor */
	Kinfo	= 1,
	Klen	= 3,
	Krepc	= 5,
	Knonce	= 13,
	Kmic	= 77,
	Kdatalen	= 93,
	Kdata	= 95,

	/* the handshake */
	Hnone	= 0,
	Hm1,		/* message 1 sent */
	Hm3,		/* message 3 sent */
	Hdone,		/* keys installed */
};

typedef struct Ap Ap;
struct Ap {
	char	*essid;
	uchar	bssid[6];
	int	channel;
	int	ht;	/* HT operation element, no DS parameter set */
	int	wpa;
	int	wmm;	/* WMM and HT capabilities in the beacon */
	uchar	*rates;
	int	nrates;

	/* the station's */
	int	assoc;
	int	qos;	/* it associated with WMM: QoS data */
	int	ba;	/* the station agreed to our block ack agreement */
	int	qseq;	/* the QoS data's sequence numbers (TID 0) */
	int	nreply;
	int	hs;
	int	tries;
	vlong	sent;	/* ms */
	uvlong	repc;
	uchar	anonce[32];
	uchar	pmk[32];
	uchar	ptk[64];	/* KCK[16] KEK[16] TK[16] */
	uchar	gtk[16];
	AESstate	tk;
	uvlong	txpn;
	uvlong	rxpn;
};

static uchar rates24[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c };
static uchar rates5[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };

static Ap aps[] = {
	{ "nfront",	{ 0x02, 0, 0, 0, 0, 0xaa },	6,	0,	0,	0,	rates24,	sizeof rates24 },
	{ "nfront5",	{ 0x02, 0, 0, 0, 0, 0xbb },	36,	1,	0,	1,	rates5,	sizeof rates5 },
	{ "nfrontwpa",	{ 0x02, 0, 0, 0, 0, 0xcc },	100,	0,	1,	1,	rates5,	sizeof rates5 },
};
static char passphrase[] = "nfront-secret";
/* HT capabilities: 20 MHz, MCS 0-7 (the access point's and the radio's) */
static uchar htcap[] = {
	45, 26,
	0x00, 0x00,		/* capability info */
	0x00,			/* A-MPDU */
	0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* MCS set */
	0, 0,			/* extended */
	0, 0, 0, 0,		/* beamforming */
	0,			/* ASEL */
};
/* WMM parameter element, version 1, the default access categories */
static uchar wmmparam[] = {
	221, 24, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x00, 0x00,
	0x03, 0xa4, 0x00, 0x00,	/* AC_BE */
	0x27, 0xa4, 0x00, 0x00,	/* AC_BK */
	0x42, 0x43, 0x5e, 0x00,	/* AC_VI */
	0x62, 0x32, 0x2f, 0x00,	/* AC_VO */
};

/* the AP's RSN element: WPA2, CCMP, PSK */
static uchar rsnie[] = {
	0x30, 0x14,
	0x01, 0x00,
	0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
	0x00, 0x00,
};

static uchar host[6] = { 0x02, 0, 0, 0, 0, 0x01 };
static uchar hostip[4] = { 10, 0, 9, 1 };
static uchar bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static uchar snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

static int fd;
static int debug;
static int tuned;
static int seq;
static uchar sta[6];
static Lock lk;
static int badrate, dfsprobe, badqos, sawqos;

static void
put16(uchar *p, int v)
{
	p[0] = v;
	p[1] = v>>8;
}

static int
get16(uchar *p)
{
	return p[0] | p[1]<<8;
}

static void
putbe64(uchar *p, uvlong v)
{
	int i;

	for(i = 7; i >= 0; i--, v >>= 8)
		p[i] = v;
}

static int
is11b(int r)
{
	r &= 0x7f;
	return r == 2 || r == 4 || r == 11 || r == 22;
}

static Ap*
apbybssid(uchar *a)
{
	int i;

	for(i = 0; i < nelem(aps); i++)
		if(memcmp(aps[i].bssid, a, 6) == 0)
			return &aps[i];
	return nil;
}

/* a frame to the station, received on the AP's channel */
static void
send(Ap *ap, uchar *f, int n)
{
	uchar b[Hdrrx+Nframe];

	if(n > Nframe)
		return;
	memset(b, 0, Hdrrx);
	put16(b, ap->channel);
	b[2] = (uchar)-40;	/* rssi */
	memmove(b+Hdrrx, f, n);
	if(write(fd, b, Hdrrx+n) != Hdrrx+n)
		fprint(2, "wlansim: write: %r\n");
}

/* a management frame's header */
static uchar*
mgmt(Ap *ap, uchar *f, int sub, uchar *da)
{
	memset(f, 0, 24);
	f[0] = sub<<4;
	memmove(f+4, da, 6);
	memmove(f+10, ap->bssid, 6);
	memmove(f+16, ap->bssid, 6);
	put16(f+22, (seq++ & 0xfff) << 4);
	return f+24;
}

static uchar*
putrates(Ap *ap, uchar *p)
{
	int n;

	n = ap->nrates > 8 ? 8 : ap->nrates;
	*p++ = 1;
	*p++ = n;
	memmove(p, ap->rates, n);
	p += n;
	if(ap->nrates > 8){
		*p++ = 50;
		*p++ = ap->nrates - 8;
		memmove(p, ap->rates + 8, ap->nrates - 8);
		p += ap->nrates - 8;
	}
	return p;
}

static uchar*
ies(Ap *ap, uchar *p)
{
	int n;

	n = strlen(ap->essid);
	*p++ = 0;
	*p++ = n;
	memmove(p, ap->essid, n);
	p += n;
	p = putrates(ap, p);
	if(ap->channel <= 14){
		*p++ = 3;	/* DS parameter set */
		*p++ = 1;
		*p++ = ap->channel;
	}
	if(ap->ht){
		*p++ = 61;	/* HT operation */
		*p++ = 22;
		memset(p, 0, 22);
		p[0] = ap->channel;	/* primary channel */
		p += 22;
	}
	if(ap->wpa){
		memmove(p, rsnie, sizeof rsnie);
		p += sizeof rsnie;
	}
	if(ap->wmm){
		memmove(p, htcap, sizeof htcap);
		p += sizeof htcap;
		memmove(p, wmmparam, sizeof wmmparam);
		p += sizeof wmmparam;
	}
	return p;
}

/* a beacon (sub 8) or probe response (sub 5) */
static void
beacon(Ap *ap, int sub, uchar *da)
{
	uchar f[512], *p;

	p = mgmt(ap, f, sub, da);
	memset(p, 0, 8);	/* timestamp */
	p += 8;
	put16(p, 100);		/* interval */
	put16(p+2, 0x0001 | (ap->wpa ? 0x0010 : 0));	/* ESS, privacy */
	p = ies(ap, p+4);
	send(ap, f, p - f);
}

/*
 * CCMP, from 9front's /sys/src/9/port/wifi.c
 */
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
}

static void
aesCCMencrypt(int L, int M, uchar *N, uchar *a, int la, uchar *m, int lm, AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);
	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);
		for(x = t, p = b; x < &t[16]; x++, p++, m++){
			*x ^= *m;
			*m ^= *p;
		}
		m -= 16;
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
aesCCMdecrypt(int L, int M, uchar *N, uchar *a, int la, uchar *m, int lm, AESstate *s)
{
	uchar t[16], b[16], *p, *x;
	uint i;

	xblock(L, M, N, a, la, lm, t, s);
	for(i = 1; lm >= 16; i++, m += 16, lm -= 16){
		sblock(L, N, i, b, s);
		for(x = t, p = b; x < &t[16]; x++, p++, m++){
			*m ^= *p;
			*x ^= *m;
		}
		m -= 16;
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

/*
 * f: the header of a data frame (no a4), with QoS control (26 bytes) if
 * QoS data: then its TID in the nonce and the AAD, as wifi.c (nfront)
 * and OpenBSD's ieee80211_ccmp_phase1 do
 */
static int
setupCCMP(uchar *f, uvlong tsc, uchar nonce[13], uchar auth[32])
{
	uchar *p;
	int qos;

	qos = (f[0] & 0x8c) == 0x88;
	nonce[0] = ((f[0] & 0x0c) == 0x00) << 4;
	if(qos)
		nonce[0] |= f[24] & 0x0f;
	memmove(&nonce[1], f+10, 6);	/* a2 */
	nonce[7]  = tsc >> 40;
	nonce[8]  = tsc >> 32;
	nonce[9]  = tsc >> 24;
	nonce[10] = tsc >> 16;
	nonce[11] = tsc >> 8;
	nonce[12] = tsc;

	p = auth;
	*p++ = (f[0] & (((f[0] & 0x0c) == 0x08) ? 0x8f : 0xff));
	*p++ = (f[1] & (qos ? ~0xb8 : ~0x38)) | 0x40;
	memmove(p, f+4, 18);	/* a1, a2, a3 */
	p += 18;
	*p++ = f[22] & 0x0f;
	*p++ = 0;
	if(qos){
		*p++ = f[24] & 0x0f;
		*p++ = 0;
	}
	return p - auth;
}

/*
 * the keys: PMK = PBKDF2-SHA1(passphrase, ssid, 4096), PTK = PRF-512 (as
 * factotum's wpapsk.c), the key wrap of RFC 3394 (aux/wpa unwraps)
 */
static void
prf(uchar *k, int klen, char *a, uchar *b, int blen, uchar *d, int dlen)
{
	uchar r[SHA1dlen], i;
	DigestState *ds;
	int n;

	i = 0;
	while(dlen > 0){
		ds = hmac_sha1((uchar*)a, strlen(a)+1, k, klen, nil, nil);
		hmac_sha1(b, blen, k, klen, nil, ds);
		hmac_sha1(&i, 1, k, klen, r, ds);
		i++;
		n = dlen > sizeof r ? sizeof r : dlen;
		memmove(d, r, n);
		d += n;
		dlen -= n;
	}
}

static void
calcptk(Ap *ap, uchar *snonce)
{
	uchar b[2*6 + 2*32];

	if(memcmp(sta, ap->bssid, 6) > 0){
		memmove(b, ap->bssid, 6);
		memmove(b+6, sta, 6);
	} else {
		memmove(b, sta, 6);
		memmove(b+6, ap->bssid, 6);
	}
	if(memcmp(snonce, ap->anonce, 32) > 0){
		memmove(b+12, ap->anonce, 32);
		memmove(b+44, snonce, 32);
	} else {
		memmove(b+12, snonce, 32);
		memmove(b+44, ap->anonce, 32);
	}
	prf(ap->pmk, 32, "Pairwise key expansion", b, sizeof b, ap->ptk, sizeof ap->ptk);
}

static int
aeswrap(uchar *kek, uchar *data, int n, uchar *out)
{
	uchar B[16], *A, *R;
	AESstate s;
	int i, j, m;
	uint t;

	if(n < 16 || n % 8)
		return -1;
	m = n/8;
	setupAESstate(&s, kek, 16, nil);
	A = out;
	memset(A, 0xa6, 8);
	memmove(out+8, data, n);
	for(j = 0; j < 6; j++)
		for(i = 1; i <= m; i++){
			R = out + 8*i;
			memmove(B, A, 8);
			memmove(B+8, R, 8);
			aes_encrypt(s.ekey, s.rounds, B, B);
			t = m*j + i;
			memmove(A, B, 8);
			A[7] ^= t;
			A[6] ^= t>>8;
			A[5] ^= t>>16;
			A[4] ^= t>>24;
			memmove(R, B+8, 8);
		}
	return n + 8;
}

/*
 * a data frame from the AP (src: the host behind it or the AP) to the
 * station into f, QoS data numbered sq (< 0: the next); its length
 */
static int amsdunext;	/* the next frame an A-MSDU of one subframe */

static int
mkdata(Ap *ap, uchar *f, uchar *src, int type, uchar *pl, int n, int sq)
{
	int am;

	uchar nonce[13], auth[32], *p;
	uvlong pn;

	if(26 + 8 + 8 + n + 8 > Nframe)
		return -1;
	memset(f, 0, 26);
	f[0] = ap->qos ? 0x88 : 0x08;	/* QoS data with WMM */
	f[1] = 0x02;	/* from DS */
	memmove(f+4, sta, 6);
	memmove(f+10, ap->bssid, 6);
	memmove(f+16, src, 6);
	if(ap->qos)
		put16(f+22, ((sq >= 0 ? sq : ap->qseq++) & 0xfff) << 4);
	else
		put16(f+22, (seq++ & 0xfff) << 4);
	p = f+24;
	am = ap->qos && amsdunext;
	amsdunext = 0;
	if(ap->qos){
		p[0] = am ? 0x80 : 0;	/* TID 0; A-MSDU present */
		p[1] = 0;
		p += 2;
	}
	if(ap->wpa && ap->hs == Hdone){
		pn = ++ap->txpn;
		p[0] = pn;
		p[1] = pn>>8;
		p[2] = 0;
		p[3] = 0<<6 | 0x20;	/* key id 0, extended iv */
		p[4] = pn>>16;
		p[5] = pn>>24;
		p[6] = pn>>32;
		p[7] = pn>>40;
		p += 8;
	}
	if(am){
		/* one subframe: DA, SA, length, the MSDU with its LLC/SNAP */
		memmove(p, sta, 6);
		memmove(p+6, src, 6);
		p[12] = (n+8)>>8;
		p[13] = n+8;
		memmove(p+14, snap, 6);
		p[20] = type>>8;
		p[21] = type;
		memmove(p+22, pl, n);
		n += 22;
	} else {
		memmove(p, snap, 6);
		p[6] = type>>8;
		p[7] = type;
		memmove(p+8, pl, n);
		n += 8;
	}
	if(ap->wpa && ap->hs == Hdone){
		aesCCMencrypt(2, 8, nonce, auth, setupCCMP(f, pn, nonce, auth), p, n, &ap->tk);
		f[1] |= 0x40;	/* protected */
		n += 8;
	}
	return p + n - f;
}

static void
data(Ap *ap, uchar *src, int type, uchar *pl, int n)
{
	uchar f[Nframe];

	if((n = mkdata(ap, f, src, type, pl, n, -1)) > 0)
		send(ap, f, n);
}

/* a block ack action frame (category 3) to the station */
static void
sendba(Ap *ap, uchar *body, int n)
{
	uchar f[64], *p;

	p = mgmt(ap, f, 13, sta);
	memmove(p, body, n);
	send(ap, f, p + n - f);
}

/* our ADDBA request: TID 0, buffer size 32, from the next sequence number */
static void
addba(Ap *ap)
{
	uchar r[9];
	int param;

	param = 2 | 0<<2 | 32<<6;
	r[0] = 3;
	r[1] = 0;
	r[2] = 7;	/* dialog token */
	put16(r+3, param);
	put16(r+5, 0);
	put16(r+7, ap->qseq << 4);
	sendba(ap, r, 9);
}

/* the station's block ack action frames */
static void
fromba(Ap *ap, uchar *d, int n)
{
	uchar r[9];
	int param;

	if(n < 2 || d[0] != 3)
		return;
	switch(d[1]){
	case 0:	/* its ADDBA request: agreed */
		if(n < 9)
			return;
		param = get16(d+3);
		fprint(2, "wlansim: %s: addba request from %E: tid %d size %d\n", ap->essid, sta, param>>2 & 15, param>>6);
		r[0] = 3;
		r[1] = 1;
		r[2] = d[2];
		put16(r+3, 0);
		put16(r+5, param & ~1);
		put16(r+7, get16(d+5));
		sendba(ap, r, 9);
		break;
	case 1:	/* its answer to ours */
		if(n < 9)
			return;
		param = get16(d+5);
		ap->ba = get16(d+3) == 0;
		fprint(2, "wlansim: %s: addba response from %E: status %d tid %d size %d%s\n", ap->essid, sta,
			get16(d+3), param>>2 & 15, param>>6, param & 1 ? " amsdu" : "");
		break;
	}
}

/*
 * an echo reply under our block ack agreement: s+1 first, then (but every
 * third time) an ARP reply as s, built first: its CCMP packet number the
 * older; the station's reordering must deliver both
 */
static void
reorderedreply(Ap *ap, uchar *r, int n, uchar *arp)
{
	uchar fa[Nframe], fb[Nframe];
	int na, nb, s;

	s = ap->qseq;
	ap->qseq += 2;
	na = mkdata(ap, fa, host, 0x0806, arp, 28, s);
	amsdunext = ap->nreply == 0;	/* the first echo reply as an A-MSDU */
	if(amsdunext)
		fprint(2, "wlansim: %s: an echo reply as an A-MSDU\n", ap->essid);
	nb = mkdata(ap, fb, host, 0x0800, r, n, s+1);
	if(na < 0 || nb < 0)
		return;
	send(ap, fb, nb);
	if(++ap->nreply % 3 != 0)
		send(ap, fa, na);
}

/* an EAPOL-Key frame of the handshake to the station */
static void
eapolkey(Ap *ap, int info, uchar *kdata, int nkdata)
{
	uchar f[512], digest[SHA1dlen], *k;
	int n;

	n = 4 + Kdata + nkdata;
	if(n > sizeof f)
		return;
	memset(f, 0, n);
	f[0] = 2;	/* 802.1X-2004 */
	f[1] = 3;	/* key */
	f[2] = (n-4)>>8;
	f[3] = n-4;
	k = f+4;
	k[0] = 2;	/* RSN key descriptor */
	k[Kinfo] = info>>8;
	k[Kinfo+1] = info;
	k[Klen+1] = 16;
	putbe64(k+Krepc, ++ap->repc);
	memmove(k+Knonce, ap->anonce, 32);
	k[Kdatalen] = nkdata>>8;
	k[Kdatalen+1] = nkdata;
	memmove(k+Kdata, kdata, nkdata);
	if(info & Fmic){
		hmac_sha1(f, n, ap->ptk, 16, digest, nil);
		memmove(k+Kmic, digest, 16);
	}
	ap->sent = nsec()/1000000;
	data(ap, ap->bssid, 0x888e, f, n);
}

static void
message1(Ap *ap)
{
	eapolkey(ap, Kvers2|Fptk|Fack, nil, 0);
	ap->hs = Hm1;
	fprint(2, "wlansim: %s: 4-way handshake: message 1 to %E\n", ap->essid, sta);
}

static void
message3(Ap *ap)
{
	uchar kd[128], wrapped[136], *p;
	int n;

	/* the AP's RSN element, the GTK's KDE (key id 1), padding */
	p = kd;
	memmove(p, rsnie, sizeof rsnie);
	p += sizeof rsnie;
	*p++ = 0xdd;
	*p++ = 6 + sizeof ap->gtk;
	memmove(p, "\x00\x0f\xac\x01", 4);
	p += 4;
	*p++ = 1;	/* key id 1 */
	*p++ = 0;
	memmove(p, ap->gtk, sizeof ap->gtk);
	p += sizeof ap->gtk;
	if((p - kd) % 8 != 0){
		*p++ = 0xdd;
		while((p - kd) % 8 != 0)
			*p++ = 0;
	}
	n = aeswrap(ap->ptk+16, kd, p - kd, wrapped);
	eapolkey(ap, Kvers2|Fptk|Fins|Fack|Fmic|Fsec|Fenc, wrapped, n);
	ap->hs = Hm3;
	fprint(2, "wlansim: %s: 4-way handshake: message 3 to %E\n", ap->essid, sta);
}

/* an EAPOL frame from the station (f: the EAPOL header) */
static void
fromeapol(Ap *ap, uchar *f, int n)
{
	uchar mic[16], digest[SHA1dlen], *k;
	int info, len;

	if(!ap->wpa || n < 4 + Kdata || f[1] != 3)
		return;
	len = f[2]<<8 | f[3];
	if(len < Kdata || 4 + len > n)
		return;
	n = 4 + len;
	k = f+4;
	info = k[Kinfo]<<8 | k[Kinfo+1];
	if((info & Fmic) == 0 || (info & 7) != Kvers2){
		fprint(2, "wlansim: %s: eapol key info %#x?\n", ap->essid, info);
		return;
	}
	if(ap->hs == Hm1){
		/* message 2: the station's nonce, its RSN element */
		calcptk(ap, k+Knonce);
	}
	memmove(mic, k+Kmic, 16);
	memset(k+Kmic, 0, 16);
	hmac_sha1(f, n, ap->ptk, 16, digest, nil);
	if(tsmemcmp(mic, digest, 16) != 0){
		fprint(2, "wlansim: %s: 4-way handshake: bad mic from %E\n", ap->essid, sta);
		return;
	}
	if(ap->hs == Hm1 && (info & Fsec) == 0){
		fprint(2, "wlansim: %s: 4-way handshake: message 2 from %E, mic ok\n", ap->essid, sta);
		setupAESstate(&ap->tk, ap->ptk+32, 16, nil);
		ap->tries = 0;
		message3(ap);
		return;
	}
	if(ap->hs == Hm3 && (info & Fsec) != 0){
		fprint(2, "wlansim: %s: 4-way handshake: message 4 from %E, mic ok: keys installed\n", ap->essid, sta);
		ap->hs = Hdone;
		if(ap->qos)
			addba(ap);
		ap->txpn = 0;
		ap->rxpn = 0;
	}
}

static int
cksum(uchar *p, int n)
{
	ulong s;
	int i;

	s = 0;
	for(i = 0; i+1 < n; i += 2)
		s += p[i]<<8 | p[i+1];
	if(n & 1)
		s += p[n-1]<<8;
	while(s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return ~s & 0xffff;
}

/* what the station sends the host: ARP for hostip, ICMP echo to it */
static void
fromsta(Ap *ap, int type, uchar *p, int n, int prot)
{
	uchar r[1600];
	int hl, c;

	if(type == 0x888e){
		fromeapol(ap, p, n);
		return;
	}
	if(ap->wpa && ap->hs != Hdone)
		return;	/* the port is blocked */
	if(ap->wpa && !prot){
		fprint(2, "wlansim: %s: data in the clear from %E\n", ap->essid, sta);
		return;
	}
	if(type == 0x0806 && n >= 28 && get16(p+6) == 0x0100 && memcmp(p+24, hostip, 4) == 0){
		memmove(r, p, 28);
		r[7] = 2;	/* reply */
		memmove(r+8, host, 6);
		memmove(r+14, hostip, 4);
		memmove(r+18, p+8, 10);	/* to the asker */
		if(debug)
			fprint(2, "wlansim: arp: %V is %E\n", hostip, host);
		data(ap, host, 0x0806, r, 28);
		return;
	}
	if(type == 0x0800 && n >= 20 && p[9] == 1 && memcmp(p+16, hostip, 4) == 0){
		hl = (p[0] & 15) * 4;
		if(n < hl + 8 || p[hl] != 8)
			return;
		if(n > sizeof r)
			return;
		memmove(r, p, n);
		memmove(r+12, p+16, 4);
		memmove(r+16, p+12, 4);
		r[10] = r[11] = 0;
		c = cksum(r, hl);
		r[10] = c>>8;
		r[11] = c;
		r[hl] = 0;	/* echo reply */
		r[hl+2] = r[hl+3] = 0;
		c = cksum(r+hl, n-hl);
		r[hl+2] = c>>8;
		r[hl+3] = c;
		fprint(2, "wlansim: %s: echo from %V%s\n", ap->essid, p+12, prot ? " (ccmp)" : "");
		if(ap->ba){
			/* an ARP reply to the station: the host's address once more */
			uchar arp[28];

			memmove(arp, "\x00\x01\x08\x00\x06\x04\x00\x02", 8);
			memmove(arp+8, host, 6);
			memmove(arp+14, hostip, 4);
			memmove(arp+18, sta, 6);
			memmove(arp+24, p+12, 4);
			reorderedreply(ap, r, n, arp);
		} else
			data(ap, host, 0x0800, r, n);
	}
}

/* the rates of an association request: none of 802.11b on 5 GHz */
static void
checkrates(Ap *ap, uchar *p, uchar *e)
{
	uchar *x;
	int i;

	for(; p+2 <= e; p = x){
		x = p+2+p[1];
		if(x > e)
			break;
		if(p[0] != 1 && p[0] != 50)
			continue;
		for(i = 2; p+i < x; i++)
			if(ap->channel > 14 && is11b(p[i])){
				fprint(2, "wlansim: %s: 802.11b rate %d in the association request\n", ap->essid, p[i] & 0x7f);
				return;
			}
	}
}

static int
hasie(uchar *p, uchar *e, int id)
{
	for(; p+2 <= e && p+2+p[1] <= e; p += 2+p[1])
		if(p[0] == id)
			return 1;
	return 0;
}

static int
hasrsn(uchar *p, uchar *e)
{
	return hasie(p, e, 48);
}

/* WMM's information element (00:50:f2, type 2, subtype 0) */
static int
haswmm(uchar *p, uchar *e)
{
	for(; p+2 <= e && p+2+p[1] <= e; p += 2+p[1])
		if(p[0] == 221 && p[1] >= 7 && memcmp(p+2, "\x00\x50\xf2\x02\x00", 5) == 0)
			return 1;
	return 0;
}

/* a protected data frame from the station: decrypt it in place */
static int
unprotect(Ap *ap, uchar *f, int n)
{
	uchar nonce[13], auth[32], *p;
	uvlong pn;
	int h;

	h = (f[0] & 0x80) ? 26 : 24;
	if(ap->hs != Hdone || n < h+8+8)
		return -1;
	p = f+h;
	pn = (uvlong)p[7]<<40 | (uvlong)p[6]<<32 | (uvlong)p[5]<<24 |
		(uvlong)p[4]<<16 | (uvlong)p[1]<<8 | (uvlong)p[0];
	if(pn <= ap->rxpn){
		fprint(2, "wlansim: %s: replayed pn %llud\n", ap->essid, pn);
		return -1;
	}
	if(aesCCMdecrypt(2, 8, nonce, auth, setupCCMP(f, pn, nonce, auth), p+8, n-h-8-8, &ap->tk) != 0){
		fprint(2, "wlansim: %s: ccmp: bad mic\n", ap->essid);
		return -1;
	}
	ap->rxpn = pn;
	memmove(p, p+8, n-h-8-8);
	return n-8-8;
}

static void
frame(uchar *f, int n)
{
	uchar r[512], *p;
	int i, type, sub, prot, h;
	Ap *ap;

	if(n < 24)
		return;
	type = f[0]>>2 & 3;
	sub = f[0]>>4;
	if(type == 0 && sub == 4){	/* probe request */
		if(tuned >= 52 && tuned <= 144 && !dfsprobe){
			dfsprobe = 1;
			fprint(2, "wlansim: probe request on channel %d (radar detection)\n", tuned);
		}
		for(i = 0; i < nelem(aps); i++){
			ap = &aps[i];
			if(ap->channel != tuned)
				continue;
			if(debug)
				fprint(2, "wlansim: %s: probe from %E\n", ap->essid, f+10);
			beacon(ap, 5, f+10);
		}
		return;
	}
	if((ap = apbybssid(f+4)) == nil)
		return;
	if(type == 0){
		switch(sub){
		case 11:	/* authentication */
			memmove(sta, f+10, 6);
			fprint(2, "wlansim: %s: auth from %E\n", ap->essid, sta);
			p = mgmt(ap, r, 11, sta);
			put16(p, 0);	/* open system */
			put16(p+2, 2);	/* sequence */
			put16(p+4, 0);	/* success */
			send(ap, r, p+6 - r);
			return;
		case 0:	/* association request */
			memmove(sta, f+10, 6);
			checkrates(ap, f+24+4, f+n);
			p = mgmt(ap, r, 1, sta);
			put16(p, 0x0001);
			if(ap->wpa && !hasrsn(f+24+4, f+n)){
				fprint(2, "wlansim: %s: association without RSN element refused\n", ap->essid);
				put16(p+2, 1);	/* unspecified failure */
				put16(p+4, 0);
				send(ap, r, p+6 - r);
				return;
			}
			ap->qos = ap->wmm && haswmm(f+24+4, f+n);
			fprint(2, "wlansim: %s: associated %E on channel %d%s%s\n", ap->essid, sta, ap->channel,
				ap->qos ? ", with WMM" : "", ap->wmm && hasie(f+24+4, f+n, 45) ? ", HT capabilities" : "");
			put16(p+2, 0);	/* success */
			put16(p+4, 0xc001);	/* aid 1 */
			p = putrates(ap, p+6);
			send(ap, r, p - r);
			ap->assoc = 1;
			ap->hs = Hnone;
			ap->ba = 0;
			if(ap->qos && !ap->wpa)
				addba(ap);
			if(ap->wpa){
				genrandom(ap->anonce, sizeof ap->anonce);
				genrandom(ap->gtk, sizeof ap->gtk);
				ap->tries = 0;
				message1(ap);
			}
			return;
		case 10:	/* disassociation */
		case 12:	/* deauthentication */
			ap->assoc = 0;
			ap->hs = Hnone;
			ap->ba = 0;
			return;
		case 13:	/* action */
			fromba(ap, f+24, n-24);
			return;
		}
		return;
	}
	if(type != 2 || (f[1] & 3) != 1)
		return;
	h = (f[0] & 0x80) ? 26 : 24;	/* QoS data: its control after the header */
	if(ap->qos != (h == 26) && !badqos){
		badqos = 1;
		fprint(2, "wlansim: %s: wrong: %s data from %E (WMM %d)\n", ap->essid, h == 26 ? "QoS" : "non-QoS", sta, ap->qos);
	}
	if(h == 26 && ap->qos && !sawqos){
		sawqos = 1;
		fprint(2, "wlansim: %s: QoS data from %E, TID %d\n", ap->essid, sta, f[24] & 0x0f);
	}
	prot = (f[1] & 0x40) != 0;
	if(prot && (n = unprotect(ap, f, n)) < 0)
		return;
	if(n >= h+8 && memcmp(f+h, snap, 6) == 0)
		fromsta(ap, f[h+6]<<8 | f[h+7], f+h+8, n-h-8, prot);
}

/* beacons; the handshake's messages again while unanswered */
static void
beaconproc(void)
{
	vlong now;
	int i;
	Ap *ap;

	for(;;){
		lock(&lk);
		now = nsec()/1000000;
		for(i = 0; i < nelem(aps); i++){
			ap = &aps[i];
			if(tuned == ap->channel)
				beacon(ap, 8, bcast);
			if((ap->hs == Hm1 || ap->hs == Hm3) && now - ap->sent >= 1000){
				if(++ap->tries > 30){
					ap->hs = Hnone;
					fprint(2, "wlansim: %s: 4-way handshake: no answer\n", ap->essid);
				} else if(ap->hs == Hm1)
					message1(ap);
				else
					message3(ap);
			}
		}
		unlock(&lk);
		sleep(100);
	}
}

static void
usage(void)
{
	fprint(2, "usage: wlansim [-d] [#ω/wlanN]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	uchar b[Hdrtx+Nframe];
	char *dev;
	int i, n;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND;
	dev = argc > 0 ? argv[0] : "#ω/wlan1";
	fmtinstall('E', eipfmt);
	fmtinstall('V', eipfmt);
	fmtinstall('H', encodefmt);
	for(i = 0; i < nelem(aps); i++)
		if(aps[i].wpa)
			pbkdf2_x((uchar*)passphrase, strlen(passphrase),
				(uchar*)aps[i].essid, strlen(aps[i].essid), 4096,
				aps[i].pmk, sizeof aps[i].pmk, hmac_sha1, SHA1dlen);
	if((fd = open(smprint("%s/data", dev), ORDWR)) < 0)
		sysfatal("%s: %r", dev);
	/* the radio's capabilities: HT, as aux/rtw89 tells its own */
	if((i = open(smprint("%s/ctl", dev), OWRITE)) >= 0){
		fprint(i, "caps %.*H", (int)sizeof htcap, htcap);
		close(i);
	}
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		beaconproc();
		exits(nil);
	}
	for(;;){
		if((n = read(fd, b, sizeof b)) <= 0)
			sysfatal("read: %r");
		if(n < Hdrtx)
			continue;
		lock(&lk);
		tuned = get16(b);
		if(debug > 1)
			fprint(2, "wlansim: tx channel %d rate %d flags %#x %d bytes\n", tuned, b[2], b[3], n-Hdrtx);
		if((b[3] & Ftune) == 0){
			if(tuned > 14 && is11b(b[2]) && !badrate){
				badrate = 1;
				fprint(2, "wlansim: frame at 802.11b rate %d on channel %d\n", b[2], tuned);
			}
			frame(b+Hdrtx, n-Hdrtx);
		}
		unlock(&lk);
	}
}
