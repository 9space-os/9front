#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <aml.h>

typedef struct Batstat Batstat;
typedef struct Bat Bat;
typedef struct Tbl Tbl;
typedef struct Therm Therm;
typedef struct FACP FACP;

struct Batstat {
	int rate;
	int capacity;
	int state;
	int voltage;
};

struct Bat {
	char *unit;
	void *bst;
	int fullcharge;
	int capacity;
	int capacitywarn;
	int capacitylow;
	int voltage;
	int chargestart;
	int chargestop;
};

struct Tbl {
	uchar sig[4];
	uchar len[4];
	uchar rev;
	uchar csum;
	uchar oemid[6];
	uchar oemtid[8];
	uchar oemrev[4];
	uchar cid[4];
	uchar crev[4];
	uchar data[];
};

struct Therm {
	uint cpus;
	void *tmp;
};

struct FACP {
	int ok;

	uvlong pm1a;
	int pm1aspace;
	int pm1awid;

	uvlong pm1b;
	int pm1bspace;
	int pm1bwid;

	uvlong gpe0;
	ulong gpe0len;
	int gpe0space;
	int gpe0wid;

	uvlong gpe1;
	ulong gpe1len;
	int gpe1space;
	int gpe1wid;

	ulong slpa;
	ulong slpb;
};

enum {
	Tblsz = 4+4+1+1+6+8+4+4+4,

	Temp = 1,
	Battery,
	Pmctl,
	Aml,
	Events,		/* nfront: acpievents */
	Backlight,	/* nfront: the panel's brightness through _BCM */

	SLP_EN = 0x2000,
	SLP_TM = 0x1c00,
};

static int ec, mem, iofd[5], nbats, ntherms;
static char *uid = "pm", *units[] = {"mW", "mA"};
static Therm therms[16];
static Bat bats[4];
static void *bccs, *bcss;
static FACP facp;
static char *amlrequest(char*, int, char**);	/* 9front-ng: the aml file */
static void *amlbelow(void*, char*);
static void *ecdev, *hkey;	/* nfront: the EC, a ThinkPad's hotkeys (events) */
static int ecgpe = -1;
static QLock amllk;	/* nfront: one evaluation at a time, the files' and the events' */
static void amldone(void);
static void fsevread(Req*);
static void backlightread(char*, char*);
static char *backlightwrite(char*);

static int
enumec(void *dot, void *)
{
	void *p;
	char *id;

	p = amlval(amlwalk(dot, "^_HID"));
	id = amleisaid(p);
	if(id == nil || strcmp(id, "PNP0C09") != 0)
		return 1;
	p = amlwalk(dot, "^_REG");
	if(p != nil)
		amleval(p, "ii", 0x3, 1, nil);
	/* nfront: its events (an EC's _GPE is a GPE number; a package, a block's: not here) */
	if(ecdev == nil && (ecdev = amlwalk(dot, "^")) != nil){
		amltake(ecdev);
		if((p = amlwalk(dot, "^_GPE")) != nil && amleval(p, "", &p) >= 0 && amltag(p) == 'i')
			ecgpe = amlint(p);
	}
	return 1;
}

/*
 * nfront: the devices' _INI, as the ACPI spec (6.5.1) and OpenBSD's
 * acpi_inidev (sys/dev/acpi/acpi.c,v 1.458; its notice below, with the
 * events) run them: where the device is present by its _STA (none: it
 * is), and nothing below a device neither present, enabled nor
 * functioning.  Firmware sets itself up there: an embedded controller's
 * _INI enables its queries of the hotkeys (Lenovo's KQEN) and notes the
 * OS by _OSI.
 */
static uvlong
getsta(void *dev)
{
	void *p;

	if((p = amlwalk(dev, "_STA")) == nil || amlwalk(p, "^") != dev || amleval(p, "", &p) < 0)
		return 0xF;
	return amlint(p);
}

static int
enumini(void *dot, void *)
{
	void *dev, *d;
	uvlong sta;

	if((dev = amlwalk(dot, "^")) == nil)
		return 1;
	for(d = amlwalk(dev, "^"); d != nil && d != amlroot; d = amlwalk(d, "^")){
		sta = getsta(d);
		if((sta & 8) == 0 && (sta & 3) == 0)
			return 1;	/* below a device not there */
	}
	if(dev == amlroot || getsta(dev) & 1)
		amleval(dot, "", nil);
	return 1;
}

static int
enumbat(void *dot, void *)
{
	void *p, *r, **rr;
	Bat *b;
	int n;

	if(nbats >= nelem(bats))
		return 1;

	if((p = amlwalk(dot, "^_STA")) == nil)
		return 1;
	if(amleval(p, "", &r) < 0 || (amlint(r)&3) != 3)
		return 1;
	if(amleval(dot, "", &r) < 0) /* _BIF */
		return 1;
	if(r == nil || amltag(r) != 'p' || amllen(r) < 7)
		return 1;

	rr = amlval(r);
	b = &bats[nbats];
	if((n = amlint(rr[0])) >= nelem(units) || n < 0)
		b->unit = "??";
	else
		b->unit = units[n];
	b->capacity = amlint(rr[1]);
	if(b->capacity < 0) /* even though _STA tells it's there */
		return 1;
	b->fullcharge = amlint(rr[2]);
	b->voltage = amlint(rr[4]);
	b->capacitywarn = amlint(rr[5]);
	b->capacitylow = amlint(rr[6]);
	b->chargestart = 0;
	b->chargestop = 100;

	b->bst = amlwalk(dot, "^_BST");
	if(b->bst != nil){
		amltake(b->bst);
		nbats++;
	}

	return 1;
}

static int
enumtmp(void *dot, void *)
{
	void *r, **rr;
	char s[64];
	uint cpus;
	int i, n;

	cpus = 0;
	if(ntherms < nelem(therms) && amleval(dot, "", &r) >= 0 && amllen(r) > 0 && (rr = amlval(r)) != nil){
		for(i = 0; i < amllen(r); i++){
			snprint(s, sizeof(s), "%N", amlval(rr[i]));
			for(n = strlen(s)-1; n > 3; n--){
				if(s[n-2] == 'C' && s[n-1] == 'P' && s[n] == 'U' && s[n+1] >= '0' && s[n+1] <= '9'){
					cpus |= 1 << atoi(&s[n+1]);
					break;
				}
			}
		}
	}

	if(cpus != 0 && (dot = amlwalk(dot, "^_TMP")) != nil){
		therms[ntherms].cpus = cpus;
		therms[ntherms].tmp = dot;
		ntherms++;
	}

	return 1;
}

static int
enumtp(void *dot, void *)
{
	void *p, *r;
	char *id;
	int i;
	uvlong v;
	static char *compat[] = { "IBM0068", "LEN0068", "LEN0268" };

	if(ec < 0)
		return 1;

	p = amlval(amlwalk(dot, "^_HID"));
	id = amleisaid(p);

	if(id == nil)
		return 1;

	for(i = 0; i < nelem(compat); i++)
		if(strcmp(id, compat[i]) == 0)
			break;

	if(i >= nelem(compat))
		return 1;

	if((p = amlwalk(dot, "^MHKV")) == nil)
		return 1;
	if(amleval(p, "", &r) < 0)
		return 1;

	switch((uint)amlint(r) >> 8){
	case 1:
	case 2:
		break;
	default:
		return 1;
	}

	/*
	 * nfront: the hotkeys, as acpithinkpad's thinkpad_enable_events:
	 * MHKA's mask and the brightness keys (bits 15, 16) through MHKM,
	 * then MHKC(1); they come as Notify(HKEY, 0x80), read by MHKP
	 */
	if(hkey == nil && (p = amlwalk(dot, "^MHKA")) != nil && amleval(p, "", &r) >= 0){
		uvlong mask;

		mask = amlint(r) | 1<<15 | 1<<16;
		if((p = amlwalk(dot, "^MHKM")) != nil)
			for(i = 0; i < 32; i++)
				amleval(p, "ii", i+1, (mask & 1ULL<<i) != 0, nil);
		if((p = amlwalk(dot, "^MHKC")) != nil && amleval(p, "i", 1, nil) >= 0){
			hkey = amlwalk(dot, "^");
			amltake(hkey);
		}
	}

	if((p = amlwalk(dot, "^BCTG")) != nil){
		for(i = 0; i < nbats; i++){
			if(amleval(p, "i", i+1, &r) < 0)
				continue;
			v = amlint(r);
			if((v & 0x80000000) == 0 && (v & 0x100) != 0)
				bats[i].chargestart = v & 0xff;
		}
		if((bccs = amlwalk(dot, "^BCCS")) != nil)
			amltake(bccs);
	}

	if((p = amlwalk(dot, "^BCSG")) != nil){
		for(i = 0; i < nbats; i++){
			if(amleval(p, "i", i+1, &r) < 0)
				continue;
			v = amlint(r);
			if((v & 0x80000000) == 0 && (v & 0x100) != 0)
				bats[i].chargestop = (v&0xff)!=0?(v&0xff):100;
		}
		if((bcss = amlwalk(dot, "^BCSS")) != nil)
			amltake(bcss);
	}

	return 1;
}

static int
batstat(Bat *b, Batstat *s)
{
	void *r, **rr;

	if(amleval(b->bst, "", &r) < 0)
		return -1;
	if(r == nil || amltag(r) != 'p' || amllen(r) < 4)
		return -1;
	rr = amlval(r);
	s->state = amlint(rr[0]);
	s->rate = amlint(rr[1]);
	s->capacity = amlint(rr[2]);
	s->voltage = amlint(rr[3]);
	return 0;
}

static void
batteryread(char *s, char *e)
{
	int n, x, hh, mm, ss;
	char *state;
	Batstat st;
	Bat *b;

	for(n = 0; n < nbats; n++){
		b = &bats[n];

		state = "unknown";
		ss = x = 0;

		if(batstat(b, &st) == 0){
			if(st.state & 4)
				state = "critical";
			else if(st.state & 1)
				state = "discharging";
			else if(st.state & 2)
				state = "charging";
			if(st.rate > 0 && (st.state & 2) == 0)
				ss = st.capacity * 3600 / st.rate;
			if(bats[n].fullcharge > 0){
				x = st.capacity * 100 / bats[n].fullcharge;
				if(st.rate > 0 && (st.state & 2) != 0)
					ss = (bats[n].fullcharge - st.capacity) * 3600 / st.rate;
			}
		}else{
			memset(&st, 0, sizeof(st));
		}

		hh = ss / 3600;
		ss -= 3600 * (ss / 3600);
		mm = ss / 60;
		ss -= 60 * (ss / 60);
		s = seprint(s, e, "%d %s %d %d %d %d %d %s %d %d %02d:%02d:%02d %s %d %d\n",
			x,
			bats[n].unit, st.capacity, b->fullcharge, b->capacity, b->capacitywarn, b->capacitylow,
			"mV", st.voltage, b->voltage,
			hh, mm, ss,
			state, b->chargestart, b->chargestop
		);
	}
}

static int
tpclampcharge(char *bat, int start, int stop)
{
	int i, s, lo, hi;
	void *r;
	Bat *b;

	if(bcss == nil || bccs == nil){
		werrstr("charge control not supported");
		return -1;
	}
	if(strcmp(bat, "all") == 0){
		lo = 0;
		hi = nbats;
	}else{
		lo = atoi(bat);
		hi = lo + 1;
	}
	if(lo < 0 || lo >= nbats){
		werrstr("battery out of range");
		return -1;
	}
	if(stop < 1 || stop > 100
	|| start < 0 || start > 99
	|| stop < start){
		werrstr("out of range");
		return -1;
	}
	for(i = lo; i < hi; i++){
		b = &bats[i];
		if(b->chargestart != start){
			if(amleval(bccs, "i", ((i + 1) << 8) | start, &r) < 0 || (amlint(r) & 0x80000000) != 0){
				werrstr("charge start");
				return -1;
			}
			b->chargestart = start;
		}
		if(b->chargestop != stop){
			s = (stop < 100) ? stop : 0;
			if(amleval(bcss, "i", ((i + 1) << 8) | s, &r) < 0 || (amlint(r) & 0x80000000) != 0){
				werrstr("charge stop");
				return -1;
			}
			b->chargestop = stop;
		}
	}
	return 0;
}

static void
tmpread(char *s, char *e)
{
	void *er;
	int n, t;

	for(n = 0; n < ntherms; n++){
		t = 2732;
		if(amleval(therms[n].tmp, "", &er) >= 0)
			t = amlint(er);
		s = seprint(s, e, "%d.0\n", (t - 2732)/10);
	}
}

static void
wirecpu0(void)
{
	char buf[32];
	int ctl;

	snprint(buf, sizeof(buf), "/proc/%d/ctl", getpid());
	if((ctl = open(buf, OWRITE)) < 0)
		return;
	write(ctl, "wired 0", 7);
	close(ctl);
}

static ulong
get32(uchar *p)
{
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p)
{
	return ((uvlong)p[7]<<56) | ((uvlong)p[6]<<48)
			| ((uvlong)p[5]<<40) | ((uvlong)p[4]<<32)
			| ((uvlong)p[3]<<24) | ((uvlong)p[2]<<16)
			| ((uvlong)p[1]<<8) | ((uvlong)p[0]);
}

static void
amlwrite(Amlio *io, int addr, int wid, uvlong v)
{
	uchar b[8];

	b[0] = v; b[1] = v >> 8;
	b[2] = v >> 16; b[3] = v >> 24;
	b[4] = v >> 32; b[5] = v >> 40;
	b[6] = v >> 48; b[7] = v >> 56;

	(*io->write)(io, b, 1<<(wid-1), addr);
}

static int
poweroff(void)
{
	int n;
	void *tts, *pts;
	Amlio ioa, iob;

	if(facp.ok == 0){
		werrstr("no FACP");
		return -1;
	}

	wirecpu0();

	/* The ACPI spec requires we call _TTS and _PTS to prepare
	 * the system to go to _S5 state. If they fail, too bad,
	 * try to go to _S5 state anyway. */
	pts = amlwalk(amlroot, "_PTS");
	if(pts)
		amleval(pts, "i", 5, nil);
	tts = amlwalk(amlroot, "_TTS");
	if(tts)
		amleval(tts, "i", 5, nil);

	/* disable GPEs */
	ioa.space = facp.gpe0space;
	iob.space = facp.gpe1space;
	ioa.off = facp.gpe0;
	iob.off = facp.gpe1;
	amlmapio(&ioa);
	amlmapio(&iob);

	for(n = 0; facp.gpe0 > 0 && n < facp.gpe0len/2; n += facp.gpe0wid){
		amlwrite(&ioa, facp.gpe0len/2 + n, facp.gpe0wid, 0);
		amlwrite(&ioa, n, facp.gpe0wid, ~0);
	}

	for(n = 0; facp.gpe1 > 0 && n < facp.gpe1len/2; n += facp.gpe1wid){
		amlwrite(&iob, facp.gpe1len/2 + n, facp.gpe1wid, 0);
		amlwrite(&iob, n, facp.gpe1wid, ~0);
	}

	ioa.space = facp.pm1aspace;
	iob.space = facp.pm1bspace;
	ioa.off = facp.pm1a;
	iob.off = facp.pm1b;
	amlmapio(&ioa);
	amlmapio(&iob);

	amlwrite(&ioa, 0, facp.pm1awid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	amlwrite(&iob, 0, facp.pm1bwid, ((facp.slpb << 10) & SLP_TM) | SLP_EN);
	sleep(100);

	/*
	 * The SetSystemSleeping() example from the ACPI spec 
	 * writes the same value in both registers. But Linux/BSD
	 * write distinct values from the _Sx package (like the
	 * code above). The _S5 package on a HP DC5700 is
	 * Package(0x2){0x0, 0x7} and writing SLP_TYPa of 0 to
	 * PM1a_CNT_BLK seems to have no effect but 0x7 seems
	 * to work fine. So trying the following as a last effort.
	 */

	facp.slpa |= facp.slpb;
	amlwrite(&ioa, 0, facp.pm1awid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	amlwrite(&iob, 0, facp.pm1bwid, ((facp.slpa << 10) & SLP_TM) | SLP_EN);
	sleep(100);

	werrstr("acpi failed");
	return -1;
}

static void
pmctlread(char *s, char *e)
{
	USED(s, e);
}

static void
fsread(Req *r)
{
	char msg[512], *s, *e;
	void *aux;

	s = msg;
	e = s + sizeof(msg);
	*s = 0;
	aux = r->fid->file->aux;
	if(aux == (void*)Aml){
		readstr(r, r->fid->aux != nil ? r->fid->aux : "");
		respond(r, nil);
		return;
	}
	if(aux == (void*)Events){
		fsevread(r);
		return;
	}
	if(r->ifcall.offset == 0){
		qlock(&amllk);
		if(aux == (void*)Temp)
			tmpread(s, e);
		else if(aux == (void*)Battery)
			batteryread(s, e);
		else if(aux == (void*)Pmctl)
			pmctlread(s, e);
		else if(aux == (void*)Backlight)
			backlightread(s, e);
		amldone();
	}

	readstr(r, msg);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char msg[256], *f[4];
	void *aux;
	int nf;

	aux = r->fid->file->aux;
	if(aux == (void*)Aml){
		char *err, *reply;

		qlock(&amllk);
		err = amlrequest(r->ifcall.data, r->ifcall.count, &reply);
		amldone();
		free(r->fid->aux);
		r->fid->aux = reply;
		if(err != nil){
			respond(r, err);
			return;
		}
		r->ofcall.count = r->ifcall.count;
		respond(r, nil);
		return;
	}
	snprint(msg, sizeof(msg), "%.*s",
		utfnlen((char*)r->ifcall.data, r->ifcall.count), (char*)r->ifcall.data);
	nf = tokenize(msg, f, nelem(f));
	if(aux == (void*)Backlight){
		char *err;

		if(nf != 1){
			respond(r, "bad level");
			return;
		}
		qlock(&amllk);
		err = backlightwrite(f[0]);
		amldone();
		if(err != nil){
			respond(r, err);
			return;
		}
	}
	if(aux == (void*)Pmctl){
		char *err;
		int rv;

		/* nfront: one evaluation at a time with the events' */
		err = nil;
		qlock(&amllk);
		if(nf == 2 && strcmp(f[0], "power") == 0 && strcmp(f[1], "off") == 0)
			rv = poweroff();	/* should not go any further here */
		else if(nf == 4 && strcmp(f[0], "charge") == 0)
			rv = tpclampcharge(f[1], atoi(f[2]), atoi(f[3]));
		else {
			rv = 0;
			err = "invalid ctl message";
		}
		amldone();
		if(rv < 0){
			responderror(r);
			return;
		}
		if(err != nil){
			respond(r, err);
			return;
		}
	}
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}

/*
 * 9front-ng: aml, the interpreter for other programs, so that the
 * machine has one (the GPU drivers' methods: _DSM, ATIF, ATCS, _ATRM,
 * _DDC; two interpreters would not know each other's mutexes and
 * embedded controller transactions).  Only the host owner opens it.
 * A write is a request, a read its answer:
 *	eval path nargs value...	the object's value (a method: its result)
 *	walk path|- name	the path of name below path (not found by the
 *				search rules higher up), or of an absolute name
 *	parent path		the scope above
 *	pci bus dev fn		the device of that PCI address (by _ADR)
 *	hid path		a device's _HID as text
 *	devices			"path hid" per device with a _HID
 * Paths are as %N prints them (\_SB.PCI0.GFX0).  A value is
 *	-		none
 *	ihex;		integer
 *	sN:bytes	string of N bytes
 *	bN:hex		buffer of N bytes, 2N hex digits
 *	pN:value...	package of N elements
 *	rN:path		reference to the name path (N bytes)
 */
typedef struct Amlreq Amlreq;
struct Amlreq {
	char	*reply;
	int	nreply;
};

/* name below dot, as ACPICA's handle walks (no search rules upward) */
static void*
amlbelow(void *dot, char *name)
{
	void *x;

	if(dot == nil)
		dot = amlroot;
	if((x = amlwalk(dot, name)) == nil)
		return nil;
	if(name[0] != '\\' && name[0] != '^' && strchr(name, '.') == nil)
		if(amlwalk(x, "^") != dot)
			return nil;
	return x;
}

static void
encval(Fmt *f, void *x, int depth)
{
	void **e;
	uchar *b;
	char *t;
	int i, n;

	if(depth > 8){
		fmtprint(f, "-");
		return;
	}
	x = amlval(x);
	switch(amltag(x)){
	case 'i':
	case 'f':
	case 'u':
		fmtprint(f, "i%llux;", amlint(x));
		return;
	case 's':
	case 'n':
		n = amllen(x);
		fmtprint(f, "s%d:", n);
		fmtprint(f, "%.*s", n, (char*)x);
		return;
	case 'b':
		n = amllen(x);
		b = x;
		fmtprint(f, "b%d:", n);
		for(i = 0; i < n; i++)
			fmtprint(f, "%.2ux", b[i]);
		return;
	case 'p':
		n = amllen(x);
		e = x;
		fmtprint(f, "p%d:", n);
		for(i = 0; i < n; i++)
			encval(f, e[i], depth+1);
		return;
	case 'N':
		t = smprint("%N", x);
		fmtprint(f, "r%d:%s", strlen(t), t);
		free(t);
		return;
	}
	fmtprint(f, "-");
}

static int
hexval(int c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a value as libaml's; nil (and werrstr) if malformed */
static void*
decval(char **pp, char *e, int depth)
{
	char *p, *t;
	void *v, **el;
	uchar *b;
	int i, n, h, l;

	p = *pp;
	if(p >= e || depth > 8){
		werrstr("bad value");
		return nil;
	}
	if(*p == 'i'){
		v = amlnew('i', 0);
		*(uvlong*)v = strtoull(p+1, &t, 16) & amlintmask;
		if(*t != ';'){
			werrstr("bad integer");
			return nil;
		}
		*pp = t+1;
		return v;
	}
	n = strtol(p+1, &t, 10);
	if(*t != ':' || n < 0 || n > e - t){
		werrstr("bad length");
		return nil;
	}
	t++;
	switch(*p){
	case 's':
		v = amlnew('s', n);
		memmove(v, t, n);
		((char*)v)[n] = 0;
		*pp = t + n;
		return v;
	case 'b':
		if(2*n > e - t){
			werrstr("short buffer");
			return nil;
		}
		v = amlnew('b', n);
		b = v;
		for(i = 0; i < n; i++){
			if((h = hexval(t[2*i])) < 0 || (l = hexval(t[2*i+1])) < 0){
				werrstr("bad buffer");
				return nil;
			}
			b[i] = h<<4 | l;
		}
		*pp = t + 2*n;
		return v;
	case 'p':
		v = amlnew('p', n);
		el = v;
		*pp = t;
		for(i = 0; i < n; i++)
			if((el[i] = decval(pp, e, depth+1)) == nil)
				return nil;
		return v;
	case 'r':
		p = smprint("%.*s", n, t);
		v = amlwalk(amlroot, p);
		free(p);
		if(v == nil){
			werrstr("no such name");
			return nil;
		}
		*pp = t + n;
		return v;
	}
	werrstr("bad value type");
	return nil;
}

/* PCI addresses: a root bridge (PNP0A03, PNP0A08) counts _BBN (else 0), a bridge its secondary bus */
static int
amlisroot(void *dev)
{
	void *x;
	char *id;
	int i;
	static char *segs[] = { "_HID", "_CID" };

	for(i = 0; i < nelem(segs); i++){
		if((x = amlbelow(dev, segs[i])) == nil)
			continue;
		if((id = amleisaid(amlval(x))) == nil)
			continue;
		if(strcmp(id, "PNP0A03") == 0 || strcmp(id, "PNP0A08") == 0)
			return 1;
	}
	return 0;
}

static vlong
amlintof(void *dev, char *seg)
{
	void *x, *r;

	if((x = amlbelow(dev, seg)) == nil)
		return -1;
	r = nil;
	if(amleval(x, "", &r) < 0)
		return -1;
	return amlint(r);
}

static int
pcicfg(int bus, int dev, int fn, int write, int off, void *buf, int n)
{
	char path[64];
	int fd, r;

	snprint(path, sizeof path, "#$/pci/%d.%d.%draw", bus, dev, fn);
	if((fd = open(path, write ? ORDWR : OREAD)) < 0)
		return -1;
	r = write ? pwrite(fd, buf, n, off) : pread(fd, buf, n, off);
	close(fd);
	return r;
}

/* the bus the devices below dev count on (pc/archacpi.c's pcibusno) */
static int
amlbusof(void *dev)
{
	void *x;
	vlong adr;
	int bno;
	uchar hdr, sec;

	if(amlisroot(dev)){
		adr = amlintof(dev, "_BBN");
		return adr < 0 ? 0 : adr;
	}
	if((adr = amlintof(dev, "_ADR")) < 0)
		return -1;
	if((x = amlwalk(dev, "^")) == nil || x == dev || (bno = amlbusof(x)) < 0)
		return -1;
	if(pcicfg(bno, adr>>16, adr&0xFFFF, 0, 0x0E, &hdr, 1) != 1 || (hdr & 0x7F) != 1)
		return bno;
	if(pcicfg(bno, adr>>16, adr&0xFFFF, 0, 0x19, &sec, 1) != 1)
		return -1;
	return sec;
}

typedef struct Adrfind Adrfind;
struct Adrfind {
	void	*parent;
	vlong	adr;
	void	*found;
	int	bus;
	void	*roots[8];
	int	nroots;
};

static int
adrof(void *x, void *a)
{
	Adrfind *f;
	void *dev, *r;

	f = a;
	if(f->found != nil)
		return 0;
	dev = amlwalk(x, "^");
	if(dev == nil || amlwalk(dev, "^") != f->parent)
		return 0;
	r = nil;
	if(amleval(x, "", &r) >= 0 && amlint(r) == f->adr)
		f->found = dev;
	return 0;
}

static int
rootof(void *x, void *a)
{
	Adrfind *f;
	void *dev;
	int i;

	f = a;
	if((dev = amlwalk(x, "^")) == nil || f->nroots >= nelem(f->roots))
		return 0;
	for(i = 0; i < f->nroots; i++)
		if(f->roots[i] == dev)
			return 0;
	if(amlisroot(dev))
		f->roots[f->nroots++] = dev;
	return 0;
}

static void*
amlchild(void *parent, int dev, int fn)
{
	Adrfind f;

	memset(&f, 0, sizeof f);
	f.parent = parent;
	f.adr = (vlong)dev<<16 | fn;
	amlenum(parent, "_ADR", adrof, &f);
	return f.found;
}

/* the bridge whose secondary bus is bus: its address in *b *d *f */
static int
bridgeto(int bus, int *b, int *d, int *f)
{
	Dir *dir;
	long i, n;
	int fd, bb, dd, ff;
	uchar hdr, sec;
	char *s;

	if((fd = open("#$/pci", OREAD)) < 0)
		return -1;
	n = dirreadall(fd, &dir);
	close(fd);
	for(i = 0; i < n; i++){
		s = dir[i].name;
		bb = strtol(s, &s, 10);
		if(*s++ != '.')
			continue;
		dd = strtol(s, &s, 10);
		if(*s++ != '.')
			continue;
		ff = strtol(s, &s, 10);
		if(strcmp(s, "ctl") != 0)
			continue;
		if(pcicfg(bb, dd, ff, 0, 0x0E, &hdr, 1) != 1 || (hdr & 0x7F) != 1)
			continue;
		if(pcicfg(bb, dd, ff, 0, 0x19, &sec, 1) != 1 || sec != bus)
			continue;
		*b = bb;
		*d = dd;
		*f = ff;
		free(dir);
		return 0;
	}
	free(dir);
	return -1;
}

static void*
amlpcinode(int bus, int dev, int fn, int depth)
{
	Adrfind f;
	void *x;
	vlong bbn;
	int i, b, d, ff;

	if(depth > 16)
		return nil;
	memset(&f, 0, sizeof f);
	amlenum(amlroot, "_HID", rootof, &f);
	amlenum(amlroot, "_CID", rootof, &f);
	for(i = 0; i < f.nroots; i++){
		if((bbn = amlintof(f.roots[i], "_BBN")) < 0)
			bbn = 0;
		if(bbn == bus && (x = amlchild(f.roots[i], dev, fn)) != nil)
			return x;
	}
	if(bridgeto(bus, &b, &d, &ff) < 0)
		return nil;
	if((x = amlpcinode(b, d, ff, depth+1)) == nil)
		return nil;
	return amlchild(x, dev, fn);
}

static int
devhid(void *x, void *a)
{
	Fmt *f;
	void *dev;
	char *id;

	f = a;
	if((dev = amlwalk(x, "^")) == nil || (id = amleisaid(amlval(x))) == nil)
		return 0;
	fmtprint(f, "%N %s\n", dev, id);
	return 0;
}

/* the request in buf; the answer in *reply (malloc'd); nil or the error */
static char*
amlrequest(char *buf, int n, char **reply)
{
	char *f[4], *p, *e, *line;
	void *x, *a[7], *r;
	Fmt fmt;
	int nf, na, i, rv;

	*reply = nil;
	e = buf + n;
	/* the command and its first words; values (eval) may hold spaces */
	if((line = malloc(n + 1)) == nil)
		return "out of memory";
	memmove(line, buf, n);
	line[n] = 0;
	nf = getfields(line, f, nelem(f), 1, " \t\n");
	fmtstrinit(&fmt);
	if(nf >= 3 && strcmp(f[0], "eval") == 0){
		if((x = amlwalk(amlroot, f[1])) == nil){
			free(line);
			free(fmtstrflush(&fmt));
			return "not found";
		}
		na = atoi(f[2]);
		if(na < 0 || na > nelem(a)){
			free(line);
			free(fmtstrflush(&fmt));
			return "too many arguments";
		}
		/* the values begin after the third word */
		p = buf + (f[2] - line) + strlen(f[2]);
		while(p < e && (*p == ' ' || *p == '\t'))
			p++;
		for(i = 0; i < na; i++)
			if((a[i] = decval(&p, e, 0)) == nil){
				free(line);
				free(fmtstrflush(&fmt));
				return "bad argument";
			}
		r = nil;
		switch(na){
		case 0:	rv = amleval(x, "", &r); break;
		case 1:	rv = amleval(x, "*", a[0], &r); break;
		case 2:	rv = amleval(x, "**", a[0], a[1], &r); break;
		case 3:	rv = amleval(x, "***", a[0], a[1], a[2], &r); break;
		case 4:	rv = amleval(x, "****", a[0], a[1], a[2], a[3], &r); break;
		case 5:	rv = amleval(x, "*****", a[0], a[1], a[2], a[3], a[4], &r); break;
		case 6:	rv = amleval(x, "******", a[0], a[1], a[2], a[3], a[4], a[5], &r); break;
		default:	rv = amleval(x, "*******", a[0], a[1], a[2], a[3], a[4], a[5], a[6], &r); break;
		}
		if(rv < 0){
			free(line);
			free(fmtstrflush(&fmt));
			return "evaluation failed";
		}
		encval(&fmt, r, 0);
	}else if(nf == 3 && strcmp(f[0], "walk") == 0){
		x = strcmp(f[1], "-") == 0 ? amlroot : amlwalk(amlroot, f[1]);
		if(x == nil || (x = amlbelow(x, f[2])) == nil){
			free(line);
			free(fmtstrflush(&fmt));
			return "not found";
		}
		fmtprint(&fmt, "%N", x);
	}else if(nf == 2 && strcmp(f[0], "parent") == 0){
		if((x = amlwalk(amlroot, f[1])) == nil || (r = amlwalk(x, "^")) == nil || r == x){
			free(line);
			free(fmtstrflush(&fmt));
			return "not found";
		}
		fmtprint(&fmt, "%N", r);
	}else if(nf == 4 && strcmp(f[0], "pci") == 0){
		if((x = amlpcinode(atoi(f[1]), atoi(f[2]), atoi(f[3]), 0)) == nil){
			free(line);
			free(fmtstrflush(&fmt));
			return "not found";
		}
		fmtprint(&fmt, "%N", x);
	}else if(nf == 2 && strcmp(f[0], "hid") == 0){
		if((x = amlwalk(amlroot, f[1])) == nil || (x = amlbelow(x, "_HID")) == nil
		|| (p = amleisaid(amlval(x))) == nil){
			free(line);
			free(fmtstrflush(&fmt));
			return "not found";
		}
		fmtprint(&fmt, "%s", p);
	}else if(nf == 1 && strcmp(f[0], "devices") == 0){
		amlenum(amlroot, "_HID", devhid, &fmt);
	}else{
		free(line);
		free(fmtstrflush(&fmt));
		return "bad request";
	}
	free(line);
	*reply = fmtstrflush(&fmt);
	return nil;
}

/*
 * nfront: ACPI's events.  A proc reads the kernel's #P/acpiev (the SCI's
 * general-purpose events and PM1's fixed ones, masked until written
 * back), runs each GPE's method (\_GPE._Lxx, _Exx; the embedded
 * controller's: its queries' _Qxx), and the Notify those methods make
 * (libaml's amlnotify) become lines of the file acpievents:
 *	notify path 0xV		each Notify as it is
 *	key brightnessup|brightnessdown|brightnesszero|brightnesscycle|
 *	    displayoff|displayswitch
 *				a display output's 0x85-0x89, a display
 *				adapter's 0x80 (the ACPI video extension), a
 *				ThinkPad's hotkeys 0x1010, 0x1011
 *	hotkey 0xN		a ThinkPad's other hotkeys (MHKP)
 *	button power|sleep	PM1's fixed buttons, PNP0C0C, PNP0C0E
 *	lid open|closed		PNP0C0D (_LID)
 *	ac online|offline	ACPI0003 (_PSR)
 *	battery			PNP0C0A changed
 * A reader gets the lines from its open on; one that falls 256 lines
 * behind loses the oldest.
 *
 * What is done with which event follows OpenBSD's sys/dev/acpi, whose
 * notices these are:
 *
 * acpi.c,v 1.458 (the GPEs' methods, PM1's buttons):
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2005 Jordan Hargrave <jordan@openbsd.org>
 * acpiec.c,v 1.67 (the queries):
 * Copyright (c) 2006 Can Erkin Acar <canacar@openbsd.org>
 * acpivout.c,v 1.26 (a display output's notifications):
 * Copyright (c) 2009 Paul Irofti <paul@irofti.net>
 * acpivideo.c,v 1.14 (a display adapter's):
 * Copyright (c) 2008 Federico G. Schwindt <fgsch@openbsd.org>
 * Copyright (c) 2009 Paul Irofti <paul@irofti.net>
 * acpithinkpad.c,v 1.74 (the hotkeys: MHKA, MHKM, MHKC, MHKP):
 * Copyright (c) 2008 joshua stein <jcs@openbsd.org>
 * acpibtn.c,v 1.56 (lid and buttons), acpiac.c,v 1.36 (AC adapter):
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
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
	Nev	= 256,	/* lines kept for readers */
	Nnote	= 64,	/* Notifys of one evaluation */

	Pwrbtn	= 1<<8,	/* PM1 status and enable */
	Slpbtn	= 1<<9,
};

typedef struct Evreader Evreader;
struct Evreader {
	Evreader	*next;
	uvlong	seq;	/* the next line it reads */
	Req	*r;	/* its read, waiting */
};

static struct {
	QLock;
	char	*line[Nev];
	uvlong	seq;	/* lines so far */
	Evreader	*readers;
} events;

static struct {
	void	*dot;
	uvlong	v;
} notes[Nnote];
static int nnotes;

static int ecq = -1;		/* #P/ecquery */
static int evfd = -1;		/* #P/acpiev */
static ulong facpflags;

/* the lines a reader has not read, as many as its read takes; events held */
static void
evanswer(Evreader *e)
{
	char *s;
	long n, m;
	Req *r;

	if((r = e->r) == nil || e->seq == events.seq)
		return;
	if(events.seq - e->seq > Nev)
		e->seq = events.seq - Nev;
	s = r->ofcall.data;
	n = 0;
	while(e->seq < events.seq){
		m = strlen(events.line[e->seq % Nev]);
		if(n + m > r->ifcall.count)
			break;
		memmove(s + n, events.line[e->seq % Nev], m);
		n += m;
		e->seq++;
	}
	if(n == 0 && e->seq < events.seq){
		/* a line longer than the read */
		respond(r, "read too short");
		e->r = nil;
		return;
	}
	r->ofcall.count = n;
	e->r = nil;
	respond(r, nil);
}

static void
evpost(char *fmt, ...)
{
	Evreader *e;
	va_list arg;
	char *s;

	va_start(arg, fmt);
	s = vsmprint(fmt, arg);
	va_end(arg);
	if(s == nil)
		return;
	qlock(&events);
	free(events.line[events.seq % Nev]);
	events.line[events.seq % Nev] = smprint("%s\n", s);
	events.seq++;
	for(e = events.readers; e != nil; e = e->next)
		evanswer(e);
	qunlock(&events);
	free(s);
}

/* libaml's Notify, inside an evaluation: noted, handled after it */
static void
amlnote(void *dot, uvlong v)
{
	if(nnotes < Nnote){
		notes[nnotes].dot = dot;
		notes[nnotes].v = v;
		nnotes++;
	}
}

static char*
devhidof(void *dev)
{
	void *p;

	if((p = amlbelow(dev, "_HID")) == nil)
		return nil;
	if(amleval(p, "", &p) < 0)
		return nil;
	return amleisaid(p);
}

static int
evalint(void *dev, char *name, uvlong *v)
{
	void *p, *r;

	if((p = amlbelow(dev, name)) == nil || amleval(p, "", &r) < 0)
		return -1;
	*v = amlint(r);
	return 0;
}

/* a Notify: the line as it is, and what it means where that is known */
static void
evnote(void *dev, uvlong v)
{
	static char *vout[] = {
		[0x85-0x85] "brightnesscycle", "brightnessup", "brightnessdown",
		"brightnesszero", "displayoff",
	};
	char *hid;
	uvlong x;
	int i;

	evpost("notify %N %#llux", dev, v);
	hid = devhidof(dev);
	if(dev == hkey && v == 0x80){
		/* acpithinkpad: the hotkeys queued, until MHKP gives 0 */
		for(i = 0; i < 64 && evalint(dev, "MHKP", &x) == 0 && x != 0; i++){
			if(x == 0x1010)
				evpost("key brightnessup");
			else if(x == 0x1011)
				evpost("key brightnessdown");
			else
				evpost("hotkey %#llux", x);
		}
		return;
	}
	if(hid != nil && strcmp(hid, "PNP0C0D") == 0 && v == 0x80){
		if(evalint(dev, "_LID", &x) == 0)
			evpost("lid %s", x ? "open" : "closed");
		return;
	}
	if(hid != nil && strcmp(hid, "PNP0C0C") == 0 && v == 0x80){
		evpost("button power");
		return;
	}
	if(hid != nil && strcmp(hid, "PNP0C0E") == 0 && v == 0x80){
		evpost("button sleep");
		return;
	}
	if(hid != nil && strcmp(hid, "ACPI0003") == 0 && v == 0x80){
		if(evalint(dev, "_PSR", &x) == 0)
			evpost("ac %s", x ? "online" : "offline");
		return;
	}
	if(hid != nil && strcmp(hid, "PNP0C0A") == 0 && (v == 0x80 || v == 0x81)){
		evpost("battery");
		return;
	}
	/* acpivout: an output with brightness levels; acpivideo: its adapter */
	if(v >= 0x85 && v <= 0x89 && (amlbelow(dev, "_BCM") != nil || amlbelow(dev, "_BCL") != nil)){
		evpost("key %s", vout[v - 0x85]);
		return;
	}
	if(v == 0x80 && (amlbelow(dev, "_DOS") != nil || amlbelow(dev, "_DOD") != nil))
		evpost("key displayswitch");
}


/* the evaluations done: their Notifys handled (which may evaluate more) */
static void
amldone(void)
{
	void *dot;
	uvlong v;
	int i;

	/* (evnote's evaluations may note more: up to Nnote in all) */
	for(i = 0; i < nnotes; i++){
		dot = notes[i].dot;
		v = notes[i].v;
		evnote(dot, v);
	}
	nnotes = 0;
	qunlock(&amllk);
}

/* acpiec: the queries while the controller has events, each its _Qxx */
static void
ecevents(void)
{
	char buf[16], name[8];
	void *m;
	int i, n, q;

	for(i = 0; i < 64; i++){
		if((n = pread(ecq, buf, sizeof buf - 1, 0)) <= 0)
			break;
		buf[n] = 0;
		if((q = strtol(buf, nil, 0)) <= 0)
			break;
		snprint(name, sizeof name, "_Q%.2X", q);
		if((m = amlbelow(ecdev, name)) != nil)
			amleval(m, "", nil);
	}
}

static void
evproc(void *)
{
	char buf[512], name[8], *f[64], *l[2];
	void *gpe, *m;
	int i, n, nf, g, pm, edge;

	threadsetname("acpievents");
	gpe = amlwalk(amlroot, "\\_GPE");
	for(;;){
		if((n = read(evfd, buf, sizeof buf - 1)) <= 0){
			fprint(2, "aux/acpi: #P/acpiev: %r\n");
			return;
		}
		buf[n] = 0;
		nf = getfields(buf, f, nelem(f), 1, "\n");
		for(i = 0; i < nf; i++){
			if(tokenize(f[i], l, nelem(l)) != 2)
				continue;
			if(strcmp(l[0], "pm1") == 0){
				pm = strtoul(l[1], nil, 0);
				if(pm & Pwrbtn)
					evpost("button power");
				if(pm & Slpbtn)
					evpost("button sleep");
				fprint(evfd, "pm1 %#ux ack", pm);
				continue;
			}
			if(strcmp(l[0], "gpe") != 0)
				continue;
			g = atoi(l[1]);
			edge = 0;
			qlock(&amllk);
			if(g == ecgpe && ecq >= 0)
				ecevents();
			else if(gpe != nil){
				snprint(name, sizeof name, "_E%.2X", g);
				if((m = amlbelow(gpe, name)) != nil){
					/* an edge: cleared before, another one is not lost */
					edge = 1;
					fprint(evfd, "gpe %d clear", g);
				}else{
					snprint(name, sizeof name, "_L%.2X", g);
					m = amlbelow(gpe, name);
				}
				if(m != nil)
					amleval(m, "", nil);
			}
			amldone();
			fprint(evfd, "gpe %d %s", g, edge ? "enable" : "ack");
		}
	}
}

/*
 * acpivideo's acpivideo_set_policy: a display adapter's outputs and its
 * panel's brightness are the OS's (_DOS 4: DOS_SWITCH_BY_OSPM |
 * DOS_BRIGHTNESS_BY_OSPM), so the firmware notifies the keys (0x86,
 * 0x87) instead of acting on them, or of not sending them at all
 */
static int
enumdos(void *dot, void *)
{
	amleval(dot, "i", 4, nil);
	return 1;
}

/*
 * the events this machine has: each GPE with a method, the EC's, PM1's
 * fixed buttons (where FADT's flags do not say they are devices), a
 * ThinkPad's hotkeys
 */
static void
evstart(void)
{
	char name[8];
	void *gpe;
	int g;

	if((evfd = open("/dev/acpiev", ORDWR)) < 0 && (evfd = open("#P/acpiev", ORDWR)) < 0)
		return;
	if((ecq = open("/dev/ecquery", OREAD)) < 0)
		ecq = open("#P/ecquery", OREAD);
	if((gpe = amlwalk(amlroot, "\\_GPE")) != nil)
		for(g = 0; g < 256; g++){
			snprint(name, sizeof name, "_L%.2X", g);
			if(amlbelow(gpe, name) == nil){
				snprint(name, sizeof name, "_E%.2X", g);
				if(amlbelow(gpe, name) == nil)
					continue;
			}
			fprint(evfd, "gpe %d ack", g);
		}
	if(ecgpe >= 0 && ecq >= 0)
		fprint(evfd, "gpe %d ack", ecgpe);
	if((facpflags & 1<<4) == 0)
		fprint(evfd, "pm1 %#ux ack", Pwrbtn);
	if((facpflags & 1<<5) == 0)
		fprint(evfd, "pm1 %#ux ack", Slpbtn);
	amlnotify = amlnote;
	amlenum(amlroot, "_DOS", enumdos, nil);
	proccreate(evproc, nil, 64*1024);
}

static void
fsevread(Req *r)
{
	Evreader *e;

	qlock(&events);
	if((e = r->fid->aux) == nil){
		e = emalloc9p(sizeof *e);
		e->seq = events.seq;
		e->next = events.readers;
		events.readers = e;
		r->fid->aux = e;
	}
	if(e->r != nil){
		qunlock(&events);
		respond(r, "one read at a time");
		return;
	}
	e->r = r;
	evanswer(e);
	qunlock(&events);
}

static void
fsflush(Req *r)
{
	Evreader *e;

	qlock(&events);
	for(e = events.readers; e != nil; e = e->next)
		if(e->r == r->oldreq){
			e->r = nil;
			respond(r->oldreq, "interrupted");
			break;
		}
	qunlock(&events);
	respond(r, nil);
}

static void
evreaderfree(Evreader *e)
{
	Evreader **l;

	qlock(&events);
	for(l = &events.readers; *l != nil; l = &(*l)->next)
		if(*l == e){
			*l = e->next;
			break;
		}
	qunlock(&events);
	free(e);
}

/*
 * nfront: the panel's brightness through the firmware (the ACPI video
 * extension: _BCL's levels, _BCM sets one, _BQC reads it back), for a
 * panel no GPU driver serves (VESA): aux/acpikeys takes it when gpu(4)
 * has no backlight.  As gpu(4)'s: reading gives "level 100", writing n or
 * n% the level nearest to it (one level on, if that is the one set).
 * As OpenBSD's acpivout (sys/dev/acpi/acpivout.c,v 1.26) does it:
 * Copyright (c) 2009 Paul Irofti <paul@irofti.net>, ISC license (above).
 */
static void *bcldev;		/* the output */
static int bcl[101];		/* its levels, ascending */
static int nbcl;
static int bclcur = -1;		/* the level set last, -1: none yet */

static int
enumbcm(void *dot, void *)
{
	void *dev, *p, **e;
	uvlong adr;
	int i, j, n, v;

	if((dev = amlwalk(dot, "^")) == nil || (p = amlwalk(dot, "^_BCL")) == nil)
		return 1;
	/* the internal flat panel (_ADR's display type 4) before others */
	if(bcldev != nil && (evalint(dev, "_ADR", &adr) < 0 || (adr & 0xF00) != 0x400))
		return 1;
	if(amleval(p, "", &p) < 0 || amltag(p) != 'p' || (n = amllen(p)) < 3)
		return 1;
	e = amlval(p);
	nbcl = 0;
	/* the first two are the levels on AC and on battery: the list follows */
	for(i = 2; i < n && nbcl < nelem(bcl); i++){
		v = amlint(e[i]);
		if(v < 0 || v > 100)
			continue;
		for(j = 0; j < nbcl && bcl[j] != v; j++)
			;
		if(j < nbcl)
			continue;
		for(j = nbcl; j > 0 && bcl[j-1] > v; j--)
			bcl[j] = bcl[j-1];
		bcl[j] = v;
		nbcl++;
	}
	if(nbcl > 0){
		if(bcldev != nil)
			amldrop(bcldev);
		bcldev = dev;
		amltake(bcldev);
	}
	return 1;
}

static void
backlightread(char *s, char *e)
{
	uvlong v;

	if(bcldev != nil && evalint(bcldev, "_BQC", &v) == 0 && v <= 100)
		bclcur = v;
	seprint(s, e, "%d 100\n", bclcur >= 0 ? bclcur : bcl[nbcl-1]);
}

static char*
backlightwrite(char *s)
{
	int i, best, v, cur;
	uvlong q;
	void *m;

	v = strtol(s, nil, 0);
	if(v < 0 || v > 100)
		return "bad level";
	cur = bclcur;
	if(evalint(bcldev, "_BQC", &q) == 0 && q <= 100)
		cur = q;
	best = 0;
	for(i = 1; i < nbcl; i++)
		if(abs(bcl[i] - v) < abs(bcl[best] - v))
			best = i;
	/* the level set already: one on, toward v */
	if(bcl[best] == cur && v != cur){
		if(v > cur && best+1 < nbcl)
			best++;
		else if(v < cur && best > 0)
			best--;
	}
	if((m = amlbelow(bcldev, "_BCM")) == nil || amleval(m, "i", bcl[best], nil) < 0)
		return "_BCM failed";
	bclcur = bcl[best];
	return nil;
}

static void
fsdestroyfid(Fid *f)
{
	if(f->file != nil && f->file->aux == (void*)Events){
		if(f->aux != nil)
			evreaderfree(f->aux);
		return;
	}
	free(f->aux);
}

static void
usage(void)
{
	fprint(2, "usage: aux/acpi [-DHp] [-m mountpoint] [-s service]\n");
	exits("usage");
}

static Srv fs = {
	.read = fsread,
	.write = fswrite,
	.flush = fsflush,
	.destroyfid = fsdestroyfid,
};



void
threadmain(int argc, char **argv)
{
	char *mtpt, *srv;
	void *r, **rr;
	int fd, n, l, halt;
	Tbl *t;

	mtpt = "/dev";
	srv = nil;
	halt = 0;
	ARGBEGIN{
	case 'D':
		chatty9p = 1;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'p':
		amldebug++;
		break;
	case 'H':
		halt = 1;
		break;
	default:
		usage();
	}ARGEND

	/*
	 * 9front-ng: one interpreter on the machine (aml: the GPU drivers
	 * use this one), posted as /srv/acpi unless -s names it.  If it
	 * runs already (a GPU driver may have started it), its files are
	 * mounted here instead, as this one would have mounted its own.
	 */
	if(srv == nil)
		srv = "acpi";
	if(!halt){
		char path[64];
		int sfd;

		snprint(path, sizeof path, "/srv/%s", srv);
		if((sfd = open(path, ORDWR)) >= 0){
			if(mount(sfd, -1, mtpt, MAFTER, "") < 0)
				sysfatal("%s runs already; mount on %s: %r", path, mtpt);
			fprint(2, "aux/acpi: runs already: %s mounted on %s\n", path, mtpt);
			threadexitsall(nil);
		}
	}

	if((ec = open("/dev/ec", ORDWR)) < 0)
		ec = open("#P/ec", ORDWR);
	if((mem = open("/dev/acpimem", ORDWR)) < 0)
		mem = open("#P/acpimem", ORDWR);
	if((iofd[1] = open("/dev/iob", ORDWR)) < 0)
		if((iofd[1] = open("#P/iob", ORDWR)) < 0)
			goto fail;
	if((iofd[2] = open("/dev/iow", ORDWR)) < 0)
		if((iofd[2] = open("#P/iow", ORDWR)) < 0)
			goto fail;
	if((iofd[4] = open("/dev/iol", ORDWR)) < 0)
		if((iofd[4] = open("#P/iol", ORDWR)) < 0)
			goto fail;
	if((fd = open("/dev/acpitbls", OREAD)) < 0)
		if((fd = open("#P/acpitbls", OREAD)) < 0)
			goto fail;

	amlinit();
	for(;;){
		t = emalloc9p(sizeof(*t));
		if((n = readn(fd, t, Tblsz)) <= 0){
			free(t);
			break;
		}
		if(n != Tblsz)
			goto fail;
		l = get32(t->len);
		if(l < Tblsz)
			goto fail;
		l -= Tblsz;
		t = erealloc9p(t, sizeof(*t) + l);
		if(readn(fd, t->data, l) != l)
			goto fail;
		if(memcmp("DSDT", t->sig, 4) == 0){
			amlintmask = (~0ULL) >> (t->rev <= 1)*32;
			/* amlload keeps pointers from t, so don't free it */
			amlload(t->data, l);
		}else if(memcmp("SSDT", t->sig, 4) == 0){
			/* amlload keeps pointers from t, so don't free it */
			amlload(t->data, l);
		}else if(memcmp("FACP", t->sig, 4) == 0){
			facp.ok = 1;
			if(l >= 116 - Tblsz)
				facpflags = get32(((uchar*)t) + 112);	/* nfront: the buttons' kind */
			if(t->rev >= 3) {
				/* try the ACPI 2.0 method */
				facp.pm1aspace = *(((uchar*)t) + 172);
				facp.pm1awid = *(((uchar*)t) + 175);
				facp.pm1a = get64(((uchar*)t) + 176);

				facp.pm1bspace = *(((uchar*)t) + 184);
				facp.pm1bwid = *(((uchar*)t) + 187);
				facp.pm1b = get64(((uchar*)t) + 188);

				facp.gpe0space = *(((uchar*)t) + 220);
				facp.gpe0wid = *(((uchar*)t) + 223);
				facp.gpe0 = get64(((uchar*)t) + 224);

				facp.gpe1space = *(((uchar*)t) + 232);
				facp.gpe1wid = *(((uchar*)t) + 235);
				facp.gpe1 = get64(((uchar*)t) + 236);
			}

			/* fall back to the ACPI 1.0 io port method */
			if(facp.pm1a == 0 || facp.pm1awid == 0) {
				facp.pm1aspace = IoSpace;
				facp.pm1awid = 2;
				facp.pm1a = get32(((uchar*)t) + 64);
			}

			if(facp.pm1b == 0 || facp.pm1bwid == 0) {
				facp.pm1bspace = IoSpace;
				facp.pm1bwid = 2;
				facp.pm1b = get32(((uchar*)t) + 68);
			}

			if(facp.gpe0 == 0 || facp.gpe0wid == 0) {
				facp.gpe0space = IoSpace;
				facp.gpe0wid = 2;
				facp.gpe0 = get32(((uchar*)t) + 80);
				facp.gpe0len = *(((uchar*)t) + 92);
			}

			if(facp.gpe1 == 0 || facp.gpe1wid == 0) {
				facp.gpe1space = IoSpace;
				facp.gpe1wid = 2;
				facp.gpe1 = get32(((uchar*)t) + 84);
				facp.gpe1len = *(((uchar*)t) + 93);
			}
			free(t);
		}else{
			free(t);
		}
	}
	if(amleval(amlwalk(amlroot, "_S5"), "", &r) >= 0 && amltag(r) == 'p' && amllen(r) >= 2){
		rr = amlval(r);
		facp.slpa = amlint(rr[0]);
		facp.slpb = amlint(rr[1]);
	}
	close(fd);

	if(halt && poweroff() < 0)
		sysfatal("%r");

	amlenum(amlroot, "_HID", enumec, nil);
	amlenum(amlroot, "_INI", enumini, nil);	/* nfront: after the EC's _REG */
	amlenum(amlroot, "_BIF", enumbat, nil);
	amlenum(amlroot, "_PSL", enumtmp, nil);
	amlenum(amlroot, "_HID", enumtp, nil);
	amlenum(amlroot, "_BCM", enumbcm, nil);	/* nfront */

	fs.tree = alloctree(uid, uid, DMDIR|0555, nil);
	if(nbats > 0)
		createfile(fs.tree->root, "battery", uid, 0444, (void*)Battery);
	if(ntherms > 0)
		createfile(fs.tree->root, "cputemp", uid, 0444, (void*)Temp);
	createfile(fs.tree->root, "pmctl", uid, 0666, (void*)Pmctl);
	createfile(fs.tree->root, "aml", getuser(), 0600, (void*)Aml);

	/* nfront: the events, where the kernel has them (#P/acpiev) */
	evstart();
	if(evfd >= 0)
		createfile(fs.tree->root, "acpievents", uid, 0444, (void*)Events);
	if(nbcl > 0)
		createfile(fs.tree->root, "backlight", getuser(), 0644, (void*)Backlight);

	threadpostmountsrv(&fs, srv, mtpt, MAFTER);
	threadexits(nil);

fail:
	sysfatal("%r");
}

static int
readec(Amlio *, void *data, int len, int off)
{
	return pread(ec, data, len, off);
}

static int
writeec(Amlio *, void *data, int len, int off)
{
	return pwrite(ec, data, len, off);
}

static int
readio(Amlio *io, void *data, int len, int port)
{
	assert(len == 1 || len == 2 || len == 4);
	return pread(iofd[len], data, len, io->off+port);
}

static int
writeio(Amlio *io, void *data, int len, int port)
{
	assert(len == 1 || len == 2 || len == 4);
	return pwrite(iofd[len], data, len, io->off+port);
}

static int
memread(Amlio *io, void *data, int len, int addr)
{
	return pread(mem, data, len, io->off+addr);
}

static int
memwrite(Amlio *io, void *data, int len, int addr)
{
	return pwrite(mem, data, len, io->off+addr);
}

static int
dummy(Amlio *, void *, int len, int)
{
	return len;
}

/*
 * 9front-ng: PCI configuration space regions (a GPU's methods use them),
 * at the PCI address of the device the region is in (_ADR, and the
 * bus as pc/archacpi.c's pciaddr finds it)
 */
typedef struct Pciaddr Pciaddr;
struct Pciaddr {
	int	bus, dev, fn;
};

static int amlbusof(void*);
static vlong amlintof(void*, char*);
static void *amlbelow(void*, char*);
static int pcicfg(int, int, int, int, int, void*, int);

static int
readpci(Amlio *io, void *data, int len, int off)
{
	Pciaddr *a;

	a = io->aux;
	off += io->off;
	if(off < 0 || off + len > 4096)
		return -1;
	return pcicfg(a->bus, a->dev, a->fn, 0, off, data, len);
}

static int
writepci(Amlio *io, void *data, int len, int off)
{
	Pciaddr *a;

	a = io->aux;
	off += io->off;
	if(off < 0 || off + len > 4096)
		return -1;
	return pcicfg(a->bus, a->dev, a->fn, 1, off, data, len);
}

static Pciaddr*
pciaddrof(void *dot)
{
	Pciaddr *a;
	void *x;
	vlong adr;
	int bno;

	for(;;){
		if(amlbelow(dot, "_ADR") == nil){
			if((x = amlwalk(dot, "^")) == nil || x == dot)
				return nil;
			dot = x;
			continue;
		}
		if((adr = amlintof(dot, "_ADR")) < 0)
			return nil;
		if((x = amlwalk(dot, "^")) == nil || (bno = amlbusof(x)) < 0)
			return nil;
		if((a = malloc(sizeof(*a))) == nil)
			return nil;
		a->bus = bno;
		a->dev = adr >> 16;
		a->fn = adr & 0xFFFF;
		return a;
	}
}

int
amlmapio(Amlio *io)
{
	switch(io->space){
	case EbctlSpace:
		io->read = ec >= 0 ? readec : dummy;
		io->write = ec >= 0 ? writeec : dummy;
		break;
	case IoSpace:
		io->read = readio;
		io->write = writeio;
		break;
	case MemSpace:
		io->read = mem >= 0 ? memread : dummy;
		io->write = mem >= 0 ? memwrite : dummy;
		break;
	case PcicfgSpace:
		if((io->aux = pciaddrof(io->name)) == nil){
			io->read = dummy;
			io->write = dummy;
			break;
		}
		io->read = readpci;
		io->write = writepci;
		break;
	default:
		io->read = dummy;
		io->write = dummy;
		break;
	}
	return 0;
}

void
amlunmapio(Amlio *io)
{
	if(io->space == PcicfgSpace && io->read == readpci){
		free(io->aux);
		io->aux = nil;
	}
}

void
amldelay(uvlong us)
{
	sleep(us/1000);
}

uvlong
amltime(void)
{
	return nsec()/100;
}
