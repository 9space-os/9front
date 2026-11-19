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
 * As OpenBSD's acpi_sleep_cpu, acpi_savecpu and acpi_resume_cpu
 * (sys/arch/amd64/amd64/acpi_machdep.c, acpi_wakecode.S) and
 * acpi_sleep_state (sys/dev/acpi/acpi.c):
 *	Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 *	Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
 *	Copyright (c) 2008, 2009 Mike Larkin <mlarkin@openbsd.org>
 *	(ISC licence; this is written anew for 9front's kernel)
 *
 * One processor only for now: with more, it says so and does nothing.
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

#include	"apbootstrap.i"	/* mp.c's is static */

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

static struct {
	u64int	cr0, cr4, xcr0;
	vlong	efer, star, lstar, sfmask, pat, fsbase, gsbase, kgsbase, tsc;
	Label	label;
	Proc	*up;
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
bspapic(void)
{
	Apic *a;

	for(a = mplapic; a != nil; a = a->next)
		if(a->online && a->machno == 0)
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

/*
 * on the wake stack, from apbootstrap's long mode: m and up are apbootstrap's
 * (its stack's Mach, nil); the processor as cpususpend left it, then back there
 */
static void
resume(void*)
{
	uintptr x;

	m = MACHP(0);
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

	putcr4(sv.cr4);
	putcr0(sv.cr0);
	if(sv.xcr0 != 0)
		putxcr0(sv.xcr0);
	wrmsr(Efer, sv.efer);
	wrmsr(Star, sv.star);
	wrmsr(Lstar, sv.lstar);
	wrmsr(Sfmask, sv.sfmask);
	wrmsr(FSbase, sv.fsbase);
	wrmsr(GSbase, sv.gsbase);
	wrmsr(KernelGSbase, sv.kgsbase);
	if(m->cpuiddx & Pat)
		wrmsr(Patmsr, sv.pat);
	wrmsr(Tscmsr, sv.tsc);		/* time stood still asleep: the RTC sets it on */

	/* the TSS is as it was (RAM kept): its stack is up's */
	sv.wakes++;
	up = sv.up;
	gotolabel(&sv.label);
}

static char*
cpususpend(char* (*enter)(void*), void *arg)
{
	Apic *bsp;
	uintptr *ap;
	char *r;
	int s;

	if(conf.nmach > 1)
		return "suspend: one processor only so far (*ncpu=1)";
	if(m->machno != 0 || (bsp = bspapic()) == nil)
		return "suspend: not on the bootstrap processor";
	if(waserror())
		return "suspend: no memory";
	mkwakepml4();
	pcisave();
	pciroutingsave();
	ioapicsave();
	poperror();

	powerall(0);
	fpuprocsave(up);
	s = splhi();

	sv.cr0 = getcr0();
	sv.cr4 = getcr4();
	sv.xcr0 = m->xcr0;
	rdmsr(Efer, &sv.efer);
	rdmsr(Star, &sv.star);
	rdmsr(Lstar, &sv.lstar);
	rdmsr(Sfmask, &sv.sfmask);
	rdmsr(FSbase, &sv.fsbase);
	rdmsr(GSbase, &sv.gsbase);
	rdmsr(KernelGSbase, &sv.kgsbase);
	if(m->cpuiddx & Pat)
		rdmsr(Patmsr, &sv.pat);
	sv.up = up;

	/* apbootstrap at the waking vector, to resume() on the wake stack */
	memmove((void*)APBOOTSTRAP, apbootstrap, sizeof(apbootstrap));
	ap = apbootp();
	ap[0] = (uintptr)resume;
	ap[1] = PADDR(sv.pml4);
	ap[2] = 0;
	ap[3] = (uintptr)sv.stack;	/* its "Mach": apbootstrap's stack is its top */
	ap[4] |= (uintptr)m->havenx<<11;
	acpiwakevector(PADDR(APBOOTSTRAP));

	if(setlabel(&sv.label) == 0){
		cycles((uvlong*)&sv.tsc);
		wbinvd();
		r = enter(arg);		/* returns only if the machine did not sleep */
		splx(s);
		powerall(1);
		return r;
	}

	/* awake, in resume's state: the interrupts and the devices, then the clocks */
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
