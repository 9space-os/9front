/*
 * embedded controller (usually at ports 0x66/0x62)
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

enum {
	/* registers */
	EC_SC	= 0,
	EC_DATA,

	/* Embedded Controller Status, EC_SC (R) */
	OBF	= 1<<0,
	IBF	= 1<<1,
	CMD	= 1<<3,
	BURST	= 1<<4,
	SCI_EVT	= 1<<5,
	SMI_EVT	= 1<<6,

	/* Embedded Controller Command Set */
	RD_EC	= 0x80,
	WR_EC	= 0x81,
	BE_EC	= 0x82,
	BD_EC	= 0x83,
	QR_EC	= 0x84,
};

static struct {
	Lock;
	int	init;
	int	port[2];	/* EC_SC and EC_DATA */
} ec;

static uchar
ecrr(int reg)
{
	return inb(ec.port[reg]);
}
static void
ecwr(int reg, uchar val)
{
	outb(ec.port[reg], val);
}

static int
ecwait(uchar mask, uchar val)
{
	int i, s;

	s = 0;
	for(i=0; i<1000; i++){
		s = ecrr(EC_SC);
		if((s & mask) == val)
			return 0;
		delay(1);
	}
	print("ec: wait timeout status=%x pc=%#p\n", s, getcallerpc(&mask));
	return -1;
}

int
ecread(uchar addr)
{
	int r;

	r = -1;
	lock(&ec.Lock);
	if(!ec.init)
		goto out;
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_SC, RD_EC);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, addr);
	if(ecwait(OBF, OBF))
		goto out;
	r = ecrr(EC_DATA);
	ecwait(OBF, 0);
out:
	unlock(&ec.Lock);
	return r;
}

int
ecwrite(uchar addr, uchar val)
{
	int r;

	r = -1;
	lock(&ec.Lock);
	if(!ec.init)
		goto out;
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_SC, WR_EC);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, addr);
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_DATA, val);
	if(ecwait(IBF, 0))
		goto out;
	r = 0;
out:
	unlock(&ec.Lock);
	return r;
}

/*
 * nfront: the query (QR_EC) of a pending SCI event, for aux/acpi's _Qxx
 * methods (#P/ecquery: "0xNN\n", 0 when none is pending), as OpenBSD's
 * acpiec_sci_event (sys/dev/acpi/acpiec.c,v 1.67) does it:
 *
 * Copyright (c) 2006 Can Erkin Acar <canacar@openbsd.org>
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
static int
ecquery(void)
{
	int r;

	r = -1;
	lock(&ec.Lock);
	if(!ec.init)
		goto out;
	if((ecrr(EC_SC) & SCI_EVT) == 0){
		r = 0;
		goto out;
	}
	if(ecwait(IBF, 0))
		goto out;
	ecwr(EC_SC, QR_EC);
	if(ecwait(OBF, OBF))
		goto out;
	r = ecrr(EC_DATA);
out:
	unlock(&ec.Lock);
	return r;
}

static long
ecqueryread(Chan*, void *a, long n, vlong off)
{
	char buf[16];
	int q;

	if(off != 0)
		return 0;
	if((q = ecquery()) < 0)
		error(Eio);
	snprint(buf, sizeof buf, "%#.2ux\n", q);
	return readstr(0, a, n, buf);
}

static long
ecarchread(Chan*, void *a, long n, vlong off)
{
	int port, v;
	uchar *p;

	if(off < 0 || off >= 256)
		return 0;
	if(off+n > 256)
		n = 256 - off;
	p = a;
	for(port = off; port < off+n; port++){
		if((v = ecread(port)) < 0)
			error(Eio);
		*p++ = v;
	}
	return n;
}

static long
ecarchwrite(Chan*, void *a, long n, vlong off)
{
	int port;
	uchar *p;

	if(off < 0 || off+n > 256)
		error(Ebadarg);
	p = a;
	for(port = off; port < off+n; port++)
		if(ecwrite(port, *p++) < 0)
			error(Eio);
	return n;
}

int
ecinit(int cmdport, int dataport)
{
	print("ec: cmd %X, data %X\n", cmdport, dataport);

	if(ioalloc(cmdport, 1, 0, "ec.sc") < 0){
		print("ec: cant allocate cmd port %X\n", cmdport);
		return -1;
	}
	if(ioalloc(dataport, 1, 0, "ec.data") < 0){
		print("ec: cant allocate data port %X\n", dataport);
		iofree(cmdport);
		return -1;
	}

	lock(&ec.Lock);
	ec.port[EC_SC] = cmdport;
	ec.port[EC_DATA] = dataport;
	ec.init = 1;
	unlock(&ec.Lock);

	addarchfile("ec", 0660, ecarchread, ecarchwrite);
	addarchfile("ecquery", 0440, ecqueryread, nil);	/* nfront */

	return 0;
}
