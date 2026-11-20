/*
 * aux/power: what the processor package draws and how deep it idles,
 * from its model-specific registers (#P/msr, arch(3)): the RAPL energy
 * counters of the package, the cores, the integrated GPU and DRAM
 * (Intel; AMD's package and cores), and the share of time the package
 * and each core spent in their C-states, every interval.
 *
 * The registers are Intel's (Software Developer's Manual vol. 4) and
 * AMD's (PPR, family 17h on); which ones a processor has is found by
 * reading them: a register it lacks is left out.
 */
#include <u.h>
#include <libc.h>

enum {
	Tsc		= 0x10,

	/* Intel RAPL */
	Iunit		= 0x606,
	Ipkg		= 0x611,
	Idram		= 0x619,
	Ipp0		= 0x639,
	Ipp1		= 0x641,

	/* AMD RAPL */
	Aunit		= 0xC0010299,
	Acore		= 0xC001029A,
	Apkg		= 0xC001029B,

	Maxcpu		= 64,
};

typedef struct Meter Meter;
struct Meter {
	char	*name;
	ulong	msr;
	int	percpu;		/* summed over the processors (AMD's cores) */
	int	ok;
	u32int	last[Maxcpu];
	double	joules;		/* in the interval */
};

typedef struct Res Res;
struct Res {
	char	*name;
	ulong	msr;
	int	core;		/* each core's, else the package's */
	int	ok;
	uvlong	last[Maxcpu];
	uvlong	delta[Maxcpu];
	char	have[Maxcpu];	/* last is a reading */
};

static Meter imeters[] = {
	{ "pkg",	Ipkg },
	{ "cores",	Ipp0 },
	{ "gpu",	Ipp1 },
	{ "dram",	Idram },
};

static Meter ameters[] = {
	{ "pkg",	Apkg },
	{ "cores",	Acore, 1 },
};

/* Intel's residency counters (they count at the TSC's rate) */
static Res res[] = {
	{ "pc2",	0x60D },
	{ "pc3",	0x3F8 },
	{ "pc6",	0x3F9 },
	{ "pc7",	0x3FA },
	{ "pc8",	0x630 },
	{ "pc9",	0x631 },
	{ "pc10",	0x632 },
	{ "cc3",	0x3FC, 1 },
	{ "cc6",	0x3FD, 1 },
	{ "cc7",	0x3FE, 1 },
};

static int msrfd;
static int ctlfd;
static int ncpu;
static double eunit;		/* joules per count */
static Meter *meters;
static int nmeters;
static uvlong tsclast[Maxcpu], tscdelta[Maxcpu];

static int
rdmsr(ulong msr, uvlong *v)
{
	return pread(msrfd, v, 8, msr) == 8 ? 0 : -1;
}

/*
 * the reads that follow on processor n: wired, then a real sleep (a
 * sleep(0) yields only when another process is ready, so on an idle
 * machine it went on reading on the processor it was on)
 */
static void
oncpu(int n)
{
	if(ctlfd >= 0)
		fprint(ctlfd, "wired %d", n);
	sleep(1);
}

static int
countcpus(void)
{
	char buf[1024], *p;
	int fd, n, i;

	/* sysstat: a line per processor */
	if((fd = open("/dev/sysstat", OREAD)) < 0)
		return 1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return 1;
	buf[n] = 0;
	i = 0;
	for(p = buf; (p = strchr(p, '\n')) != nil; p++)
		i++;
	if(i < 1)
		i = 1;
	if(i > Maxcpu)
		i = Maxcpu;
	return i;
}

static void
setup(void)
{
	uvlong v;
	int i;

	oncpu(0);
	if(rdmsr(Iunit, &v) == 0){
		meters = imeters;
		nmeters = nelem(imeters);
	}else if(rdmsr(Aunit, &v) == 0){
		meters = ameters;
		nmeters = nelem(ameters);
		for(i = 0; i < nelem(res); i++)
			res[i].msr = 0;	/* Intel's only */
	}else{
		fprint(2, "power: no RAPL registers\n");
		v = 0;
	}
	eunit = 1.0 / (double)(1ULL << ((v >> 8) & 0x1F));
	for(i = 0; i < nmeters; i++)
		meters[i].ok = rdmsr(meters[i].msr, &v) == 0;
	for(i = 0; i < nelem(res); i++)
		res[i].ok = res[i].msr != 0 && rdmsr(res[i].msr, &v) == 0;
}

/* one pass over the processors: the counters' deltas since the last */
static void
sample(void)
{
	Meter *mt;
	Res *r;
	uvlong v;
	u32int e;
	int c, i;

	for(i = 0; i < nmeters; i++)
		meters[i].joules = 0;
	for(c = 0; c < ncpu; c++){
		oncpu(c);
		if(rdmsr(Tsc, &v) == 0){
			tscdelta[c] = v - tsclast[c];
			tsclast[c] = v;
		}
		for(i = 0; i < nmeters; i++){
			mt = &meters[i];
			if(!mt->ok || (!mt->percpu && c != 0))
				continue;
			if(rdmsr(mt->msr, &v) < 0)
				continue;
			e = v;		/* 32 bits that wrap */
			mt->joules += (u32int)(e - mt->last[c]) * eunit;
			mt->last[c] = e;
		}
		for(i = 0; i < nelem(res); i++){
			r = &res[i];
			if(!r->ok || (!r->core && c != 0))
				continue;
			r->delta[c] = 0;
			if(rdmsr(r->msr, &v) < 0){
				r->have[c] = 0;
				continue;
			}
			if(r->have[c] && v >= r->last[c])
				r->delta[c] = v - r->last[c];
			r->last[c] = v;
			r->have[c] = 1;
		}
	}
}

/* a residency's share of the interval, at most all of it */
static double
share(uvlong d, uvlong tsc)
{
	if(tsc == 0)
		return 0;
	if(d > tsc)
		d = tsc;
	return 100.0 * d / tsc;
}

static void
report(double secs)
{
	Res *r;
	double pct, avg;
	int c, i;

	for(i = 0; i < nmeters; i++)
		if(meters[i].ok)
			print("%s %.2fW  ", meters[i].name, meters[i].joules / secs);
	for(i = 0; i < nelem(res); i++){
		r = &res[i];
		if(!r->ok)
			continue;
		if(!r->core){
			pct = share(r->delta[0], tscdelta[0]);
			print("%s %.0f%%  ", r->name, pct);
		}else{
			avg = 0;
			for(c = 0; c < ncpu; c++)
				avg += share(r->delta[c], tscdelta[c]);
			print("%s %.0f%%  ", r->name, avg / ncpu);
		}
	}
	print("\n");
}

static void
usage(void)
{
	fprint(2, "usage: aux/power [-i seconds] [-n count]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[64];
	double secs;
	int n, count;

	secs = 5;
	count = -1;
	ARGBEGIN{
	case 'i':
		secs = atof(EARGF(usage()));
		if(secs < 0.1)
			usage();
		break;
	case 'n':
		count = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	if((msrfd = open("#P/msr", OREAD)) < 0)
		sysfatal("open #P/msr: %r");
	snprint(buf, sizeof buf, "/proc/%d/ctl", getpid());
	ctlfd = open(buf, OWRITE);
	ncpu = countcpus();
	setup();
	sample();
	for(n = 0; count < 0 || n < count; n++){
		sleep(secs * 1000);
		sample();
		report(secs);
	}
	exits(nil);
}
