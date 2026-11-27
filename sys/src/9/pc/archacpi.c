#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/error.h"

#include "mp.h"

#include <aml.h>

typedef struct Rsd Rsd;
typedef struct Tbl Tbl;

struct Rsd {
	uchar	sig[8];
	uchar	csum;
	uchar	oemid[6];
	uchar	rev;
	uchar	raddr[4];
	uchar	len[4];
	uchar	xaddr[8];
	uchar	xcsum;
	uchar	reserved[3];
};

struct Tbl {
	uchar	sig[4];
	uchar	len[4];
	uchar	rev;
	uchar	csum;
	uchar	oemid[6];
	uchar	oemtid[8];
	uchar	oemrev[4];
	uchar	cid[4];
	uchar	crev[4];
	uchar	data[];
};

enum {
	Tblsz	= 4+4+1+1+6+8+4+4+4,
};

static Rsd *rsd;

static void mcfginit(void);

/* physical addresses visited by maptable() */
static int ntblpa;
static uvlong tblpa[64];

/* successfully mapped tables */
static int ntblmap;
static Tbl *tblmap[64];

static ushort
get16(uchar *p){
	return p[1]<<8 | p[0];
}

static uint
get32(uchar *p){
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p){
	uvlong u;

	u = get32(p+4);
	return u<<32 | get32(p);
}

static uint
tbldlen(Tbl *t){
	return get32(t->len) - Tblsz;
}

static long
memcheck(uintptr pa, long len)
{
	int i;
	uintptr pe;
	Confmem *cm;

	if(len <= 0)
		return len;
	pe = pa + len-1;
	if(pe < pa){
		len = -pa;
		pe = pa + len-1;
	}
	if(pa < PADDR(CPU0END))
		return 0;
	if(pe >= PADDR(KTZERO) && pa < PADDR(end))
		return PADDR(KTZERO) - pa;
	for(i=0; i<nelem(conf.mem); i++){
		cm = &conf.mem[i];
		if(cm->npage == 0)
			continue;
		if(pe >= cm->base && pa <= cm->base + cm->npage*BY2PG - 1)
			return cm->base - pa;
	}
	return len;
}

static void
maptable(uvlong pa)
{
	uchar *p, *e;
	u32int l;
	Tbl *t;
	int i;

	if(-pa < 8)
		return;

	if(ntblpa >= nelem(tblpa) || ntblmap >= nelem(tblmap))
		return;

	for(i=0; i<ntblpa; i++){
		if(pa == tblpa[i])
			return;
	}
	tblpa[ntblpa++] = pa;

	memreserve(pa, 8);
	if((t = vmapmem(pa, 8)) == nil)	/* GCC port: ACPI tables, memory: not volatile */
		return;
	l = get32(t->len);
	if(l < Tblsz
	|| l >= 0x10000000
	|| -pa < l){
		vunmap(t, 8);
		return;
	}
	memreserve(pa, l);
	vunmap(t, 8);
	if((t = vmapmem(pa, l)) == nil)	/* GCC port: ACPI tables, memory: not volatile */
		return;
	if(checksum(t, l)){
		vunmap(t, l);
		return;
	}
	tblmap[ntblmap++] = t;

	p = (uchar*)t;
	e = p + l;
	if(memcmp("RSDT", t->sig, 4) == 0){
		for(p = t->data; p+3 < e; p += 4)
			maptable(get32(p));
		return;
	}
	if(memcmp("XSDT", t->sig, 4) == 0){
		for(p = t->data; p+7 < e; p += 8)
			maptable(get64(p));
		return;
	}
	if(memcmp("FACP", t->sig, 4) == 0){
		if(l < 44)
			return;
		maptable(get32(p + 40));
		if(l < 148)
			return;
		maptable(get64(p + 140));
		return;
	}
}

static void
maptables(void)
{
	if(rsd == nil || ntblmap > 0 || ntblpa > 0)
		return;
	if(!checksum(rsd, 20))
		maptable(get32(rsd->raddr));
	if(rsd->rev >= 2){
		if(!checksum(rsd, 36))
			maptable(get64(rsd->xaddr));
	}
}

static Tbl*
findtable(char sig[4])
{
	Tbl *t;
	int i;

	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, sig, 4) == 0)
			return t;
	}
	return nil;
}

static Apic*
findioapic(int gsi, int *pintin)
{
	Apic *a;

	for(a = mpioapic; a != nil; a = a->next) {
		if((a->flags & PcmpEN) == 0)
			continue;
		if(gsi >= a->gsibase && gsi <= a->gsibase+a->mre){
			if(pintin)
				*pintin = gsi - a->gsibase;
			return a;
		}
	}
	print("findioapic: no ioapic found for gsi %d\n", gsi);
	return nil;
}

static void
addnmi(Apic *apic, int intin, int flags)
{
	Aintr *ai;

	if((ai = xalloc(sizeof(Aintr))) == nil)
		panic("addnmi: no memory for Aintr");

	ai->type = PcmpNMI;
	ai->flags = flags & (PcmpPOMASK|PcmpELMASK);
	ai->irq = -1;
	ai->intin = intin;

	ai->apic = apic;
	ai->anext = apic->aintr;
	apic->aintr = ai;

	if(apic->type == PcmpPROCESSOR && (uint)intin < 2)
		apic->lint[intin] = ai;
}

static void
addirq(int gsi, int type, int busno, int irq, int flags, void *from)
{
	Bus *bus;
	Apic *apic;
	Aintr *ai;
	int intin;

	if((apic = findioapic(gsi, &intin)) == nil)
		return;

	if(type == BusISA && irq < 16){
		for(ai = apic->aintr; ai != nil; ai = ai->anext)
			if(ai->intin == intin)
				return;
	}

	if((bus = mpgetbus(type, busno)) == nil)
		return;

	for(ai = bus->aintr; ai != nil; ai = ai->next)
		if(ai->irq == irq)
			return;

	if((ai = xalloc(sizeof(Aintr))) == nil)
		panic("addirq: no memory for Aintr");

	/* debugging */
	ai->from = nil;
	if(from != nil) {
		char buf[128];

		snprint(buf, sizeof(buf), "%N", from);
		kstrdup(&ai->from, buf);
	}

	ai->type = PcmpINT;
	ai->flags = flags & (PcmpPOMASK|PcmpELMASK);

	ai->irq = irq;
	ai->bus = bus;
	ai->next = bus->aintr;
	bus->aintr = ai;

	ai->intin = intin;
	ai->apic = apic;
	ai->anext = apic->aintr;
	apic->aintr = ai;
}

static int
pcibusno(void *dot)
{
	int bno, adr, tbdf, isroot;
	Pcidev *pdev;
	void *p, *x;
	char *id;

	id = nil;
	if((x = amlwalk(dot, "^_HID")) != nil)
		if((p = amlval(x)) != nil)
			id = amleisaid(p);
	isroot = id != nil && (strcmp(id, "PNP0A03") == 0 || strcmp(id, "PNP0A08") == 0);
	if((x = amlwalk(dot, "^_BBN")) == nil)
		x = amlwalk(dot, "^_ADR");
	if(x == nil){
		/* QEMU omits _BBN from root bridge, default to bus 0 */
		if(isroot)
			return 0;
		return -1;
	}
	p = nil;
	if(amleval(x, "", &p) < 0)
		return -1;
	adr = amlint(p);
	/* if root bridge, then we are done here */
	if(isroot)
		return adr;
	x = amlwalk(dot, "^");
	if(x == nil || x == dot)
		return -1;
	if((bno = pcibusno(x)) < 0)
		return -1;
	tbdf = MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	pdev = pcimatchtbdf(tbdf);
	if(pdev == nil)
		return -1;
	if(pdev->bridge == nil)
		return bno;
	return BUSBNO(pdev->bridge->tbdf);
}

static int
pciaddr(void *dot)
{
	int adr, bno;
	void *x, *p;

	for(;;){
		if((x = amlwalk(dot, "_ADR")) == nil){
			x = amlwalk(dot, "^");
			if(x == nil || x == dot)
				break;
			dot = x;
			continue;
		}
		if((bno = pcibusno(x)) < 0)
			break;
		p = nil;
		if(amleval(x, "", &p) < 0)
			break;
		adr = amlint(p);
		return MKBUS(BusPCI, bno, adr>>16, adr&0xFFFF);
	}
	return -1;
}

static int
reslen(uchar *p, uchar *e, int *ptype)
{
	int n;

	if(p >= e)
		return -1;
	if(p[0] & 0x80){
		/* large resource */
		if(p + 3 > e)
			return -1;
		n = 3 + get16(p+1);
		if(ptype) *ptype = p[0];
	} else {
		/* small resource */
		n = 1 + (p[0] & 7);
		if(ptype) *ptype = (p[0]>>3)&0xF;
	}
	if(p + n > e)
		return -1;
	return n;
}

static int
getirqs(void *d, uchar pmask[256/8], int *pflags)
{
	int i, c, t, n, m;
	uchar *p, *e;

	*pflags = 0;
	memset(pmask, 0, 256/8);
	if(amltag(d) != 'b')
		return -1;
	for(p = amlval(d), e = p + amllen(d); p < e; p += n) {
		if((n = reslen(p, e, &t)) < 0)
			break;
		switch(t){
		case 0x04:	/* IRQ Format Descriptor */
			if(n < 3)
				break;
			pmask[0] = p[1];
			pmask[1] = p[2];
			if(n > 3)
				*pflags = ((p[3] & (1<<0)) ? PcmpEDGE : PcmpLEVEL)
					| ((p[3] & (1<<3)) ? PcmpLOW : PcmpHIGH);
			return 0;
		case 0x89:	/* Extended IRQ Descriptor */
			if(n < 9)
				break;
			*pflags = ((p[3] & (1<<1)) ? PcmpEDGE : PcmpLEVEL)
				| ((p[3] & (1<<2)) ? PcmpLOW : PcmpHIGH);
			c = p[4];
			for(i=0; i<c; i++){
				if(5+4+i*4 > n)
					break;
				m = get32(p+5 + i*4);
				if(m >= 0 && m < 256)
					pmask[m/8] |= 1<<(m%8);
			}
			return 0;
		case 0x0F:	/* Terminator */
			return -1;
		}
	}
	return -1;
}

static uchar*
setirq(void *d, uint irq)
{
	uchar *p, *e, *q;
	int n, t;

	if(amltag(d) != 'b')
		return nil;
	for(p = amlval(d), e = p + amllen(d); p < e; p += n) {
		if((n = reslen(p, e, &t)) < 0)
			break;
		switch(t){
		case 0x04:	/* IRQ Format Descriptor */
			if(n < 3 || irq >= 16)
				break;
			q = amlnew('b', n);
			memmove(q, p, n);
			irq = 1<<irq;
			q[1] = irq;
			q[2] = irq>>8;
			return q;
		case 0x89:	/* Extended IRQ Descriptor */
			if(n < 9 || irq >= 256)
				break;
			q = amlnew('b', n);
			memmove(q, p, n);
			q[4] = 1;
			q[5] = irq;
			q[6] = irq>>8;
			q[7] = irq>>16;
			q[8] = irq>>24;
			return q;
		case 0x0F:	/* Terminator */
			return nil;
		}
	}
	return nil;
}

static int
setuplink(void *link, int *pflags)
{
	uchar pm[256/8], cm[256/8], *c;
	static int lastirq = 1;
	int gsi, i;
	void *r;

	if(amltag(link) != 'N')
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_PRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, pm, pflags) < 0)
		return -1;

	r = nil;
	if(amleval(amlwalk(link, "_CRS"), "", &r) < 0)
		return -1;
	if(getirqs(r, cm, pflags) < 0)
		return -1;
	
	gsi = -1;
	for(i=0; i<256; i++){
		if(pm[i>>3] & cm[i>>3] & 1<<(i&7))
			gsi = i;
	}

	if(gsi > 0 || getconf("*nopcirouting") != nil)
		return gsi;

	for(i=0; i<256; i++){
		gsi = lastirq++ & 0xFF;	/* round robin */
		if(pm[gsi>>3] & 1<<(gsi&7)){
			if((c = setirq(r, gsi)) == nil)
				break;
			if(amleval(amlwalk(link, "_SRS"), "b", c, nil) < 0)
				break;
			return gsi;
		}
	}
	return -1;
}

static int
enumprt(void *dot, void *)
{
	void *p, **a, **b;
	int bno, dno, pin, gsi, flags;
	int n, i;

	bno = pcibusno(dot);
	if(bno < 0)
		return 1;

	/* evalulate _PRT method */
	p = nil;
	if(amleval(dot, "", &p) < 0)
		return 1;
	if(amltag(p) != 'p')
		return 1;

	amltake(p);
	n = amllen(p);
	a = amlval(p);
	for(i=0; i<n; i++){
		if(amltag(a[i]) != 'p')
			continue;
		if(amllen(a[i]) != 4)
			continue;
		flags = 0;
		b = amlval(a[i]);
		dno = amlint(b[0])>>16;
		pin = amlint(b[1]);
		gsi = amlint(b[3]);
		if(gsi==0){
			gsi = setuplink(b[2], &flags);
			if(gsi <= 0)
				continue;
		}
		addirq(gsi, BusPCI, bno, (dno<<2)|pin, flags, dot);
	}
	amldrop(p);

	return 1;
}

static int
enumcrs(void *dot, void *)
{
	uchar mask[256/8];
	int flags, irq;
	void *p;

	p = nil;
	if(amleval(dot, "", &p) < 0)
		return 1;
	getirqs(p, mask, &flags);
	for(irq=0; irq<16; irq++){
		if(mask[irq>>3] & (1<<(irq&7)))
			addirq(irq, BusISA, 0, irq, flags, dot);
	}
	return 1;
}

static int
enumec(void *dot, void *)
{
	int cmdport, dataport;
	uchar *b;
	void *x;
	char *id;

	b = nil;
	id = amleisaid(amlval(amlwalk(dot, "^_HID")));
	if(id == nil || strcmp(id, "PNP0C09") != 0)
		return 1;
	if((x = amlwalk(dot, "^_CRS")) == nil)
		return 1;
	if(amleval(x, "", &b) < 0 || amltag(b) != 'b' || amllen(b) < 16)
		return 1;
	if(b[0] != 0x47 || b[8] != 0x47)	/* two i/o port descriptors */
		return 1;
	dataport = b[0+2] | b[0+3]<<8;
	cmdport = b[8+2] | b[8+3]<<8;
	ecinit(cmdport, dataport);
	return 1;
}

/*
 * 9space: AMD's GPIO controller (AMDI0030, AMD0030, _UID 0) for #γ
 * (devgpio.c): its registers (Memory32Fixed) and interrupt (Extended
 * IRQ) by _CRS, while the AML interpreter is there.  Its _STA wants an
 * _INI the kernel does not run (the OS's type): not asked.
 */
extern void amdgpioinit(uvlong, ulong, int);

static int
enumgpio(void *dot, void *)
{
	uchar *b, *p, *e;
	uvlong pa;
	ulong len;
	int irq, n, t;
	void *x;
	char *id;

	id = amleisaid(amlval(amlwalk(dot, "^_HID")));
	if(id == nil || strcmp(id, "AMDI0030") != 0 && strcmp(id, "AMD0030") != 0)
		return 1;
	if((x = amlwalk(dot, "^_UID")) != nil && amleval(x, "", &x) >= 0 && amlint(x) != 0)
		return 1;
	b = nil;
	if((x = amlwalk(dot, "^_CRS")) == nil || amleval(x, "", &b) < 0 || amltag(b) != 'b')
		return 1;
	pa = 0;
	len = 0;
	irq = -1;
	for(p = b, e = p + amllen(b); (n = reslen(p, e, &t)) > 0; p += n){
		if(t == 0x86 && n >= 12){	/* Memory32Fixed */
			pa = get32(p+4);
			len = get32(p+8);
		} else if(t == 0x89 && n >= 9 && irq < 0)	/* Extended IRQ */
			irq = get32(p+5);
	}
	if(pa != 0 && irq >= 0)
		amdgpioinit(pa, len, irq);
	return 1;
}

/*
 * 9space: AMD's DesignWare I2C controllers (AMDI0010) for i2c(3)
 * (i2cdw.c), as the GPIO controller above: by _CRS (one the firmware
 * left off is left out by i2cdwinit), and the devices on each, the
 * controller's children whose _CRS has an I2cSerialBus to it (their
 * _STA is aux/i2chid's to ask, after _INI)
 */
extern void i2cdwinit(char*, uvlong, ulong, int);
extern void i2cdwdev(char*, int, int);

typedef struct I2cbus I2cbus;
struct I2cbus {
	char	*path;	/* the controller's, \_SB.I2CB */
	char	*name;	/* its bus, I2CB */
};

static int
enumi2cdev(void *dot, void *a)
{
	I2cbus *bus = a;
	uchar *b, *p, *e;
	int n, t, tlen, src, len;

	b = nil;
	if(amleval(dot, "", &b) < 0 || amltag(b) != 'b')
		return 1;
	for(p = b, e = p + amllen(b); (n = reslen(p, e, &t)) > 0; p += n){
		/* GenericSerialBus, I2C (ACPI 6.5, 6.4.3.8.2) */
		if(t != 0x8E || n < 18 || p[5] != 1)
			continue;
		tlen = get16(p+10);
		src = 12 + tlen;
		if(src >= n)
			continue;
		len = strlen(bus->path);
		if(n - src <= len || cistrncmp((char*)p+src, bus->path, len) != 0 || p[src+len] != 0)
			continue;
		i2cdwdev(bus->name, get16(p+16), get16(p+7) & 1);
	}
	return 1;
}

static int
enumi2c(void *dot, void *)
{
	uchar *b, *p, *e;
	char path[64], *s;
	uvlong pa;
	ulong len;
	int irq, n, t;
	void *x, *dev;
	char *id;

	id = amleisaid(amlval(amlwalk(dot, "^_HID")));
	if(id == nil || strcmp(id, "AMDI0010") != 0 || (dev = amlwalk(dot, "^")) == nil)
		return 1;
	b = nil;
	if((x = amlwalk(dot, "^_CRS")) == nil || amleval(x, "", &b) < 0 || amltag(b) != 'b')
		return 1;
	pa = 0;
	len = 0;
	irq = -1;
	for(p = b, e = p + amllen(b); (n = reslen(p, e, &t)) > 0; p += n){
		if(t == 0x86 && n >= 12){	/* Memory32Fixed */
			pa = get32(p+4);
			len = get32(p+8);
		} else if(t == 0x89 && n >= 9 && irq < 0)	/* Extended IRQ */
			irq = get32(p+5);
	}
	snprint(path, sizeof path, "%N", dev);
	s = strrchr(path, '.');
	if(pa != 0){
		I2cbus bus;

		bus.path = path;
		bus.name = s != nil ? s+1 : path;
		i2cdwinit(bus.name, pa, len, irq);
		amlenum(dev, "_CRS", enumi2cdev, &bus);
	}
	return 1;
}

static long
readmem(Chan*, void *v, long n, vlong o)
{
	uvlong pa = (uvlong)o;
	void *t;

	if((n = memcheck(pa, n)) <= 0)
		return 0;
	if((t = vmapmem(pa, n)) == nil)	/* GCC port: ACPI tables, memory: not volatile */
		error(Enovmem);
	if(waserror()){
		vunmap(t, n);
		nexterror();
	}
	memmove(v, t, n);
	vunmap(t, n);
	poperror();
	return n;
}

static long
writemem(Chan*, void *v, long n, vlong o)
{
	uvlong pa = (uvlong)o;
	void *t;

	if(memcheck(pa, n) != n)
		error(Eio);
	if((t = vmapmem(pa, n)) == nil)	/* GCC port: ACPI tables, memory: not volatile */
		error(Enovmem);
	if(waserror()){
		vunmap(t, n);
		nexterror();
	}
	memmove(t, v, n);
	vunmap(t, n);
	poperror();
	return n;
}

/*
 * 9space: #P/acpiev, ACPI's events for aux/acpi: the SCI (the FADT's
 * SCI_INT) masks in the general-purpose event (GPE) blocks and in PM1
 * what is both set and enabled and hands it to the reader, which runs
 * the event's methods (_Lxx, _Exx, the embedded controller's _Qxx) and
 * writes it back.  The kernel owns the GPE and PM1 enable registers,
 * so that the interrupt's masking and a reader's enabling do not undo
 * each other.
 *	read	blocks until an event: "gpe N\n" per GPE, "pm1 0xBITS\n"
 *	write	gpe N enable|disable|clear|ack	(ack: clear, then enable)
 *		pm1 0xBITS enable|disable|clear|ack
 * On the first open the machine goes into ACPI mode if it is not (SCI_EN
 * clear: the firmware's events go to its SMM, no SCI comes; FADT's
 * ACPI_ENABLE written to SMI_CMD, as OpenBSD's acpi_enable), and every GPE
 * is disabled and cleared (aux/acpi enables those it has a method for);
 * PM1's enables stay as the firmware set them.
 * *noacpiev= (plan9.ini) leaves it out: the firmware's events then run
 * nothing, as before.
 *
 * The interrupt's masking follows OpenBSD's acpi_interrupt
 * (sys/dev/acpi/acpi.c,v 1.458):
 *
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
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
enum {
	Ngpe	= 1024,
};

static struct {
	Lock;
	QLock	ql;		/* opening, reading */
	Rendez	r;
	int	sci;		/* -1: no events */
	int	on;		/* interrupt enabled */
	int	pm1[2];		/* PM1a, PM1b event blocks: status, then enable */
	int	pm1len;		/* bytes of each half */
	int	pm1cnt;		/* PM1a's control block: SCI_EN */
	int	pm1bcnt;	/* PM1b's control block (9space: sleep) */
	uvlong	facs;		/* the FACS (9space: the waking vector) */
	int	suspending;	/* 9space: a sleep from sleepevents to wakeevents */
	int	smicmd;		/* SMI_CMD, and what switches to ACPI mode */
	int	acpienable;
	int	gpe[2];		/* GPE0, GPE1 blocks: status, then enable */
	int	gpelen[2];	/* bytes of each half */
	int	gpebase[2];	/* the first GPE of the block */
	ushort	pm1pend;
	uchar	pend[Ngpe/8];
	int	npend;
	ulong	nintr;
} acpiev = { .sci = -1 };

/* the port of gpe's status (en 0) or enable (en 1) byte and its bit; -1 if none */
static int
gpeport(int gpe, int en, int *bit)
{
	int b, i;

	for(b = 0; b < 2; b++){
		if(acpiev.gpe[b] == 0)
			continue;
		i = gpe - acpiev.gpebase[b];
		if(i >= 0 && i < acpiev.gpelen[b]*8){
			*bit = 1 << i%8;
			return acpiev.gpe[b] + en*acpiev.gpelen[b] + i/8;
		}
	}
	return -1;
}

static void
acpiintr(Ureg*, void*)
{
	int b, i, j, g, port;
	uchar s, e, p;
	ushort ps, pe, pp;

	ilock(&acpiev);
	acpiev.nintr++;
	for(b = 0; b < 2; b++){
		if(acpiev.gpe[b] == 0)
			continue;
		for(i = 0; i < acpiev.gpelen[b]; i++){
			port = acpiev.gpe[b] + i;
			s = inb(port);
			e = inb(port + acpiev.gpelen[b]);
			if((p = s & e) == 0)
				continue;
			/* masked until the reader has run its method */
			outb(port + acpiev.gpelen[b], e & ~p);
			for(j = 0; j < 8; j++)
				if(p & 1<<j){
					g = acpiev.gpebase[b] + i*8 + j;
					if(g < Ngpe && (acpiev.pend[g/8] & 1<<g%8) == 0){
						acpiev.pend[g/8] |= 1<<g%8;
						acpiev.npend++;
					}
				}
		}
	}
	for(b = 0; b < 2; b++){
		if(acpiev.pm1[b] == 0)
			continue;
		ps = ins(acpiev.pm1[b]);
		pe = ins(acpiev.pm1[b] + acpiev.pm1len);
		if(acpiev.suspending && (ps & pe & (1<<9 | 1<<8)) != 0){
			/* 9space: the button that wakes the machine (wakeevents) */
			outs(acpiev.pm1[b], ps & pe & (1<<9 | 1<<8));
			ps &= ~(1<<9 | 1<<8);
		}
		if((pp = ps & pe) == 0)
			continue;
		outs(acpiev.pm1[b] + acpiev.pm1len, pe & ~pp);
		acpiev.pm1pend |= pp;
	}
	iunlock(&acpiev);
	wakeup(&acpiev.r);
}

static int
acpievpending(void*)
{
	return acpiev.npend > 0 || acpiev.pm1pend != 0;
}

/* ACPI mode (SCI_EN) by SMI_CMD if the firmware has not set it: at the first open, after a wake */
static void
acpimode(void)
{
	int i;

	if(acpiev.pm1cnt != 0 && (ins(acpiev.pm1cnt) & 1) == 0){
		if(acpiev.smicmd == 0 || acpiev.acpienable == 0)
			print("acpiev: not in ACPI mode, no SMI_CMD to switch\n");
		else {
			outb(acpiev.smicmd, acpiev.acpienable);
			for(i = 0; i < 300 && (ins(acpiev.pm1cnt) & 1) == 0; i++)
				tsleep(&up->sleep, return0, nil, 10);
			if((ins(acpiev.pm1cnt) & 1) == 0)
				print("acpiev: SCI_EN still clear after ACPI_ENABLE\n");
		}
	}
}

static void
acpievon(void)
{
	int b, i;

	qlock(&acpiev.ql);
	if(acpiev.on){
		qunlock(&acpiev.ql);
		return;
	}
	/* ACPI mode: SCI_EN, else no event comes as an SCI */
	acpimode();
	ilock(&acpiev);
	for(b = 0; b < 2; b++)
		for(i = 0; acpiev.gpe[b] != 0 && i < acpiev.gpelen[b]; i++){
			outb(acpiev.gpe[b] + acpiev.gpelen[b] + i, 0);
			outb(acpiev.gpe[b] + i, 0xFF);
		}
	iunlock(&acpiev);
	intrenable(acpiev.sci, acpiintr, nil, BUSUNKNOWN, "acpi");
	acpiev.on = 1;
	qunlock(&acpiev.ql);
}

static long
acpievread(Chan*, void *a, long n, vlong)
{
	char buf[512], *s, *e;	/* (the kernel stack is small; the rest waits for the next read) */
	int g;

	acpievon();
	qlock(&acpiev.ql);
	if(waserror()){
		qunlock(&acpiev.ql);
		nexterror();
	}
	sleep(&acpiev.r, acpievpending, nil);
	s = buf;
	e = buf + sizeof buf;
	ilock(&acpiev);
	if(acpiev.pm1pend != 0){
		s = seprint(s, e, "pm1 %#ux\n", acpiev.pm1pend);
		acpiev.pm1pend = 0;
	}
	for(g = 0; g < Ngpe && acpiev.npend > 0 && e - s > 16; g++)
		if(acpiev.pend[g/8] & 1<<g%8){
			s = seprint(s, e, "gpe %d\n", g);
			acpiev.pend[g/8] &= ~(1<<g%8);
			acpiev.npend--;
		}
	iunlock(&acpiev);
	qunlock(&acpiev.ql);
	poperror();
	if(n > s - buf)
		n = s - buf;
	memmove(a, buf, n);
	return n;
}

static long
acpievwrite(Chan*, void *a, long n, vlong)
{
	char buf[64], *f[4];
	int nf, g, b, port, enport, bit, op;
	uchar v;
	ushort w;

	acpievon();
	if(n >= sizeof buf)
		error(Ebadarg);
	memmove(buf, a, n);
	buf[n] = 0;
	nf = tokenize(buf, f, nelem(f));
	if(nf != 3)
		error(Ebadarg);
	if(strcmp(f[2], "enable") == 0)
		op = 0;
	else if(strcmp(f[2], "disable") == 0)
		op = 1;
	else if(strcmp(f[2], "clear") == 0)
		op = 2;
	else if(strcmp(f[2], "ack") == 0)
		op = 3;
	else
		error(Ebadarg);
	if(strcmp(f[0], "gpe") == 0){
		g = strtol(f[1], nil, 0);
		if((port = gpeport(g, 0, &bit)) < 0)
			error("no such gpe");
		enport = gpeport(g, 1, &bit);
		ilock(&acpiev);
		if(op == 2 || op == 3)
			outb(port, bit);	/* status: 1 clears */
		if(op != 2){
			v = inb(enport);
			outb(enport, op == 1 ? v & ~bit : v | bit);
		}
		iunlock(&acpiev);
	} else if(strcmp(f[0], "pm1") == 0){
		bit = strtoul(f[1], nil, 0) & 0xFFFF;
		ilock(&acpiev);
		for(b = 0; b < 2; b++){
			if(acpiev.pm1[b] == 0)
				continue;
			if(op == 2 || op == 3)
				outs(acpiev.pm1[b], bit);
			if(op != 2){
				w = ins(acpiev.pm1[b] + acpiev.pm1len);
				outs(acpiev.pm1[b] + acpiev.pm1len, op == 1 ? w & ~bit : w | bit);
			}
		}
		iunlock(&acpiev);
	} else
		error(Ebadarg);
	return n;
}

/*
 * 9space: sleeping, S3 (suspend to RAM).  aux/acpi evaluates _PTS and
 * _S3's SLP_TYPa, SLP_TYPb, writes "sleep a b" here and _WAK after it
 * returned.  The processor's part (its state, the interrupt controllers,
 * the way back from the waking vector) is the architecture's
 * archsuspend (pc64/acpisleep.c); it calls acpisleepenter with the
 * waking vector set.  As OpenBSD's acpi_sleep_state and
 * acpi_write_pmreg (dev/acpi/acpi.c, ISC).
 */
char*	(*archsuspend)(char* (*)(void*), void*);	/* nil, or why not */
static int slptyp[2];

/* the firmware jumps to pa in real mode on waking (FACS' waking vector) */
void
acpiwakevector(ulong pa)
{
	static uchar *f;

	if(f == nil)
		f = vmapmem(acpiev.facs, 64);
	if(f == nil)
		return;
	f[12] = pa;
	f[13] = pa>>8;
	f[14] = pa>>16;
	f[15] = pa>>24;
	if(get32(f + 4) >= 32)		/* the 64-bit vector: 0, the real mode one counts */
		memset(f + 24, 0, 8);
}

static char*
acpisleepenter(void*)
{
	ushort v;
	int i;

	/* the wake status cleared, the sleep type, then it with SLP_EN */
	outs(acpiev.pm1[0], 1<<15);
	if(acpiev.pm1[1] != 0)
		outs(acpiev.pm1[1], 1<<15);
	wbinvd();
	v = ins(acpiev.pm1cnt) & ~(7<<10 | 1<<13);
	outs(acpiev.pm1cnt, v | slptyp[0]<<10);
	if(acpiev.pm1bcnt != 0)
		outs(acpiev.pm1bcnt, (ins(acpiev.pm1bcnt) & ~(7<<10 | 1<<13)) | slptyp[1]<<10);
	outs(acpiev.pm1cnt, v | slptyp[0]<<10 | 1<<13);
	if(acpiev.pm1bcnt != 0)
		outs(acpiev.pm1bcnt, (ins(acpiev.pm1bcnt) & ~(7<<10 | 1<<13)) | slptyp[1]<<10 | 1<<13);

	/* asleep now; a machine that did not go comes on here */
	for(i = 0; i < 1000; i++){
		if(ins(acpiev.pm1[0]) & 1<<15)
			break;
		microdelay(100);
	}
	return "the machine did not sleep";
}

static QLock sleeplock;

/*
 * the events around a sleep (as OpenBSD's acpi_sleep_state): the
 * enables saved, every GPE off and its status cleared but the wake
 * GPEs aux/acpi names (from the devices' _PRW), PM1's enables kept
 * (the power button wakes); after the wake the enables as they were
 */
enum { Ngpebyte = 128 };
static struct {
	uchar	gpe[2][Ngpebyte];
	ushort	pm1[2];
} sleepen;

static void
sleepevents(Cmdbuf *cb)
{
	int b, i, port, bit;

	ilock(&acpiev);
	acpiev.suspending = 1;
	for(b = 0; b < 2; b++){
		if(acpiev.pm1[b] != 0)
			sleepen.pm1[b] = ins(acpiev.pm1[b] + acpiev.pm1len);
		for(i = 0; acpiev.gpe[b] != 0 && i < acpiev.gpelen[b] && i < Ngpebyte; i++){
			sleepen.gpe[b][i] = inb(acpiev.gpe[b] + acpiev.gpelen[b] + i);
			outb(acpiev.gpe[b] + acpiev.gpelen[b] + i, 0);
			outb(acpiev.gpe[b] + i, 0xFF);
		}
	}
	for(i = 3; i < cb->nf; i++){
		port = gpeport(strtol(cb->f[i], nil, 0), 1, &bit);
		if(port >= 0)
			outb(port, inb(port) | bit);
	}
	iunlock(&acpiev);
}

static void
wakeevents(void)
{
	int b, i;

	acpimode();
	ilock(&acpiev);
	for(b = 0; b < 2; b++){
		/* the button that woke the machine is no event (as Linux, OpenBSD): its status and WAK_STS cleared */
		if(acpiev.pm1[b] != 0){
			outs(acpiev.pm1[b], 1<<15 | 1<<9 | 1<<8);
			outs(acpiev.pm1[b] + acpiev.pm1len, sleepen.pm1[b]);
		}
		for(i = 0; acpiev.gpe[b] != 0 && i < acpiev.gpelen[b] && i < Ngpebyte; i++)
			outb(acpiev.gpe[b] + acpiev.gpelen[b] + i, sleepen.gpe[b][i]);
	}
	acpiev.suspending = 0;
	iunlock(&acpiev);
}

static long
acpisleepwrite(Chan*, void *a, long n, vlong)
{
	Cmdbuf *cb;
	char *e;

	if(!iseve())
		error(Eperm);
	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	if(cb->nf < 3 || strcmp(cb->f[0], "sleep") != 0)
		error("usage: sleep slptypa slptypb [wakegpe ...]");
	if(archsuspend == nil)
		error("suspend not supported on this architecture");
	slptyp[0] = strtol(cb->f[1], nil, 0) & 7;
	slptyp[1] = strtol(cb->f[2], nil, 0) & 7;

	/* one at a time, on the bootstrap processor: it is the one that wakes */
	qlock(&sleeplock);
	procwired(up, 0);
	sched();
	sleepevents(cb);
	/* 9space: an error in the suspend: the events back, the lock free (they stayed so, and every later sleep hung) */
	if(waserror()){
		wakeevents();
		up->wired = 0;
		qunlock(&sleeplock);
		nexterror();
	}
	e = archsuspend(acpisleepenter, nil);
	poperror();
	wakeevents();
	up->wired = 0;
	qunlock(&sleeplock);
	if(e != nil)
		error(e);
	poperror();
	free(cb);
	return n;
}

/* the FADT's event blocks (I/O ports only: no hardware-reduced ACPI) */
static void
acpievinit(void)
{
	uchar *p;
	Tbl *t;
	int l;

	if(getconf("*noacpiev") != nil || (t = findtable("FACP")) == nil)
		return;
	p = (uchar*)t;
	l = get32(t->len);
	if(l < 116 || (l >= 116 && get32(p + 112) & 1<<20))
		return;
	acpiev.pm1[0] = get32(p + 56);
	acpiev.pm1[1] = get32(p + 60);
	acpiev.pm1len = p[88] / 2;
	acpiev.smicmd = get32(p + 48);
	acpiev.acpienable = p[52];
	acpiev.pm1cnt = get32(p + 64);
	if(acpiev.pm1cnt == 0 && l >= 184 && p[172] == IoSpace)
		acpiev.pm1cnt = get64(p + 176);
	acpiev.gpe[0] = get32(p + 80);
	acpiev.gpe[1] = get32(p + 84);
	acpiev.gpelen[0] = p[92] / 2;
	acpiev.gpelen[1] = p[93] / 2;
	acpiev.gpebase[0] = 0;
	acpiev.gpebase[1] = p[94];
	/* the extended (GAS) blocks, if the old fields are empty: ports only */
	if(acpiev.pm1[0] == 0 && l >= 160 && p[148] == IoSpace)
		acpiev.pm1[0] = get64(p + 152);
	if(acpiev.pm1[1] == 0 && l >= 172 && p[160] == IoSpace)
		acpiev.pm1[1] = get64(p + 164);
	if(acpiev.gpe[0] == 0 && l >= 232 && p[220] == IoSpace)
		acpiev.gpe[0] = get64(p + 224);
	if(acpiev.gpe[1] == 0 && l >= 244 && p[232] == IoSpace)
		acpiev.gpe[1] = get64(p + 236);
	if(acpiev.gpelen[0] == 0)
		acpiev.gpe[0] = 0;
	if(acpiev.gpelen[1] == 0)
		acpiev.gpe[1] = 0;
	if(acpiev.pm1len == 0)
		acpiev.pm1[0] = acpiev.pm1[1] = 0;
	if(acpiev.pm1[0] == 0 && acpiev.gpe[0] == 0)
		return;
	acpiev.sci = get16(p + 46);
	addarchfile("acpiev", 0600, acpievread, acpievwrite);

	/* 9space: sleeping (S3): PM1b's control block, the FACS */
	acpiev.pm1bcnt = get32(p + 68);
	if(acpiev.pm1bcnt == 0 && l >= 196 && p[184] == IoSpace)
		acpiev.pm1bcnt = get64(p + 188);
	acpiev.facs = get32(p + 36);
	if(acpiev.facs == 0 && l >= 140)
		acpiev.facs = get64(p + 132);
	if(acpiev.pm1cnt != 0 && acpiev.facs != 0)
		addarchfile("acpisleep", 0200, nil, acpisleepwrite);
}

static void
acpiinit(void)
{
	Tbl *t;
	Apic *a;
	uchar *s, *p, *e;
	ulong lapicbase;
	int i, c;

	amlinit();

	/* load DSDT */
	if((t = findtable("DSDT")) != nil){
		amlintmask = (~0ULL) >> (t->rev <= 1)*32;
		amlload(t->data, tbldlen(t));
	}

	/* load SSDT, there can be multiple tables */
	for(i=0; i<ntblmap; i++){
		t = tblmap[i];
		if(memcmp(t->sig, "SSDT", 4) == 0)
			amlload(t->data, tbldlen(t));
	}

	/* set APIC mode */
	amleval(amlwalk(amlroot, "_PIC"), "i", 1, nil);

	t = findtable("APIC");
	if(t == nil)
		panic("acpiinit: no MADT (APIC) table");

	s = t->data;
	e = s + tbldlen(t);
	lapicbase = get32(s);

	s += 8;
	for(p = s; p < e; p += c){
		c = p[1];
		if(c < 2 || (p+c) > e)
			break;
		switch(*p){
		case 0x00:	/* Processor Local APIC */
		case 0x09:	/* x2APIC */
			if((a = xalloc(sizeof(Apic))) == nil)
				panic("acpiinit: no memory for Apic");
			a->type = PcmpPROCESSOR;
			a->paddr = lapicbase;
			if(*p == 0x09){
				a->apicno = get32(p+4);
				a->flags = get32(p+8) & PcmpEN;
				a->x2apic = get32(p+12);
			} else {
				a->apicno = p[3];
				if(a->apicno > MaxAPICNO)
					a->apicno = -1;
				a->flags = p[4] & PcmpEN;
				a->x2apic = -1;
			}
			/* skip disabled processors or duplicate apic id's */
			if((a->flags & PcmpEN) == 0
			|| a->apicno == -1
			|| mpgetapic(mplapic, a->apicno) != nil){
				xfree(a);
				break;
			}

			/*
			 * platform firmware should list the boot processor
			 * as the first processor entry in the MADT
			 */
			if(mplapic == nil)
				a->flags |= PcmpBP;

			*mplapicp = a, mplapicp = &a->next;
			break;
		case 0x01:	/* I/O APIC */
			if(p[2] > MaxAPICNO || mpgetapic(mpioapic, p[2]) != nil)
				break;
			if((a = xalloc(sizeof(Apic))) == nil)
				panic("acpiinit: no memory for io Apic");
			a->type = PcmpIOAPIC;
			a->apicno = p[2];
			a->paddr = get32(p+4);
			a->gsibase = get32(p+8);
			a->flags = PcmpEN;
			*mpioapicp = a, mpioapicp = &a->next;

			ioapicinit(a);
			break;
		}
	}

	/*
	 * need 2nd pass as vbox puts interrupt overrides
	 * *before* the ioapic entries (!)
	 */
	for(p = s; p < e; p += c){
		c = p[1];
		if(c < 2 || (p+c) > e)
			break;
		switch(*p){
		case 0x02:	/* Interrupt Source Override */
			addirq(get32(p+4), BusISA, 0, p[3], get16(p+8), nil);
			break;
		case 0x03:	/* Non-maskable Interrupt Source (NMI) */
			if((a = findioapic(get32(p+4), &i)) != nil)
				addnmi(a, i, get16(p+3));
			break;
		case 0x04:	/* Local APIC NMI Structure */
			i = p[2];
			for(a = mplapic; a != nil; a = a->next){
				if(i == 0xFF || i == a->apicno)
					addnmi(a, p[5], get16(p+3));
			}
			break;
		case 0x05:	/* Local APIC Address Override Structure */
			for(a = mplapic; a != nil; a = a->next)
				a->paddr = get64(p+4);
			break;
		case 0x0A:	/* Local x2APIC NMI Structure */
			i = get32(p+4);	/* Processor UID */
			for(a = mplapic; a != nil; a = a->next){
				if(i == -1 || i == a->x2apic)
					addnmi(a, p[8], get16(p+2));
			}
			break;
		}
	}

	/* find embedded controller */
	amlenum(amlroot, "_HID", enumec, nil);

	/* 9space: AMD's GPIO controller (#γ), its I2C controllers (i2c(3)) */
	amlenum(amlroot, "_HID", enumgpio, nil);
	amlenum(amlroot, "_HID", enumi2c, nil);

	/* look for legacy ISA interrupts 0-15 */
	if(getconf("*noenumcrs") == nil)
		amlenum(amlroot, "_CRS", enumcrs, nil);

	/* add identity mapped legacy ISA interrupts */
	for(i=0; i<16; i++)
		addirq(i, BusISA, 0, i, 0, nil);

	/* look for PCI interrupt mappings */
	amlenum(amlroot, "_PRT", enumprt, nil);

	/* free the AML interpreter */
	amlexit();

	/*
	 * Ininitalize local APIC and start application processors.
	 */
	mpinit();
}

static void
acpireset(void)
{
	uchar *p;
	Tbl *t;

	/* stop application processors */
	mpshutdown();

	/* locate and write platform reset register */
	while((t = findtable("FACP")) != nil){
		if(get32(t->len) <= 128)
			break;
		p = (uchar*)t;
		if((get32(p + 112) & (1<<10)) == 0)
			break;
		if(p[116+0] != IoSpace)
			break;
		outb(get32(p+116+4), p[128]);
		break;
	}

	/* acpi shutdown failed, try generic reset */
	archreset();
}

static int identify(void);
extern int i8259irqno(int, int);
extern void i8253init(void);

extern int hpetprobe(uvlong);
extern void hpetinit(void);
extern uvlong hpetread(uvlong*);

PCArch archacpi = {
.id=		"ACPI",	
.ident=		identify,
.reset=		acpireset,
.intrinit=	acpiinit,
.intrassign=	mpintrassign,
.intrspurious=	mpintrspurious,
.intrirqno=	i8259irqno,
.intron=	lapicintron,
.introff=	lapicintroff,
.clockinit=	i8253init,
.fastclock=	i8253read,
.timerset=	lapictimerset,
};

static long
readtbls(Chan*, void *v, long n, vlong o)
{
	int i, l, m;
	uchar *p;
	Tbl *t;

	maptables();

	p = v;
	for(i=0; n > 0 && i < ntblmap; i++){
		t = tblmap[i];
		l = get32(t->len);
		if(o >= l){
			o -= l;
			continue;
		}
		m = l - o;
		if(m > n)
			m = n;
		memmove(p, (uchar*)t + o, m);
		p += m;
		n -= m;
		o = 0;
	}
	return p - (uchar*)v;
}

static int
identify(void)
{
	uvlong v;
	char *cp;
	Tbl *t;

	if((cp = getconf("*acpi")) == nil || *cp == '\0')
		cp = "1";	/* search for rsd by default */
	v = (uintptr)strtoull(cp, nil, 16);
	if(v <= 1)
		rsd = rsdsearch();
	else {
		memreserve(v, sizeof(Rsd));
		rsd = vmapmem(v, sizeof(Rsd));	/* GCC port: memory, not registers (vmapmem) */
	}
	if(rsd == nil)
		return 1;
	if(checksum(rsd, 20) && checksum(rsd, 36))
		return 1;
	maptables();
	addarchfile("acpitbls", 0444, readtbls, nil);
	addarchfile("acpimem", 0600, readmem, writemem);
	acpievinit();	/* 9space: #P/acpiev */
	mcfginit();	/* 9space: PCI Express's extended configuration space */
	if(v == 0 || findtable("APIC") == nil)
		return 1;
	if((cp = getconf("*nomp")) != nil && strcmp(cp, "0") != 0)
		return 1;
	if(getconf("*nohpet") == nil
	&& (t = findtable("HPET")) != nil
	&& ((uchar*)t)[40] == 0
	&& hpetprobe(get64((uchar*)t+44)) == 0){
		archacpi.clockinit = hpetinit;
		archacpi.fastclock = hpetread;
	}
	if(m->havetsc && getconf("*notsc") == nil)
		archacpi.fastclock = tscticks;

	return 0;
}

/*
 * 9space: ACPI's MCFG table: the memory mapped configuration space
 * (ECAM) of PCI segment 0 (pcipc.c), as OpenBSD's acpimcfg.c reads it:
 * after 8 reserved bytes entries of 16: base[8] segment[2] start bus[1]
 * end bus[1] reserved[4].  *nomcfg= leaves it.
 */
static void
mcfginit(void)
{
	Tbl *t;
	uchar *p, *e;

	if(getconf("*nomcfg") != nil || (t = findtable("MCFG")) == nil)
		return;
	p = t->data + 8;
	e = t->data + tbldlen(t);
	for(; p + 16 <= e; p += 16)
		if(get16(p+8) == 0)
			pcimcfginit(get64(p), p[10], p[11]);
}

static int
readpcicfg(Amlio *io, void *data, int n, int offset)
{
	ulong r, x;
	Pcidev *p;
	uchar *a;
	int i;

	a = data;
	p = io->aux;
	if(p == nil)
		return -1;
	offset += io->off;
	if(offset > 256)
		return 0;
	if(n+offset > 256)
		n = 256-offset;
	r = offset;
	if(!(r & 3) && n == 4){
		x = pcicfgr32(p, r);
		PBIT32(a, x);
		return 4;
	}
	if(!(r & 1) && n == 2){
		x = pcicfgr16(p, r);
		PBIT16(a, x);
		return 2;
	}
	for(i = 0; i <  n; i++){
		x = pcicfgr8(p, r);
		PBIT8(a, x);
		a++;
		r++;
	}
	return i;
}

static int
readec(Amlio *io, void *data, int n, int off)
{
	int port, v;
	uchar *p;

	USED(io);
	if(off < 0 || off >= 256)
		return 0;
	if(off+n > 256)
		n = 256 - off;
	p = data;
	for(port = off; port < off+n; port++){
		if((v = ecread(port)) < 0)
			break;
		*p++ = v;
	}
	return n;
}

static int
writeec(Amlio *io, void *data, int n, int off)
{
	int port;
	uchar *p;

	USED(io);
	if(off < 0 || off+n > 256)
		return -1;
	p = data;
	for(port = off; port < off+n; port++)
		if(ecwrite(port, *p++) < 0)
			break;
	return n;
}

static int
writepcicfg(Amlio *io, void *data, int n, int offset)
{
	ulong r, x;
	Pcidev *p;
	uchar *a;
	int i;

	a = data;
	p = io->aux;
	if(p == nil)
		return -1;
	offset += io->off;
	if(offset > 256)
		return 0;
	if(n+offset > 256)
		n = 256-offset;
	r = offset;
	if(!(r & 3) && n == 4){
		x = GBIT32(a);
		pcicfgw32(p, r, x);
		return 4;
	}
	if(!(r & 1) && n == 2){
		x = GBIT16(a);
		pcicfgw16(p, r, x);
		return 2;
	}
	for(i = 0; i <  n; i++){
		x = GBIT8(a);
		pcicfgw8(p, r, x);
		a++;
		r++;
	}
	return i;
}

static int
readioport(Amlio *io, void *data, int len, int port)
{
	uchar *a;

	a = data;
	port += io->off;
	switch(len){
	case 4:
		PBIT32(a, inl(port));
		return 4;
	case 2:
		PBIT16(a, ins(port));
		return 2;
	case 1:
		PBIT8(a, inb(port));
		return 1;
	}
	return -1;
}

static int
writeioport(Amlio *io, void *data, int len, int port)
{
	uchar *a;

	a = data;
	port += io->off;
	switch(len){
	case 4:
		outl(port, GBIT32(a));
		return 4;
	case 2:
		outs(port, GBIT16(a));
		return 2;
	case 1:
		outb(port, GBIT8(a));
		return 1;
	}
	return -1;
}

int
amlmapio(Amlio *io)
{
	int tbdf;
	Pcidev *pdev;
	char tag[64];

	switch(io->space){
	default:
		print("amlmapio: address space %x not implemented\n", io->space);
		break;
	case MemSpace:
		if(memcheck(io->off, io->len) != io->len){
			print("amlmapio: [%#p-%#p) overlaps usable memory\n",
				(uintptr)io->off, (uintptr)(io->off+io->len));
			break;
		}
		if((io->va = vmap(io->off, io->len)) == nil){
			print("amlmapio: vmap failed\n");
			break;
		}
		return 0;
	case IoSpace:
		snprint(tag, sizeof(tag), "%N", io->name);
		if(ioalloc(io->off, io->len, 0, tag) < 0){
			print("amlmapio: ioalloc failed\n");
			break;
		}
		io->read = readioport;
		io->write = writeioport;
		return 0;
	case PcicfgSpace:
		if((tbdf = pciaddr(io->name)) < 0){
			print("amlmapio: no address\n");
			break;
		}
		if((pdev = pcimatchtbdf(tbdf)) == nil){
			print("amlmapio: no device %T\n", tbdf);
			break;
		}
		io->aux = pdev;
		io->read = readpcicfg;
		io->write = writepcicfg;
		return 0;
	case EbctlSpace:
		io->read = readec;
		io->write = writeec;
		return 0;
	}
	print("amlmapio: mapping %N failed\n", io->name);
	return -1;
}

void
amlunmapio(Amlio *io)
{
	char tag[64];

	switch(io->space){
	case MemSpace:
		vunmap(io->va, io->len);
		break;
	case IoSpace:
		snprint(tag, sizeof(tag), "%N", io->name);
		iofreetag(io->off, tag);
		break;
	}
}

void*
amlalloc(int n){
	void *p;

	if((p = malloc(n)) == nil)
		panic("amlalloc: no memory");
	memset(p, 0, n);
	return p;
}

void
amlfree(void *p){
	free(p);
}

void
amldelay(uvlong us)
{
	while(us > 1000){
		us -= 1000;
		microdelay(1000);
	}
	microdelay(us);
}

uvlong
amltime(void)
{
	uvlong hz;

	/* can't use tod yet, use fastticks() */
	return (fastticks(&hz) * 10000000ULL) / hz;
}
