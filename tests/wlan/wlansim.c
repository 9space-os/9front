/*
 * tests/wlan: wlansim, the radio of an etherwlan interface (#ω/wlanN,
 * etherwlan.c) and an open access point on it: essid nfront, bssid
 * 02:00:00:00:00:aa, channel 6.  It sends beacons while the interface is
 * tuned to channel 6, answers probe requests, authentication and
 * association, and in the data frames ARP and ICMP echo for 10.0.9.1
 * (ethernet 02:00:00:00:00:01, behind the access point).  Prints what
 * happens on stderr.
 *
 * usage: wlansim [-d] [#ω/wlan1]
 */
#include <u.h>
#include <libc.h>
#include <ip.h>

enum {
	Hdrtx	= 12,
	Hdrrx	= 12,
	Ftune	= 1,
	Channel	= 6,
	Nframe	= 4096,
};

static uchar bssid[6] = { 0x02, 0, 0, 0, 0, 0xaa };
static uchar host[6] = { 0x02, 0, 0, 0, 0, 0x01 };
static uchar hostip[4] = { 10, 0, 9, 1 };
static uchar bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static char essid[] = "nfront";
static uchar rates[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };

static int fd;
static int debug;
static int tuned;
static int seq;
static uchar sta[6];

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

/* a frame to the station */
static void
send(uchar *f, int n)
{
	uchar b[Hdrrx+Nframe];

	if(n > Nframe)
		return;
	memset(b, 0, Hdrrx);
	put16(b, Channel);
	b[2] = (uchar)-40;	/* rssi */
	memmove(b+Hdrrx, f, n);
	if(write(fd, b, Hdrrx+n) != Hdrrx+n)
		fprint(2, "wlansim: write: %r\n");
}

/* a management frame's header */
static uchar*
mgmt(uchar *f, int sub, uchar *da)
{
	memset(f, 0, 24);
	f[0] = sub<<4;
	memmove(f+4, da, 6);
	memmove(f+10, bssid, 6);
	memmove(f+16, bssid, 6);
	put16(f+22, (seq++ & 0xfff) << 4);
	return f+24;
}

static uchar*
ies(uchar *p)
{
	*p++ = 0;
	*p++ = strlen(essid);
	memmove(p, essid, strlen(essid));
	p += strlen(essid);
	*p++ = 1;
	*p++ = sizeof rates;
	memmove(p, rates, sizeof rates);
	p += sizeof rates;
	*p++ = 3;
	*p++ = 1;
	*p++ = Channel;
	return p;
}

/* a beacon (sub 8) or probe response (sub 5) */
static void
beacon(int sub, uchar *da)
{
	uchar f[256], *p;

	p = mgmt(f, sub, da);
	memset(p, 0, 8);	/* timestamp */
	p += 8;
	put16(p, 100);		/* interval */
	put16(p+2, 0x0001);	/* ESS */
	p = ies(p+4);
	send(f, p - f);
}

static void
beaconproc(void)
{
	for(;;){
		if(tuned == Channel)
			beacon(8, bcast);
		sleep(100);
	}
}

/* a data frame from the host (behind the access point) to the station */
static void
data(int type, uchar *pl, int n)
{
	uchar f[Nframe], *p;

	if(24 + 8 + n > Nframe)
		return;
	memset(f, 0, 24);
	f[0] = 0x08;
	f[1] = 0x02;	/* from DS */
	memmove(f+4, sta, 6);
	memmove(f+10, bssid, 6);
	memmove(f+16, host, 6);
	put16(f+22, (seq++ & 0xfff) << 4);
	p = f+24;
	memmove(p, "\xaa\xaa\x03\x00\x00\x00", 6);
	p[6] = type>>8;
	p[7] = type;
	memmove(p+8, pl, n);
	send(f, 24 + 8 + n);
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
fromsta(int type, uchar *p, int n)
{
	uchar r[1600];
	int hl, c;

	if(type == 0x0806 && n >= 28 && get16(p+6) == 0x0100 && memcmp(p+24, hostip, 4) == 0){
		memmove(r, p, 28);
		r[7] = 2;	/* reply */
		memmove(r+8, host, 6);
		memmove(r+14, hostip, 4);
		memmove(r+18, p+8, 10);	/* to the asker */
		if(debug)
			fprint(2, "wlansim: arp: %V is %E\n", hostip, host);
		data(0x0806, r, 28);
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
		fprint(2, "wlansim: echo from %V\n", p+12);
		data(0x0800, r, n);
	}
}

static void
frame(uchar *f, int n)
{
	uchar r[256], *p;
	int type, sub;

	if(n < 24)
		return;
	type = f[0]>>2 & 3;
	sub = f[0]>>4;
	if(type == 0){
		if(memcmp(f+4, bssid, 6) != 0 && memcmp(f+4, bcast, 6) != 0)
			return;
		switch(sub){
		case 4:	/* probe request */
			if(debug)
				fprint(2, "wlansim: probe from %E\n", f+10);
			beacon(5, f+10);
			return;
		case 11:	/* authentication */
			memmove(sta, f+10, 6);
			fprint(2, "wlansim: auth from %E\n", sta);
			p = mgmt(r, 11, sta);
			put16(p, 0);	/* open system */
			put16(p+2, 2);	/* sequence */
			put16(p+4, 0);	/* success */
			send(r, p+6 - r);
			return;
		case 0:	/* association request */
			memmove(sta, f+10, 6);
			fprint(2, "wlansim: associated %E\n", sta);
			p = mgmt(r, 1, sta);
			put16(p, 0x0001);
			put16(p+2, 0);	/* success */
			put16(p+4, 0xc001);	/* aid 1 */
			p += 6;
			*p++ = 1;
			*p++ = sizeof rates;
			memmove(p, rates, sizeof rates);
			p += sizeof rates;
			send(r, p - r);
			return;
		}
		return;
	}
	if(type == 2 && (f[1] & 3) == 1 && n >= 32 && memcmp(f+24, "\xaa\xaa\x03\x00\x00\x00", 6) == 0)
		fromsta(f[30]<<8 | f[31], f+32, n-32);
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
	int n;

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
	if((fd = open(smprint("%s/data", dev), ORDWR)) < 0)
		sysfatal("%s: %r", dev);
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
		tuned = get16(b);
		if(debug > 1)
			fprint(2, "wlansim: tx channel %d flags %#x %d bytes\n", tuned, b[3], n-Hdrtx);
		if((b[3] & Ftune) == 0)
			frame(b+Hdrtx, n-Hdrtx);
	}
}
