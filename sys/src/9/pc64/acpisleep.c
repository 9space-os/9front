/*
 * 9space: suspend to RAM (ACPI S3), the processor's part (archacpi.c's
 * #P/acpisleep calls archsuspend).  The bootstrap processor saves what
 * the sleep loses: its control registers and model-specific registers,
 * the I/O APICs' redirection entries, every PCI function's
 * configuration header and MSI capability, the TSC; it points the
 * FACS' waking vector at the AP bootstrap code (APBOOTSTRAP), with a
 * page table of its own whose low half maps the kernel, and enters the
 * sleep.  The firmware wakes it in real mode there; apbootstrap takes it
 * to long mode and calls resume() on a stack of its own (the top of
 * Mach 0 holds schedinit's frame), which puts everything back and
 * returns from the setlabel in cpususpend.
 *
 * The other processors first: a kproc wired to each saves its
 * processor's state the same way and halts it.  After the wake (or when
 * one does not park and the suspend is given up) the bootstrap
 * processor starts each with INIT and STARTUP at apbootstrap, as at
 * boot, but to resume() and that processor's saved state: its kproc
 * returns from its setlabel, takes the TSC from processor 0, sets its
 * local APIC and timer again and exits.
 *
 * As OpenBSD's acpi_sleep_cpu, acpi_savecpu and acpi_resume_cpu
 * (sys/arch/amd64/amd64/acpi_machdep.c, acpi_wakecode.S) and
 * acpi_sleep_state (sys/dev/acpi/acpi.c):
 *	Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 *	Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
 *	Copyright (c) 2008, 2009 Mike Larkin <mlarkin@openbsd.org>
 *	(ISC licence; this is written anew for 9front's kernel)
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"
#include	"../port/pci.h"
#include	"mp.h"

/* mp.c includes apbootstrap.i too, and mk's %.i (portmkfile) is not static: this copy under a name of its own */
#define	apbootstrap	sleepapbootstrap
#include	"apbootstrap.i"

enum {
	Patmsr	= 0x277,
	Tscmsr	= 0x10,
	Nhdr	= 16,		/* dwords of a configuration header */
	Nmsi	= 6,		/* dwords of an MSI capability */
	Wakestk	= MACHSIZE,	/* resume's stack (apbootstrap: a Mach's size) */
};

extern char* (*archsuspend)(char* (*)(void*), void*);
extern void acpiwakevector(ulong);
extern void hpetresume(void);
extern void i8253init(void);

typedef struct Pcisave Pcisave;
struct Pcisave {
	Pcidev	*p;
	u32int	hdr[Nhdr];
	int	msi;		/* the MSI capability's offset, 0 if none */
	u32int	msicap[Nmsi];
};

/* a processor's state, each its own */
typedef struct Cpusave Cpusave;
struct Cpusave {
	u64int	cr0, cr4, xcr0;
	vlong	efer, star, lstar, sfmask, pat, fsbase, gsbase, kgsbase, tsc;
	Label	label;
	Proc	*up;
	int	machno;
	Apic	*apic;
	int	parked;		/* the AP saved its state and halts */
	int	awake;		/* the AP is back */
};

static Cpusave cpusv[MAXMACH];

static struct {
	Pcisave	*pci;
	int	npci;
	u32int	*rdt;		/* each I/O APIC's entries, lo and hi */
	int	nrdt;
	uintptr	*pml4;		/* for apbootstrap: the kernel in the low half */
	uchar	*stack;		/* resume's */
	ulong	wakes;
} sv;

/* the AP bootstrap code's parameters (apbootstrap.S): vector, pml4, apic, mach, efer */
static uintptr*
apbootp(void)
{
	return (uintptr*)(APBOOTSTRAP+0x08);
}

static void
pcisave(void)
{
	Pcidev *p;
	Pcisave *s;
	int i, n;

	n = 0;
	for(p = nil; (p = pcimatch(p, 0, 0)) != nil; )
		n++;
	free(sv.pci);
	sv.pci = malloc(n * sizeof(Pcisave));
	if(sv.pci == nil)
		error(Enomem);
	sv.npci = 0;
	for(p = nil; (p = pcimatch(p, 0, 0)) != nil; ){
		s = &sv.pci[sv.npci++];
		s->p = p;
		for(i = 0; i < Nhdr; i++)
			s->hdr[i] = pcicfgr32(p, 4*i);
		s->msi = pcicap(p, PciCapMSI);
		if(s->msi < 0)
			s->msi = 0;
		if(s->msi)
			for(i = 0; i < Nmsi; i++)
				s->msicap[i] = pcicfgr32(p, s->msi + 4*i);
	}
}

/*
 * the headers back: everything but the identity, the command register
 * last (BARs before decoding is on); then the MSI capability
 */
static void
pcirestore(void)
{
	Pcisave *s;
	int i, j;

	for(j = 0; j < sv.npci; j++){
		s = &sv.pci[j];
		for(i = Nhdr-1; i >= 2; i--)
			pcicfgw32(s->p, 4*i, s->hdr[i]);
		if(s->msi)
			for(i = Nmsi-1; i >= 0; i--)
				pcicfgw32(s->p, s->msi + 4*i, s->msicap[i]);
		pcicfgw32(s->p, 4, s->hdr[1] & 0xFFFF);
	}
}

static void
ioapicsave(void)
{
	Apic *a;
	int n, v, hi, lo;

	n = 0;
	for(a = mpioapic; a != nil; a = a->next)
		n += a->mre + 1;
	free(sv.rdt);
	sv.rdt = malloc(2 * n * sizeof(u32int));
	if(sv.rdt == nil)
		error(Enomem);
	sv.nrdt = 0;
	for(a = mpioapic; a != nil; a = a->next)
		for(v = 0; v <= a->mre; v++){
			ioapicrdtr(a, v, &hi, &lo);
			sv.rdt[sv.nrdt++] = lo;
			sv.rdt[sv.nrdt++] = hi;
		}
}

static void
ioapicrestore(void)
{
	Apic *a;
	int v, i;

	/* as saved (not ioapicinit: it maps the registers anew) */
	i = 0;
	for(a = mpioapic; a != nil; a = a->next){
		for(v = 0; v <= a->mre && i+1 < sv.nrdt; v++, i += 2)
			ioapicrdtw(a, v, sv.rdt[i+1], sv.rdt[i]);
	}
}

/*
 * every device's power function (port's devpower is the stub of those
 * without one: it only says Eperm); off in the reverse order
 */
static void
powerall(int on)
{
	int i, n;

	for(n = 0; devtab[n] != nil; n++)
		;
	for(i = 0; i < n; i++){
		Dev *d = devtab[on ? i : n-1-i];
		if(d->power != nil && d->power != devpower)
			d->power(on);
	}
}

static Apic*
machapic(int machno)
{
	Apic *a;

	for(a = mplapic; a != nil; a = a->next)
		if(a->online && a->machno == machno)
			return a;
	return nil;
}

/* the page table apbootstrap starts with: the kernel's, and it in the low half too */
static void
mkwakepml4(void)
{
	uintptr *pdp0, v;
	uchar *p;

	if(sv.pml4 != nil)
		return;
	p = xspanalloc(2*PTSZ + Wakestk, BY2PG, 0);
	sv.pml4 = (uintptr*)p;
	pdp0 = (uintptr*)(p + PTSZ);
	sv.stack = p + 2*PTSZ;
	memset(sv.pml4, 0, PTSZ);
	memset(pdp0, 0, PTSZ);
	*mmuwalk(sv.pml4, KZERO, 3, 0) = *mmuwalk(MACHP(0)->pml4, KZERO, 3, 0);
	for(v = VMAP; v < VMAP+VMAPSIZE; v += PGLSZ(3)){
		mmuwalk(MACHP(0)->pml4, v, 2, 1);
		*mmuwalk(sv.pml4, v, 3, 0) = *mmuwalk(MACHP(0)->pml4, v, 3, 0);
	}
	sv.pml4[0] = PADDR(pdp0) | PTEWRITE|PTEVALID;
	pdp0[0] = *mmuwalk(sv.pml4, KZERO, 2, 0);
}

/* lgdt and lidt take a 10-byte pointer: limit, then base (as mmu.c's loadptr) */
static void
loaddesc(u16int lim, uintptr off, void (*load)(void*))
{
	u64int b[2], *o;
	u16int *s;

	o = &b[1];
	s = ((u16int*)o)-1;
	*s = lim;
	*o = off;
	(*load)(s);
}

static void
savecpu(Cpusave *c)
{
	c->cr0 = getcr0();
	c->cr4 = getcr4();
	c->xcr0 = m->xcr0;
	rdmsr(Efer, &c->efer);
	rdmsr(Star, &c->star);
	rdmsr(Lstar, &c->lstar);
	rdmsr(Sfmask, &c->sfmask);
	rdmsr(FSbase, &c->fsbase);
	rdmsr(GSbase, &c->gsbase);
	rdmsr(KernelGSbase, &c->kgsbase);
	if(m->cpuiddx & Pat)
		rdmsr(Patmsr, &c->pat);
	c->up = up;
	c->machno = m->machno;
}

/*
 * on the wake stack, from apbootstrap's long mode (its argument is the
 * processor's Cpusave): m and up are apbootstrap's (its stack's Mach,
 * nil); the processor as it was saved, then back to its setlabel
 */
static void
resume(void *arg)
{
	Cpusave *c;
	uintptr x;

	c = arg;
	m = MACHP(c->machno);
	putcr3(PADDR(m->pml4));

	/* the TSS' descriptor not busy again, then GDT, IDT, TR (as mmuinit) */
	x = (uintptr)m->tss;
	m->gdt[TSSSEG+0].d0 = (x<<16)|(sizeof(Tss)-1);
	m->gdt[TSSSEG+0].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;
	m->gdt[TSSSEG+1].d0 = x>>32;
	m->gdt[TSSSEG+1].d1 = 0;
	loaddesc(NGDT*sizeof(Segdesc)-1, (uintptr)m->gdt, lgdt);
	loaddesc(sizeof(Segdesc)*512-1, (uintptr)IDTADDR, lidt);
	ltr(TSSSEL);

	putcr4(c->cr4);
	putcr0(c->cr0);
	if(c->xcr0 != 0)
		putxcr0(c->xcr0);
	wrmsr(Efer, c->efer);
	wrmsr(Star, c->star);
	wrmsr(Lstar, c->lstar);
	wrmsr(Sfmask, c->sfmask);
	wrmsr(FSbase, c->fsbase);
	wrmsr(GSbase, c->gsbase);
	wrmsr(KernelGSbase, c->kgsbase);
	if(m->cpuiddx & Pat)
		wrmsr(Patmsr, c->pat);
	if(c->machno == 0)
		wrmsr(Tscmsr, c->tsc);	/* time stood still asleep: the RTC sets it on */

	/* the TSS is as it was (RAM kept): its stack is up's */
	up = c->up;
	gotolabel(&c->label);
}

/* apbootstrap to resume(c) on the wake stack */
static void
setwake(Cpusave *c)
{
	uintptr *ap;

	memmove((void*)APBOOTSTRAP, apbootstrap, sizeof(apbootstrap));
	ap = apbootp();
	ap[0] = (uintptr)resume;
	ap[1] = PADDR(sv.pml4);
	ap[2] = (uintptr)c;
	ap[3] = (uintptr)sv.stack;	/* its "Mach": apbootstrap's stack is its top */
	ap[4] |= (uintptr)m->havenx<<11;
	coherence();
}

/* an AP's kproc: on its processor, saves it and halts it until restartaps */
static void
appark(void *arg)
{
	Cpusave *c;
	int s;

	c = arg;
	procwired(up, c->machno);
	sched();
	fpuprocsave(up);
	s = splhi();
	savecpu(c);
	if(setlabel(&c->label) == 0){
		wbinvd();
		/*
		 * 9space: parked only if parkaps has not given up on it
		 * (0 to -1 at its timeout): one that came late parked after
		 * restartaps and stayed halted for ever (a processor lost)
		 */
		if(cas(&c->parked, 0, 1)){
			coherence();
			cpuhalt();		/* interrupts off: only INIT ends it */
		}
		splx(s);
		up->wired = 0;
		pexit("", 1);
	}

	/* restarted: the TSC from processor 0, the local APIC, its timer */
	syncclock();
	lapicinit(c->apic);
	lapiconline();
	timerset(0);
	c->awake = 1;
	coherence();
	splx(s);
	up->wired = 0;
	pexit("", 1);
}

/*
 * 9space: the APs not parked yet given up (parked 0 to -1): they will not
 * park now; restartaps restarts the parked ones (1)
 */
static void
parkabort(void)
{
	int i;

	for(i = 1; i < conf.nmach; i++)
		cas(&cpusv[i].parked, 0, -1);
	coherence();
}

static int
parkaps(void)
{
	Cpusave *c;
	int i, ms;

	for(i = 1; i < conf.nmach; i++){
		c = &cpusv[i];
		c->machno = i;
		c->apic = machapic(i);
		c->parked = c->awake = 0;
		if(c->apic == nil)
			return -1;
	}
	for(i = 1; i < conf.nmach; i++)
		kproc("apsuspend", appark, &cpusv[i]);
	for(ms = 0; ms < 5000; ms += 10){
		for(i = 1; i < conf.nmach; i++)
			if(!*(volatile int*)&cpusv[i].parked)
				break;
		if(i == conf.nmach)
			return 0;
		tsleep(&up->sleep, return0, 0, 10);
	}
	parkabort();
	return -1;
}

/* the parked APs back, one after the other (they share the wake stack) */
static void
restartaps(void)
{
	Cpusave *c;
	uchar *p;
	int i, t, s;

	s = splhi();
	for(i = 1; i < conf.nmach; i++){
		c = &cpusv[i];
		if(c->parked != 1)	/* 9space: -1, given up: it did not halt */
			continue;
		c->parked = 0;
		setwake(c);

		/* as mpstartap: the warm-reset vector, then INIT and STARTUP */
		p = KADDR(0x467);
		*p++ = PADDR(APBOOTSTRAP);
		*p++ = PADDR(APBOOTSTRAP)>>8;
		*p++ = 0;
		*p = 0;
		coherence();
		nvramwrite(0x0F, 0x0A);
		lapicstartap(c->apic, PADDR(APBOOTSTRAP));
		for(t = 0; t < 1000; t++){
			if(arch->fastclock == tscticks)
				cycles(&m->tscticks);	/* for the AP's syncclock() */
			if(*(volatile int*)&c->awake)
				break;
			delay(1);
		}
		nvramwrite(0x0F, 0x00);
		if(!c->awake)
			print("suspend: cpu%d did not come back\n", i);
	}
	splx(s);
}

static char*
cpususpend(char* (*enter)(void*), void *arg)
{
	Cpusave *c;
	Apic *bsp;
	char *r;
	int s;

	if(m->machno != 0 || (bsp = machapic(0)) == nil)
		return "suspend: not on the bootstrap processor";
	if(waserror())
		return "suspend: no memory";
	mkwakepml4();
	pcisave();
	pciroutingsave();
	ioapicsave();
	poperror();

	/*
	 * 9space: an error on the way down (a note interrupted parkaps'
	 * or a device's sleep) undoes it: the processors back, the devices
	 * on (they stayed halted and off)
	 */
	if(waserror()){
		parkabort();
		restartaps();
		powerall(1);
		nexterror();
	}
	powerall(0);
	if(parkaps() < 0){
		poperror();
		restartaps();
		powerall(1);
		return "suspend: a processor did not stop";
	}
	poperror();
	fpuprocsave(up);
	s = splhi();

	c = &cpusv[0];
	savecpu(c);
	setwake(c);
	acpiwakevector(PADDR(APBOOTSTRAP));

	if(setlabel(&c->label) == 0){
		cycles((uvlong*)&c->tsc);
		wbinvd();
		r = enter(arg);		/* returns only if the machine did not sleep */
		splx(s);
		restartaps();
		powerall(1);
		return r;
	}

	/* awake, in resume's state: the interrupts and the devices, then the clocks */
	sv.wakes++;
	outb(0x21, 0xFF);		/* the 8259s masked: the APICs route (not i8259init: it allocates the ports) */
	outb(0xA1, 0xFF);
	lapicinit(bsp);
	lapiconline();
	ioapicrestore();
	if(arch->clockinit == i8253init)
		i8253init();
	hpetresume();
	pciroutingrestore();
	pcirestore();
	todset((vlong)rtctime()*1000000000LL, 0, 0);
	timerset(0);			/* the local APIC's timer anew: the next tick sets the next */
	restartaps();
	splx(s);
	powerall(1);
	print("suspend: awake (%lud)\n", sv.wakes);
	return nil;
}

void
acpisleeplink(void)
{
	archsuspend = cpususpend;
}
