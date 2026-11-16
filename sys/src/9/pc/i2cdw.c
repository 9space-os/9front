/*
 * 9space: Synopsys DesignWare I2C controllers, as an x86 SoC has them
 * (AMD's AMDI0010; Intel's are PCI devices, not here yet), busses of
 * i2c(3): found in ACPI at boot (archacpi.c: enumi2c calls i2cdwinit),
 * named as the ACPI device (\_SB.I2CB: i2cb).  Not scanned: the bus
 * holds the devices ACPI names (i2cdwdev; a read at every address can
 * upset what is wired there, and neither Linux nor OpenBSD scans these).
 * *i2cdebug= prints each transfer that fails.  Polled: an I2C-HID
 * touchpad's reports are tens of bytes, its interrupt is a GPIO pin
 * (#γ).  The controller driven as OpenBSD's dwiic does
 * (sys/dev/ic/dwiic.c,v 1.21, its polled path):
 *
 * Copyright (c) 2015-2017 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/i2c.h"

enum {
	Con		= 0x00,
	Tar		= 0x04,
	DataCmd		= 0x10,
	IntrMask	= 0x30,
	RawIntrStat	= 0x34,
	RxTl		= 0x38,
	TxTl		= 0x3c,
	ClrIntr		= 0x40,
	ClrTxAbrt	= 0x54,
	Enable		= 0x6c,
	Status		= 0x70,
	Txflr		= 0x74,
	Rxflr		= 0x78,
	TxAbrtSource	= 0x80,
	EnableStatus	= 0x9c,
	CompParam1	= 0xf4,
	CompType	= 0xfc,

	CompTypeValue	= 0x44570140,

	ConMaster	= 1<<0,
	ConSpeedFast	= 2<<1,
	Con10bit	= 1<<4,
	ConRestartEn	= 1<<5,
	ConSlaveDisable	= 1<<6,

	CmdRead		= 1<<8,
	CmdStop		= 1<<9,
	CmdRestart	= 1<<10,

	IntrTxAbrt	= 1<<6,
	IntrStopDet	= 1<<9,

	StatusActivity	= 1<<0,

	Nctlr		= 8,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	u32int	*regs;
	int	irq;
	int	txdepth;
	int	rxdepth;
	char	name[8];
	I2Cbus	bus;
};

static Ctlr ctlrs[Nctlr];
static int nctlr;
static int debug;

static u32int
rd(Ctlr *c, int r)
{
	return c->regs[r/4];
}

static void
wr(Ctlr *c, int r, u32int v)
{
	c->regs[r/4] = v;
}

static int
enable(Ctlr *c, int on)
{
	int i;

	for(i = 0; i < 100; i++){
		wr(c, Enable, on);
		if((rd(c, EnableStatus) & 1) == on)
			return 0;
		microdelay(25);
	}
	print("%s: can't %sable\n", c->name, on ? "en" : "dis");
	return -1;
}

/* until f(c) or ms milliseconds: 0, or -1 */
static int
waitfor(Ctlr *c, int (*f)(Ctlr*), int ms)
{
	int i;

	for(i = 0; i < ms*20; i++){
		if(f(c))
			return 0;
		microdelay(50);
	}
	return -1;
}

static int
quiet(Ctlr *c)
{
	return (rd(c, Status) & StatusActivity) == 0;
}

static int
txroom(Ctlr *c)
{
	return rd(c, Txflr) < c->txdepth;
}

static int
rxany(Ctlr *c)
{
	return rd(c, Rxflr) > 0;
}

static int
stopped(Ctlr *c)
{
	return (rd(c, RawIntrStat) & (IntrStopDet|IntrTxAbrt)) != 0;
}

static int
init(I2Cbus *bus)
{
	Ctlr *c;
	u32int v;
	int d;

	c = bus->ctlr;
	if(rd(c, CompType) != CompTypeValue){
		print("%s: not a DesignWare I2C controller (%#ux)\n", c->name, rd(c, CompType));
		return -1;
	}
	/* the timings as the firmware left them (no SSCN, FMCN here) */
	if(enable(c, 0) < 0)
		return -1;
	c->txdepth = c->rxdepth = 32;
	v = rd(c, CompParam1);
	if((d = (v>>16 & 0xff) + 1) > 1 && d < c->txdepth)
		c->txdepth = d;
	if((d = (v>>8 & 0xff) + 1) > 1 && d < c->rxdepth)
		c->rxdepth = d;
	wr(c, TxTl, c->txdepth/2);
	wr(c, RxTl, 0);
	wr(c, Con, ConMaster|ConSlaveDisable|ConRestartEn|ConSpeedFast);
	wr(c, IntrMask, 0);
	return 0;
}

/*
 * pkt: the address byte, what is written; ilen bytes read after it (a
 * repeated start when something was written).  The bytes done, or -1
 */
static int
io(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	Ctlr *c;
	int nw, o, issued, x;
	u32int cmd;
	char *why;

	c = dev->bus->ctlr;
	o = 0;
	why = "";
	if(olen <= 0)
		return 0;
	if((pkt[0] & 0xF8) == 0xF0)
		return -1;	/* 10-bit addresses: not yet */
	if(waitfor(c, quiet, 100) < 0)
		return -1;
	if(enable(c, 0) < 0)
		return -1;
	wr(c, Con, rd(c, Con) & ~Con10bit);
	wr(c, Tar, pkt[0]>>1);
	wr(c, IntrMask, 0);
	rd(c, ClrIntr);
	if(enable(c, 1) < 0)
		return -1;
	microdelay(200);

	nw = olen - 1;
	if(nw == 0 && ilen == 0){
		/*
		 * i2c(3)'s probe, the address alone: the controller has no
		 * transfer of no bytes: a byte read instead, its byte dropped
		 */
		wr(c, DataCmd, CmdRead|CmdStop);
		why = "probe: no stop";
		if(waitfor(c, stopped, 100) < 0)
			goto Err;
		why = "probe: not acknowledged";
		if(rd(c, RawIntrStat) & IntrTxAbrt)
			goto Err;
		while(rd(c, Rxflr) > 0)
			rd(c, DataCmd);
		rd(c, ClrIntr);
		return olen;
	}
	why = "no room to send";
	for(x = 0; x < nw; x++){
		if(waitfor(c, txroom, 50) < 0)
			goto Err;
		cmd = pkt[1+x];
		if(x == nw-1 && ilen == 0)
			cmd |= CmdStop;
		wr(c, DataCmd, cmd);
	}
	o = olen;
	issued = 0;
	while(o < olen + ilen){
		/* reads asked for, no more than the FIFOs hold */
		while(issued < ilen && txroom(c) && issued - (o - olen) < c->rxdepth){
			cmd = CmdRead;
			if(issued == 0 && nw > 0)
				cmd |= CmdRestart;
			if(issued == ilen-1)
				cmd |= CmdStop;
			wr(c, DataCmd, cmd);
			issued++;
		}
		why = "nothing received";
		if(waitfor(c, rxany, 50) < 0)
			goto Err;
		while(rd(c, Rxflr) > 0 && o < olen + ilen)
			pkt[o++] = rd(c, DataCmd);
	}
	why = "no stop";
	if(waitfor(c, stopped, 100) < 0)
		goto Err;
	why = "not acknowledged";
	if(rd(c, RawIntrStat) & IntrTxAbrt)
		goto Err;	/* not acknowledged (no such device, refused) */
	rd(c, ClrIntr);
	return o;
Err:
	if(debug)
		iprint("%s: %#ux: %s, status %#ux raw %#ux abort %#ux txflr %d rxflr %d, %d of %d bytes\n",
			c->name, pkt[0]>>1, why, rd(c, Status), rd(c, RawIntrStat), rd(c, TxAbrtSource),
			rd(c, Txflr), rd(c, Rxflr), o, olen+ilen);
	rd(c, ClrTxAbrt);
	rd(c, ClrIntr);
	enable(c, 0);
	return -1;
}

/*
 * called by archacpi.c for AMDI0010 at boot: \_SB.name's registers,
 * interrupt.  A controller the firmware left off (its registers all
 * ones) is not a bus; the bus is set up here, not scanned (i2cdwdev).
 */
void
i2cdwinit(char *name, uvlong pa, ulong len, int irq)
{
	Ctlr *c;
	int i;

	if(nctlr >= Nctlr || len < 0x100)
		return;
	debug = getconf("*i2cdebug") != nil;
	c = &ctlrs[nctlr];
	for(i = 0; i < sizeof c->name - 1 && name[i] != 0; i++)
		c->name[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 'a' - 'A' : name[i];
	c->name[i] = 0;
	if((c->regs = vmap(pa, len)) == nil){
		print("i2c %s: can't map %#llux\n", c->name, pa);
		return;
	}
	c->irq = irq;
	c->bus.name = c->name;
	c->bus.speed = 400000;
	c->bus.ctlr = c;
	c->bus.init = init;
	c->bus.io = io;
	if(rd(c, CompType) == 0xFFFFFFFF){
		print("i2c %s: off\n", c->name);
		vunmap(c->regs, len);
		return;
	}
	if(init(&c->bus) < 0){
		vunmap(c->regs, len);
		return;
	}
	c->bus.probed = 1;
	addi2cbus(&c->bus);
	nctlr++;
	print("i2c %s: designware %#llux irq %d\n", c->name, pa, irq);
}

/* a device on bus name that ACPI names (_CRS: I2cSerialBus) */
void
i2cdwdev(char *name, int addr, int a10)
{
	I2Cdev *d;
	int i;

	for(i = 0; i < nctlr; i++)
		if(cistrcmp(ctlrs[i].name, name) == 0)
			break;
	if(i == nctlr || (d = malloc(sizeof *d)) == nil)
		return;
	d->bus = &ctlrs[i].bus;
	d->addr = addr;
	d->a10 = a10;
	d->subaddr = 0;
	d->size = 0;
	addi2cdev(d);
	print("i2c %s: device %#ux\n", ctlrs[i].name, addr);
}
