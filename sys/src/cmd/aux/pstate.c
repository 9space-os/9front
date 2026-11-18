/*
 * aux/pstate: the processors' clock (P-states). Serves /dev/pstate:
 * reading it gives the method, the limits and each processor's
 * clock and load; writing auto, high or low sets the policy.
 *
 * Methods, chosen by the processor (CPUID and the MSRs, #P/msr):
 *	intel-hwp	Intel's hardware P-states (Skylake and newer): the
 *			processor picks the clock itself within a minimum
 *			and maximum, after an energy/performance preference
 *	amd-cppc	AMD's CPPC (Zen 2 and newer, if the firmware has
 *			it on): the same
 *	intel-eist	Enhanced SpeedStep by ratio (Nehalem to Broadwell;
 *			Silvermont/Airmont Atoms excepted: their PERF_CTL
 *			carries a voltage), with turbo
 *	amd-pstate	AMD's P-state MSRs (family 10h and newer); boost
 *			comes by itself in P0
 * The last two have no governor of their own: one here raises the
 * clock to the highest when a processor is busy and lowers it to the
 * lowest after half a second without load. Its rule is OpenBSD's
 * setperf_auto (sys/kern/sched_bsd.c, "Copyright (c) 1982, 1986,
 * 1990, 1991, 1993 The Regents of the University of California",
 * BSD licence): faster when one processor was busy more than two
 * thirds of the last 100 ms or all of them more than half, slower
 * after five such periods without; written anew here, the load
 * taken from the MPERF counter (it stops while a processor halts).
 * The HWP and CPPC registers as in the Intel SDM vol. 3 §15.4 and
 * AMD's PPR, the order of their setup as FreeBSD's hwpstate_intel.c
 * and hwpstate_amd.c do it (BSD-2-Clause).
 *
 * Each processor's MSRs are only reachable from that processor: one
 * proc per processor, wired to it.
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

enum {
	/* Intel */
	MPERF		= 0xE7,
	APERF		= 0xE8,
	PLATFORMINFO	= 0xCE,
	PERFSTATUS	= 0x198,
	PERFCTL		= 0x199,
	MISCENABLE	= 0x1A0,
	TURBORATIO	= 0x1AD,
	ENERGYPERFBIAS	= 0x1B0,
	PMENABLE	= 0x770,
	HWPCAP		= 0x771,
	HWPREQ		= 0x774,

	/* AMD */
	PSTATELIMIT	= 0xC0010061,
	PSTATECTL	= 0xC0010062,
	PSTATESTATUS	= 0xC0010063,
	CPPCCAP1	= 0xC00102B0,
	CPPCENABLE	= 0xC00102B1,
	CPPCREQ		= 0xC00102B3,

	Mnone = 0,
	Mhwp,
	Mcppc,
	Meist,
	Mamdpst,

	Pauto = 0,
	Phigh,
	Plow,

	Lhigh = 0,	/* governor levels */
	Llow,

	Govms = 100,	/* the governor's period */
	Samplems = 1000,	/* the clock's sample without a governor */
	Downbeats = 5,

	MAXCPU = 256,
};

typedef struct Cpu Cpu;
struct Cpu {
	int	id;		/* machno, /dev/sysstat's first column */
	int	ok;		/* wired and set up */
	char	*err;

	/* the method's limits on this processor */
	int	lowest;
	int	nominal;
	int	highest;

	uvlong	m, a, t;	/* MPERF, APERF, TSC at the last sample */
	int	busy;		/* percent of the last period not halted */
	int	mhz;		/* clock while not halted */
	int	level;		/* the governor's level applied */
	int	gen;		/* the policy's generation applied */
};

static char *mnames[] = {
[Mnone]		"none",
[Mhwp]		"intel-hwp",
[Mcppc]		"amd-cppc",
[Meist]		"intel-eist",
[Mamdpst]	"amd-pstate",
};

static char *pnames[] = {
[Pauto]	"auto",
[Phigh]	"high",
[Plow]	"low",
};

static int	method;
static char	*why;		/* why none */
static int	policy;
static int	gen;		/* bumped by each policy change */
static int	golevel = Llow;	/* the governor's decision */
static int	hasaperf;
static int	hasepp;		/* HWP: EPP in HWP_REQUEST, else EPB */
static int	hasepb;
static double	tscmhz;
static int	debug;
static int	noenable;	/* -n: HWP and CPPC are not switched on (they stay on until a reset) */

static Cpu	cpus[MAXCPU];
static int	ncpu;

static void
cpuid(uint leaf, uint sub, uint r[4])
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
#else
	USED(leaf, sub);
	r[0] = r[1] = r[2] = r[3] = 0;	/* no CPUID: probe says "none" */
#endif
}

static int msrfd = -1;

static int
rdmsr(uvlong msr, uvlong *v)
{
	return pread(msrfd, v, 8, msr) == 8? 0: -1;
}

static int
wrmsr(uvlong msr, uvlong v)
{
	return pwrite(msrfd, &v, 8, msr) == 8? 0: -1;
}

static void
dprint(char *fmt, ...)
{
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
}

static int
wire(int id)
{
	char buf[64];
	int fd, r;

	snprint(buf, sizeof buf, "/proc/%d/ctl", getpid());
	if((fd = open(buf, OWRITE)) < 0)
		return -1;
	r = fprint(fd, "wired %d", id);
	close(fd);
	if(r < 0)
		return -1;
	sleep(10);	/* scheduled again: now on that processor */
	return 0;
}

static void
readcpus(void)
{
	char buf[16384], *p, *e;
	int fd, n;

	if((fd = open("/dev/sysstat", OREAD)) < 0)
		sysfatal("/dev/sysstat: %r");
	n = readn(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		sysfatal("/dev/sysstat: %r");
	buf[n] = 0;
	for(p = buf; *p != 0 && ncpu < MAXCPU; p = e){
		if((e = strchr(p, '\n')) == nil)
			e = p + strlen(p);
		else
			e++;
		while(*p == ' ')
			p++;
		if(*p >= '0' && *p <= '9')
			cpus[ncpu++].id = atoi(p);
	}
	if(ncpu == 0)
		sysfatal("no processors in /dev/sysstat");
}

static double
measuretsc(void)
{
	uvlong t0, t1;
	vlong n0, n1;

	cycles(&t0);
	n0 = nsec();
	sleep(200);
	cycles(&t1);
	n1 = nsec();
	if(n1 <= n0 || t1 <= t0)
		return 0;
	return (double)(t1 - t0) * 1000.0 / (double)(n1 - n0);
}

/* Silvermont and Airmont: PERF_CTL's low byte is a voltage (VID) */
static int
atomvid(int model)
{
	switch(model){
	case 0x37: case 0x4A: case 0x4C: case 0x4D: case 0x5A: case 0x5D:
		return 1;
	}
	return 0;
}

/*
 * Switch on HWP or CPPC: a write that holds until the next reset.
 * Not with -n, which only looks.
 */
static int
enable(uvlong msr)
{
	uvlong v;

	if(rdmsr(msr, &v) < 0)
		return -1;
	if(v & 1)
		return 0;
	if(noenable){
		werrstr("off (-n does not switch it on)");
		return -1;
	}
	return wrmsr(msr, 1);
}

/*
 * The method, decided on the first processor (this proc is wired to
 * it): CPUID and whether the MSRs answer. With -n, HWP and CPPC are
 * taken by their CPUID bit and their enable MSR answering.
 */
static void
probe(void)
{
	uint r[4], maxleaf, maxext, family, model;
	char vendor[13];
	uvlong v;

#if !defined(__x86_64__) && !defined(__i386__)
	why = "not an x86 processor";
	return;
#endif
	cpuid(0, 0, r);
	maxleaf = r[0];
	memmove(vendor, &r[1], 4);
	memmove(vendor+4, &r[3], 4);
	memmove(vendor+8, &r[2], 4);
	vendor[12] = 0;
	cpuid(1, 0, r);
	family = (r[0]>>8) & 0xF;
	model = (r[0]>>4) & 0xF;
	if(family == 0xF)
		family += (r[0]>>20) & 0xFF;
	if(family == 6 || family >= 0xF)
		model |= ((r[0]>>16) & 0xF) << 4;
	if(r[2] & (1<<31)){
		why = "a virtual machine";
		return;
	}
	cpuid(0x80000000, 0, r);
	maxext = r[0];
	if(maxleaf >= 6){
		cpuid(6, 0, r);
		hasaperf = r[2] & 1;
		hasepb = (r[2]>>3) & 1;
	}

	if(strcmp(vendor, "GenuineIntel") == 0){
		if(maxleaf >= 6){
			cpuid(6, 0, r);
			if(r[0] & (1<<7)){	/* HWP */
				hasepp = (r[0]>>10) & 1;
				if(noenable && rdmsr(PMENABLE, &v) == 0){
					method = Mhwp;
					return;
				}
				if(enable(PMENABLE) == 0 && rdmsr(HWPCAP, &v) == 0){
					method = Mhwp;
					return;
				}
				dprint("pstate: HWP did not enable: %r\n");
			}
		}
		cpuid(1, 0, r);
		if(family != 6 || atomvid(model)){
			why = "an Intel processor without ratio-based SpeedStep";
			return;
		}
		if((r[2] & (1<<7)) == 0){
			why = "no Enhanced SpeedStep";
			return;
		}
		if(rdmsr(MISCENABLE, &v) < 0 || (v & (1<<16)) == 0){
			why = "Enhanced SpeedStep off in the firmware";
			return;
		}
		if(rdmsr(PLATFORMINFO, &v) < 0){
			why = "no MSR_PLATFORM_INFO (older than Nehalem: needs ACPI's _PSS)";
			return;
		}
		method = Meist;
		return;
	}
	if(strcmp(vendor, "AuthenticAMD") == 0 || strcmp(vendor, "HygonGenuine") == 0){
		if(maxext >= 0x80000008){
			cpuid(0x80000008, 0, r);
			if(r[1] & (1<<27)){
				if(noenable && rdmsr(CPPCENABLE, &v) == 0){
					method = Mcppc;
					return;
				}
				if(enable(CPPCENABLE) == 0 && rdmsr(CPPCREQ, &v) == 0){
					method = Mcppc;
					return;
				}
				dprint("pstate: CPPC did not enable: %r\n");
			}
		}
		if(family < 0x10 || maxext < 0x80000007){
			why = "an AMD processor older than family 10h";
			return;
		}
		cpuid(0x80000007, 0, r);
		if((r[3] & (1<<7)) == 0 || rdmsr(PSTATELIMIT, &v) < 0){
			why = "no hardware P-states";
			return;
		}
		method = Mamdpst;
		return;
	}
	why = smprint("a processor by %s", vendor);
}

/* this processor's limits, in the method's units */
static int
limits(Cpu *c)
{
	uvlong v, t;
	uint r[4];
	int lo, hi;

	switch(method){
	case Mhwp:
		if(enable(PMENABLE) < 0 || rdmsr(HWPCAP, &v) < 0)
			return -1;
		c->highest = v & 0xFF;
		c->nominal = (v>>8) & 0xFF;
		c->lowest = (v>>24) & 0xFF;
		return 0;
	case Mcppc:
		if(enable(CPPCENABLE) < 0)
			return -1;
		lo = 0;
		hi = 255;
		c->nominal = 0;
		if(rdmsr(CPPCCAP1, &v) == 0){
			if((v & 0xFF) < ((v>>24) & 0xFF)){
				lo = v & 0xFF;
				hi = (v>>24) & 0xFF;
			}
			c->nominal = (v>>16) & 0xFF;
		}
		c->lowest = lo;
		c->highest = hi;
		return 0;
	case Meist:
		if(rdmsr(PLATFORMINFO, &v) < 0)
			return -1;
		c->nominal = (v>>8) & 0xFF;
		c->lowest = (v>>40) & 0xFF;
		if(c->lowest == 0 || c->lowest > c->nominal)
			c->lowest = c->nominal;
		c->highest = c->nominal;
		/* turbo: available (CPUID 6) and not off (MISC_ENABLE bit 38) */
		cpuid(6, 0, r);
		if((r[0] & 2) && rdmsr(MISCENABLE, &t) == 0 && (t & (1ULL<<38)) == 0)
		if(rdmsr(TURBORATIO, &t) == 0 && (t & 0xFF) > c->nominal)
			c->highest = t & 0xFF;
		return 0;
	case Mamdpst:
		/* P-state numbers: 0 the fastest; the limit can change */
		if(rdmsr(PSTATELIMIT, &v) < 0)
			return -1;
		c->highest = v & 7;
		c->lowest = (v>>4) & 7;
		c->nominal = 0;
		return 0;
	}
	return -1;
}

/* the policy, for HWP and CPPC */
static int
setrequest(Cpu *c)
{
	uvlong req;
	int min, max, epp, epb;

	switch(policy){
	default:
		min = c->lowest; max = c->highest; epp = 0x80; epb = 6;
		break;
	case Phigh:
		min = c->highest; max = c->highest; epp = 0; epb = 0;
		break;
	case Plow:
		min = c->lowest; max = c->lowest; epp = 0xFF; epb = 15;
		break;
	}
	if(method == Mhwp){
		/* desired 0: the processor chooses; activity window 0: automatic */
		req = (uvlong)min | (uvlong)max<<8;
		if(hasepp)
			req |= (uvlong)epp<<24;
		else if(hasepb){
			uvlong v;

			if(rdmsr(ENERGYPERFBIAS, &v) == 0)
				wrmsr(ENERGYPERFBIAS, (v & ~0xFULL) | epb);
		}
		return wrmsr(HWPREQ, req);
	}
	/* CPPC: max 7:0, min 15:8, desired 23:16 (0: autonomous), EPP 31:24 */
	req = (uvlong)max | (uvlong)min<<8 | (uvlong)epp<<24;
	return wrmsr(CPPCREQ, req);
}

/* a governor level, for EIST and AMD's P-states */
static int
setlevel(Cpu *c, int level)
{
	uvlong v;
	int x;

	switch(method){
	case Meist:
		x = level == Lhigh? c->highest: c->lowest;
		if(rdmsr(PERFCTL, &v) < 0)
			return -1;
		/* the ratio in 15:8; bit 32 (turbo disengage) cleared */
		v &= ~(0xFFFFULL | 1ULL<<32);
		v |= (uvlong)x << 8;
		return wrmsr(PERFCTL, v);
	case Mamdpst:
		if(limits(c) < 0)
			return -1;
		x = level == Lhigh? c->highest: c->lowest;
		return wrmsr(PSTATECTL, x);
	}
	return -1;
}

static void
sample(Cpu *c)
{
	uvlong m, a, t;

	if(!hasaperf || rdmsr(MPERF, &m) < 0 || rdmsr(APERF, &a) < 0)
		return;
	cycles(&t);
	if(c->t != 0 && t > c->t && m > c->m){
		c->busy = (m - c->m) * 100 / (t - c->t);
		if(c->busy > 100)
			c->busy = 100;
		c->mhz = tscmhz * (double)(a - c->a) / (double)(m - c->m);
	}else if(c->t != 0)
		c->busy = 0;	/* halted the whole period */
	c->m = m;
	c->a = a;
	c->t = t;
}

static int
governed(void)
{
	return method == Meist || method == Mamdpst;
}

static void
cpuproc(void *v)
{
	Cpu *c;
	int g, want;

	c = v;
	threadsetname("pstate cpu%d", c->id);
	if(wire(c->id) < 0){
		c->err = smprint("wire: %r");
		return;
	}
	if(limits(c) < 0){
		c->err = smprint("limits: %r");
		return;
	}
	c->gen = -1;
	c->level = -1;
	c->ok = 1;
	for(;;){
		g = gen;
		if(c->gen != g){
			c->gen = g;
			if(method == Mhwp || method == Mcppc){
				if(setrequest(c) < 0)
					c->err = smprint("request: %r");
			}
			c->level = -1;
		}
		if(governed()){
			switch(policy){
			case Phigh:	want = Lhigh; break;
			case Plow:	want = Llow; break;
			default:	want = golevel; break;
			}
			if(want != c->level){
				if(setlevel(c, want) < 0)
					c->err = smprint("level: %r");
				c->level = want;
			}
		}
		sample(c);
		sleep(governed()? Govms: Samplems);
	}
}

/*
 * OpenBSD's setperf_auto: faster when a processor was busy more than
 * 2/3 or all together more than 1/2; slower after Downbeats periods
 * without.
 */
static void
govproc(void*)
{
	int i, n, speedup, sum, beats;

	threadsetname("pstate governor");
	beats = 0;
	for(;;){
		sleep(Govms);
		speedup = 0;
		sum = 0;
		n = 0;
		for(i = 0; i < ncpu; i++){
			if(!cpus[i].ok)
				continue;
			if(cpus[i].busy > 66)
				speedup = 1;
			sum += cpus[i].busy;
			n++;
		}
		if(n > 0 && sum > 50*n)
			speedup = 1;
		if(speedup && beats < Downbeats)
			beats++;
		if(speedup && golevel != Lhigh){
			golevel = Lhigh;
			dprint("pstate: high\n");
		}else if(!speedup && golevel != Llow && --beats <= 0){
			golevel = Llow;
			dprint("pstate: low\n");
		}
	}
}

static char*
status(void)
{
	Fmt f;
	Cpu *c;
	int i;

	fmtstrinit(&f);
	fmtprint(&f, "method %s\n", mnames[method]);
	if(method == Mnone){
		fmtprint(&f, "why %s\n", why);
		return fmtstrflush(&f);
	}
	fmtprint(&f, "policy %s\n", pnames[policy]);
	if(tscmhz > 0)
		fmtprint(&f, "tsc %.0f\n", tscmhz);
	for(i = 0; i < ncpu; i++){
		c = &cpus[i];
		fmtprint(&f, "cpu%d", c->id);
		if(!c->ok){
			fmtprint(&f, " off %s\n", c->err? c->err: "starting");
			continue;
		}
		fmtprint(&f, " limits %d %d %d", c->lowest, c->nominal, c->highest);
		if(governed())
			fmtprint(&f, " level %s", c->level == Lhigh? "high": "low");
		if(hasaperf)
			fmtprint(&f, " mhz %d busy %d", c->mhz, c->busy);
		if(c->err)
			fmtprint(&f, " error %s", c->err);
		fmtprint(&f, "\n");
	}
	return fmtstrflush(&f);
}

static int
setpolicy(char *s)
{
	int i;

	for(i = 0; i < nelem(pnames); i++)
		if(strcmp(s, pnames[i]) == 0){
			policy = i;
			gen++;
			return 0;
		}
	return -1;
}

static void
fsread(Req *r)
{
	char *s;

	s = status();
	readstr(r, s);
	free(s);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char buf[64], *f[4];
	int n;

	n = r->ifcall.count;
	if(n >= sizeof buf)
		n = sizeof buf - 1;
	memmove(buf, r->ifcall.data, n);
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) != 1 || setpolicy(f[0]) < 0){
		respond(r, "usage: auto|high|low");
		return;
	}
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
};

static void
usage(void)
{
	fprint(2, "usage: %s [-dn] [-s srvname] [-m mtpt] [auto|high|low]\n", argv0);
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	char *srvname, *mtpt, *user;
	int i, probeonly;

	srvname = "pstate";
	mtpt = "/dev";
	probeonly = 0;
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'n':
		probeonly++;
		noenable++;
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc > 1)
		usage();
	if(argc == 1 && setpolicy(argv[0]) < 0)
		usage();

	if((msrfd = open("#P/msr", ORDWR)) < 0)
		sysfatal("#P/msr: %r");
	readcpus();
	if(wire(cpus[0].id) < 0)
		sysfatal("wire: %r");
	probe();
	if(probeonly){
		print("method %s\n", mnames[method]);
		if(method == Mnone)
			print("why %s\n", why);
		else if(limits(&cpus[0]) == 0)
			print("cpu%d limits %d %d %d\n", cpus[0].id, cpus[0].lowest, cpus[0].nominal, cpus[0].highest);
		else
			print("cpu%d limits unknown: %r\n", cpus[0].id);
		threadexitsall(nil);
	}
	if(method == Mnone){
		dprint("pstate: no P-state control: %s\n", why);
		threadexitsall("no P-state control");
	}
	tscmhz = measuretsc();

	/* this proc stays wired to the first processor: unwire it */
	{
		char buf[64];
		int fd;

		snprint(buf, sizeof buf, "/proc/%d/ctl", getpid());
		if((fd = open(buf, OWRITE)) >= 0){
			fprint(fd, "wired -1");
			close(fd);
		}
	}

	for(i = 0; i < ncpu; i++)
		proccreate(cpuproc, &cpus[i], 16*1024);
	if(governed() && hasaperf)
		proccreate(govproc, nil, 16*1024);
	else if(governed())
		golevel = Lhigh;	/* no load to go by: the highest */

	user = getuser();
	fs.tree = alloctree(user, user, 0555, nil);
	createfile(fs.tree->root, "pstate", user, 0664, nil);
	threadpostmountsrv(&fs, srvname, mtpt, MAFTER);
	threadexits(nil);
}
