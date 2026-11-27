/*
 * 9space: deeper idle states.  idlehands' MWAIT asks for C1 only (hint
 * 0): the cores never reach C3/C6 and the package none of its C-states
 * (the X1 Carbon's Broadwell drew 2.3 W idle, aux/power(8)).  Here the
 * MWAIT hints come from CPUID leaf 5 (the sub-states of each C-state),
 * used only when the local APIC timer runs in them (CPUID 6, ARAT; else
 * it stops and the clock with it).  A state is chosen after the
 * processor's last idle period: the deepest whose target residency it
 * would have covered, as Linux's intel_idle menu does in its simplest
 * form.  The residencies are those Linux's intel_idle gives Broadwell
 * (numbers, not code).  Off for now (shelved with the rest of the
 * power management): only *cstates= (plan9.ini) turns it on; without
 * it an idle processor waits in C1 as in 9front.
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

enum {
	Arat	= 1<<2,		/* CPUID 6 EAX: APIC timer always running */
	Nstate	= 8,
};

extern void (*idlemwait)(void);
extern int nrdy;

/* the target residency (µs) of MWAIT C-state k: hint (k-1)<<4 */
static ulong residency[Nstate] = {
	0, 0, 100, 400, 500, 900, 1800, 7700,
};

static struct {
	int	n;		/* states in hint[] (hint[0] is C1) */
	ulong	hint[Nstate];
	ulong	us[Nstate];
} cs;

static uvlong lastidle[MAXMACH];	/* µs */

static void
cstateidle(void)
{
	uvlong t0, t1, hz;
	int k;

	for(k = cs.n-1; k > 0 && cs.us[k] > lastidle[m->machno]; k--)
		;
	cycles(&t0);
	mwaithint(&nrdy, cs.hint[k]);
	cycles(&t1);
	hz = m->cpuhz;
	if(hz != 0)
		lastidle[m->machno] = (t1 - t0) * 1000000ULL / hz;
}

void
cstatelink(void)
{
	ulong r[4];
	int k, sub;

	if(getconf("*cstates") == nil || (m->cpuidcx & Monitor) == 0)
		return;
	cpuid(0, 0, r);
	if(r[0] < 6)
		return;
	cpuid(6, 0, r);
	if((r[0] & Arat) == 0)
		return;
	cpuid(5, 0, r);
	cs.n = 0;
	cs.hint[cs.n] = 0;
	cs.us[cs.n++] = 0;
	for(k = 2; k < Nstate; k++){
		sub = (r[3] >> 4*k) & 0xF;
		if(sub == 0)
			continue;
		cs.hint[cs.n] = (k-1)<<4;
		cs.us[cs.n++] = residency[k];
	}
	if(cs.n > 1)
		idlemwait = cstateidle;
}
