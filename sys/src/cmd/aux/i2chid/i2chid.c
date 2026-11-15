/*
 * aux/i2chid: HID over I²C (Microsoft's protocol, version 1.0) -- the
 * touchpads, touchscreens and keyboards of laptops that are no USB
 * devices.  The firmware's ACPI names them (_CID PNP0C50): the I²C bus
 * and address (_CRS I2cSerialBus), the interrupt (GpioInt, a pin of
 * #γ's GPIO controller), the HID descriptor's register (_DSM); aux/acpi
 * evaluates them (its /dev/aml).  The bus is i2c(3)'s, the device's
 * reports go through nusb/kb's HID code (hid.c) to /dev/mousein and
 * /dev/kbin; a precision touchpad is put into its mode (hidtouchpad):
 * one finger moves, two scroll (-n: as on a phone), taps click.
 *
 * The protocol's steps as OpenBSD's ihidev does them
 * (sys/dev/i2c/ihidev.c,v 1.33: the HID descriptor, SET_POWER, RESET,
 * the report descriptor, an input report per interrupt):
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <u.h>
#include <libc.h>
#include "hid.h"

enum {
	/* the HID descriptor (protocol 1.0, 5.1.1), 30 bytes */
	HdLen		= 0,
	HdVersion	= 2,
	HdRepLen	= 4,
	HdRepReg	= 6,
	HdInReg		= 8,
	HdInMax		= 10,
	HdOutReg	= 12,
	HdOutMax	= 14,
	HdCmdReg	= 16,
	HdDataReg	= 18,
	HdVid		= 20,
	HdPid		= 22,
	HdVer		= 24,
	Nhd		= 30,

	/* commands (7.2), the opcode in the high byte */
	CmdReset	= 0x01,
	CmdSetReport	= 0x03,
	TypeFeature	= 3,
	CmdSetPower	= 0x08,
	PowerOn		= 0,
	PowerSleep	= 1,

	Maxrep		= 4096,		/* report descriptor */
	Maxin		= 512,		/* input report */
	Ndev		= 4,
};

typedef struct Dev Dev;
struct Dev {
	char	path[64];	/* ACPI's, \_SB.I2CB.TPD0 */
	char	bus[16];	/* i2c(3)'s, i2cb */
	int	addr;
	int	pin;		/* GpioInt's, -1: none */
	int	edge;
	int	pol;		/* 0 high, 1 low, 2 both */
	int	hdreg;

	uchar	hd[Nhd];
	int	regfd;	/* data file, the register in front (sub-address 2) */
	int	infd;	/* data file, none: input reports */
	int	minfd;
	int	kinfd;

	uchar	rep[Maxrep];
	int	nrep;
	Hid	hid;
	QLock	lk;	/* hid: the reader's, the timer's */
};

static int debug;
static int natural;
static int amlfd = -1;

static ushort
get16(uchar *p)
{
	return p[0] | p[1]<<8;
}

/* aux/acpi's interpreter: /dev/aml, or the one /srv/acpi serves */
static int
amlopen(void)
{
	int fd;

	if((amlfd = open("/dev/aml", ORDWR)) >= 0)
		return 0;
	if((fd = open("/srv/acpi", ORDWR)) < 0)
		return -1;
	if(mount(fd, -1, "/dev", MAFTER, "") < 0){
		close(fd);
		return -1;
	}
	if((amlfd = open("/dev/aml", ORDWR)) < 0)
		return -1;
	return 0;
}

/* a request's answer, malloc'd, or nil */
static char*
aml(char *fmt, ...)
{
	char req[512], *r;
	va_list arg;
	long n, m;

	va_start(arg, fmt);
	vseprint(req, req+sizeof req, fmt, arg);
	va_end(arg);
	if(write(amlfd, req, strlen(req)) != strlen(req))
		return nil;
	n = 0;
	r = nil;
	for(;;){
		if((r = realloc(r, n + 8192 + 1)) == nil)
			return nil;
		if((m = pread(amlfd, r + n, 8192, n)) <= 0)
			break;
		n += m;
	}
	r[n] = 0;
	return r;
}

/*
 * aml(8)'s values: -, ihex;, sN:bytes, bN:hex, pN:values, rN:path.
 * skipval: past one value; nil if malformed
 */
static char*
skipval(char *p)
{
	char *t;
	int n;

	switch(*p){
	case '-':
		return p+1;
	case 'i':
		if((t = strchr(p, ';')) == nil)
			return nil;
		return t+1;
	case 's':
	case 'r':
	case 'b':
	case 'p':
		n = strtol(p+1, &t, 10);
		if(*t != ':' || n < 0)
			return nil;
		t++;
		if(*p == 's' || *p == 'r')
			return strlen(t) < n ? nil : t + n;
		if(*p == 'b')
			return strlen(t) < 2*n ? nil : t + 2*n;
		while(n-- > 0)
			if((t = skipval(t)) == nil)
				return nil;
		return t;
	}
	return nil;
}

/* a buffer value: its bytes in b (at most nb), their number or -1 */
static int
bufval(char *p, uchar *b, int nb)
{
	char *t;
	int n, i;

	if(p == nil || *p != 'b')
		return -1;
	n = strtol(p+1, &t, 10);
	if(*t++ != ':' || n < 0 || n > nb || strlen(t) < 2*n)
		return -1;
	for(i = 0; i < n; i++){
		char h[3] = { t[2*i], t[2*i+1], 0 };
		b[i] = strtol(h, nil, 16);
	}
	return n;
}

/* the text of an EISA id (or of a string), in id */
static int
idval(char *p, char *id, int nid)
{
	uvlong v;
	char *t;
	int n;

	if(*p == 's'){
		n = strtol(p+1, &t, 10);
		if(*t++ != ':' || n >= nid)
			return -1;
		memmove(id, t, n);
		id[n] = 0;
		return 0;
	}
	if(*p != 'i' || nid < 8)
		return -1;
	v = strtoull(p+1, nil, 16);
	v = (v&0xff)<<24 | (v>>8&0xff)<<16 | (v>>16&0xff)<<8 | v>>24&0xff;
	snprint(id, nid, "%c%c%c%.4llX",
		(int)(v>>26 & 0x1f) + '@', (int)(v>>21 & 0x1f) + '@', (int)(v>>16 & 0x1f) + '@',
		v & 0xffff);
	return 0;
}

/* is the device's _HID or one of its _CIDs id? */
static int
hasid(char *path, char *id)
{
	char *r, *p, buf[16];
	int n, ok;

	ok = 0;
	if((r = aml("hid %s", path)) != nil){
		ok = strcmp(r, id) == 0;
		free(r);
	}
	if(ok || (r = aml("eval %s._CID 0", path)) == nil)
		return ok;
	p = r;
	n = 1;
	if(*p == 'p'){
		n = strtol(p+1, &p, 10);
		p++;
	}
	while(n-- > 0 && p != nil && *p != 0){
		if(idval(p, buf, sizeof buf) == 0 && strcmp(buf, id) == 0)
			ok = 1;
		p = skipval(p);
	}
	free(r);
	return ok;
}

/* the last segment of an ACPI path, lower case: devi2c's bus name (i2cdw.c) */
static void
busname(char *dst, int n, char *path, int len)
{
	char *s, *e;
	int i;

	e = path + len;
	for(s = e; s > path && s[-1] != '.' && s[-1] != '\\'; s--)
		;
	for(i = 0; i < n-1 && s < e && *s != 0; i++, s++)
		dst[i] = *s >= 'A' && *s <= 'Z' ? *s + 'a' - 'A' : *s;
	dst[i] = 0;
}

/* _CRS: the I²C bus, the address, the interrupt's pin */
static int
crs(Dev *d)
{
	uchar b[1024], *p, *e, *q;
	char *r;
	int n, len, tlen, ok;

	if((r = aml("eval %s._CRS 0", d->path)) == nil)
		return -1;
	n = bufval(r, b, sizeof b);
	free(r);
	if(n < 0)
		return -1;
	ok = 0;
	d->pin = -1;
	for(p = b, e = b+n; p < e; p += len){
		if((p[0] & 0x80) == 0){	/* small item */
			if((p[0]>>3 & 0xf) == 0xf)
				break;	/* end tag */
			len = 1 + (p[0] & 7);
			continue;
		}
		if(p+3 > e)
			break;
		len = 3 + get16(p+1);
		if(p+len > e)
			break;
		switch(p[0]){
		case 0x8E:	/* GenericSerialBus (ACPI 6.5, 6.4.3.8.2) */
			if(len < 18 || p[5] != 1)	/* not I²C */
				break;
			if(get16(p+7) & 1)
				break;	/* 10-bit address: not yet */
			tlen = get16(p+10);
			d->addr = get16(p+16);
			q = p + 12 + tlen;
			if(q < p+len)
				busname(d->bus, sizeof d->bus, (char*)q, p+len - q);
			ok = d->bus[0] != 0;
			break;
		case 0x8C:	/* GPIO connection (6.4.3.8.1) */
			if(len < 23 || p[4] != 0 || d->pin >= 0)	/* GpioInt, the first */
				break;
			d->edge = get16(p+7) & 1;
			d->pol = get16(p+7)>>1 & 3;
			q = p + get16(p+14);
			if(q+2 <= p+len)
				d->pin = get16(q);
			break;
		}
	}
	return ok ? 0 : -1;
}

/*
 * _DSM 3CDFF6F7-4267-4555-AD05-B30A3D8938DE (HID over I²C, 3.2),
 * revision 1, function 1: the HID descriptor's register
 */
static int
hdregister(Dev *d)
{
	char *r;
	int v;

	r = aml("eval %s._DSM 4 b16:f7f6df3c67425545ad05b30a3d8938dei1;i1;p0:", d->path);
	if(r == nil || r[0] != 'i'){
		free(r);
		return -1;
	}
	v = strtol(r+1, nil, 16);
	free(r);
	return v;
}

static int
rdreg(Dev *d, int reg, uchar *buf, int n)
{
	return pread(d->regfd, buf, n, reg);
}

/* a command to the command register */
static int
command(Dev *d, int op, int arg)
{
	uchar b[2];

	b[0] = arg;
	b[1] = op;
	return pwrite(d->regfd, b, 2, get16(d->hd+HdCmdReg)) == 2 ? 0 : -1;
}

/* an input report into buf: its bytes after the length, 0: none (reset), -1 */
static int
input(Dev *d, uchar *buf, int max)
{
	int n;

	if(max > get16(d->hd+HdInMax))
		max = get16(d->hd+HdInMax);
	if((n = pread(d->infd, buf, max, 0)) < 2)
		return -1;
	n = get16(buf);
	if(n == 0)
		return 0;
	if(n < 2 || n > max)
		return -1;
	return n - 2;
}

/*
 * SET_REPORT of a feature report (7.2.2): rep its id, then its data;
 * the command, the data register, the length and the report in one write
 */
static int
setfeature(Hid *h, uchar *rep, int n)
{
	Dev *d = h->aux;
	uchar b[8+64];
	int id, o, len;

	if(n > 64)
		return -1;
	id = rep[0];
	o = 0;
	b[o++] = TypeFeature<<4 | (id >= 15 ? 15 : id);
	b[o++] = CmdSetReport;
	if(id >= 15)
		b[o++] = id;
	b[o++] = d->hd[HdDataReg];
	b[o++] = d->hd[HdDataReg+1];
	if(id == 0){
		rep++;
		n--;
	}
	len = 2 + (id != 0) + n;
	b[o++] = len;
	b[o++] = len>>8;
	if(id != 0)
		b[o++] = id;
	memmove(b+o, rep, n);
	o += n;
	if(pwrite(d->regfd, b, o, get16(d->hd+HdCmdReg)) != o){
		werrstr("set report %d: %r", id);
		return -1;
	}
	return 0;
}

static void
dkeys(Hid *h, uchar *uks, int nuks, uchar *dks, int ndks)
{
	Dev *d = h->aux;
	uchar s[2];
	int i, n;

	/* no repeat: nusb/kb's repeats are for USB keyboards */
	if(d->kinfd < 0 && (d->kinfd = open("/dev/kbin", OWRITE)) < 0)
		return;
	for(i = 0; i < nuks; i++)
		if((n = hidscan(s, uks[i], 1)) > 0)
			write(d->kinfd, s, n);
	for(i = 0; i < ndks; i++)
		if((n = hidscan(s, dks[i], 0)) > 0)
			write(d->kinfd, s, n);
}

static void
dmouse(Hid *h, int abs, int x, int y, int b)
{
	Dev *d = h->aux;
	char buf[80];

	if(d->minfd < 0 && (d->minfd = open("/dev/mousein", OWRITE)) < 0)
		return;
	snprint(buf, sizeof buf, "%c%11d %11d %11d", "ma"[abs], x, y, b);
	write(d->minfd, buf, strlen(buf));
}

/* the device on: HID descriptor, power, reset, report descriptor */
static int
setup(Dev *d)
{
	char ctl[64], data[64];
	uchar buf[Maxin];
	int n, i, fd;

	/* i2c(3): a data file keeps the sub-address it was opened with */
	snprint(ctl, sizeof ctl, "#J/%s/i2c.%ux.ctl", d->bus, d->addr);
	snprint(data, sizeof data, "#J/%s/i2c.%ux.data", d->bus, d->addr);
	if((fd = open(ctl, ORDWR)) < 0)
		return -1;
	if(fprint(fd, "size 0") < 0 || fprint(fd, "subaddress 2") < 0
	|| (d->regfd = open(data, ORDWR)) < 0
	|| fprint(fd, "subaddress 0") < 0
	|| (d->infd = open(data, OREAD)) < 0){
		close(fd);
		return -1;
	}
	close(fd);
	if(rdreg(d, d->hdreg, d->hd, Nhd) != Nhd){
		werrstr("HID descriptor: %r");
		return -1;
	}
	if(get16(d->hd+HdLen) != Nhd || get16(d->hd+HdVersion) != 0x100){
		werrstr("HID descriptor: length %d version %#ux",
			get16(d->hd+HdLen), get16(d->hd+HdVersion));
		return -1;
	}
	if(debug)
		fprint(2, "%s: %s: hid %.4ux:%.4ux version %.4ux, report descriptor %d at %#ux, input %d at %#ux\n",
			argv0, d->path, get16(d->hd+HdVid), get16(d->hd+HdPid), get16(d->hd+HdVer),
			get16(d->hd+HdRepLen), get16(d->hd+HdRepReg),
			get16(d->hd+HdInMax), get16(d->hd+HdInReg));
	if(command(d, CmdSetPower, PowerOn) < 0){
		werrstr("set power: %r");
		return -1;
	}
	sleep(1);
	if(command(d, CmdReset, 0) < 0){
		werrstr("reset: %r");
		return -1;
	}
	/* the reset done: an input report of no bytes (7.2.1), within 5s */
	for(i = 0; i < 50; i++){
		sleep(100);
		if(input(d, buf, sizeof buf) == 0)
			break;
	}
	if((n = get16(d->hd+HdRepLen)) > sizeof d->rep){
		werrstr("report descriptor of %d bytes", n);
		return -1;
	}
	if(rdreg(d, get16(d->hd+HdRepReg), d->rep, n) != n){
		werrstr("report descriptor: %r");
		return -1;
	}
	d->nrep = n;
	d->hid.rep = d->rep;
	d->hid.nrep = d->nrep;
	d->hid.aux = d;
	d->hid.keys = dkeys;
	d->hid.mouse = dmouse;

	/* a precision touchpad: its fingers instead of a mouse's motion */
	if(hidtouchpad(&d->hid, setfeature) == 0){
		d->hid.tp.natural = natural;
		if(debug)
			fprint(2, "%s: %s: precision touchpad\n", argv0, d->path);
	} else if(debug)
		fprint(2, "%s: %s: %r: as it comes\n", argv0, d->path);
	return 0;
}

/* a touchpad's: hid.c lets go of a tap's button when no drag follows */
static void
timer(Dev *d)
{
	for(;;){
		sleep(20);
		qlock(&d->lk);
		hidtimer(&d->hid);
		qunlock(&d->lk);
	}
}

/* one proc per device: an input report per interrupt */
static void
reader(Dev *d)
{
	char file[64], ev[32];
	uchar buf[Maxin];
	int fd, n, efd;

	efd = -1;
	if(d->pin >= 0){
		if((fd = open("#γ/gpio/ctl", OWRITE)) < 0
		|| fprint(fd, "%d int %s %s", d->pin, d->edge ? "edge" : "level",
			d->pol == 0 ? "high" : d->pol == 1 ? "low" : "both") < 0)
			sysfatal("%s: gpio pin %d: %r", d->path, d->pin);
		close(fd);
		snprint(file, sizeof file, "#γ/gpio/event");
		if((efd = open(file, ORDWR)) < 0 || fprint(efd, "watch %d", d->pin) < 0)
			sysfatal("%s: gpio event: %r", d->path);
	}
	for(;;){
		if(efd >= 0){
			if((n = read(efd, ev, sizeof ev - 1)) <= 0)
				sysfatal("%s: gpio event: %r", d->path);
		} else
			sleep(10);	/* no interrupt: polled */
		while((n = input(d, buf, sizeof buf)) > 0){
			if(debug > 1)
				fprint(2, "%s: %.*H\n", d->path, n+2, buf);
			qlock(&d->lk);
			hidinput(&d->hid, buf+2, n);
			qunlock(&d->lk);
			if(efd < 0 || !d->edge)
				break;	/* level: the pin again while there is more */
		}
		if(n < 0 && debug)
			fprint(2, "%s: input: %r\n", argv0);
		if(efd >= 0)
			fprint(efd, "ack %d", d->pin);
	}
}

/* the devices with HID over I²C's id */
static int
finddevs(Dev *dev, int max)
{
	char *r, *l, *nl, *sp, *x;
	Dev *d;
	int n;

	if((r = aml("devices")) == nil){
		fprint(2, "%s: aml devices: %r\n", argv0);
		return 0;
	}
	n = 0;
	for(l = r; l != nil && *l != 0 && n < max; l = nl){
		if((nl = strchr(l, '\n')) != nil)
			*nl++ = 0;
		if((sp = strchr(l, ' ')) != nil)
			*sp = 0;
		if(!hasid(l, "PNP0C50") && !hasid(l, "ACPI0C50"))
			continue;
		if(debug)
			fprint(2, "%s: %s: HID over I²C\n", argv0, l);
		/* present: _STA's bit 0, or no _STA */
		if((x = aml("eval %s._STA 0", l)) != nil){
			if(x[0] == 'i' && (strtoull(x+1, nil, 16) & 1) == 0){
				free(x);
				continue;
			}
			free(x);
		}
		d = &dev[n];
		memset(d, 0, sizeof *d);
		d->minfd = d->kinfd = -1;
		strecpy(d->path, d->path+sizeof d->path, l);
		if(crs(d) < 0){
			fprint(2, "%s: %s: no I²C bus in _CRS\n", argv0, l);
			continue;
		}
		if((d->hdreg = hdregister(d)) < 0){
			fprint(2, "%s: %s: no _DSM for the HID descriptor\n", argv0, l);
			continue;
		}
		n++;
	}
	free(r);
	return n;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-dn]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Dev dev[Ndev], *d;
	int i, n;

	ARGBEGIN{
	case 'd':
		hiddebug = ++debug;
		break;
	case 'n':
		natural = 1;
		break;
	default:
		usage();
	}ARGEND;
	if(argc != 0)
		usage();
	fmtinstall('H', encodefmt);
	if(amlopen() < 0)
		sysfatal("no aux/acpi: %r");
	n = finddevs(dev, nelem(dev));
	close(amlfd);
	for(i = 0; i < n; i++){
		d = &dev[i];
		if(debug)
			fprint(2, "%s: %s: %s address %#ux, gpio pin %d %s %s\n", argv0,
				d->path, d->bus, d->addr, d->pin, d->edge ? "edge" : "level",
				d->pol == 0 ? "high" : d->pol == 1 ? "low" : "both");
		if(setup(d) < 0){
			fprint(2, "%s: %s: %r\n", argv0, d->path);
			continue;
		}
		switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
		case -1:
			sysfatal("rfork: %r");
		case 0:
			reader(d);
			exits(nil);
		}
		if(d->hid.tp.on)
			switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
			case -1:
				sysfatal("rfork: %r");
			case 0:
				timer(d);
				exits(nil);
			}
	}
	exits(nil);
}
