/*
 * aux/acpikeys: the brightness keys aux/acpi reports (its acpievents:
 * key brightnessup, brightnessdown, brightnesszero) on the backlight of
 * every panel a GPU driver serves (gpu(4): /srv/gpu.N, its
 * outputs/conn/backlight), one of sixteen steps at a time on a curve
 * the eye sees as even (max * (k/16)^3, as brightnessctl -e: a linear
 * sixteenth was hardly seen near the top, coarse near the bottom),
 * never dark (a thirty-second at least) but by brightnesszero.  The drivers
 * are looked for at each key: one started later is found.  Without one
 * that has a backlight, aux/acpi's (/dev/backlight: the firmware's
 * levels, _BCM) is taken.
 * 9space: the lid (lid closed, lid open): closed, the panels dark and the
 * processors' clock low (pstate(8)); open, both as they were.  No
 * suspend yet; the machine runs on.
 *	aux/acpikeys [-f events]
 */
#include <u.h>
#include <libc.h>

static char *evfile = "/dev/acpievents";

/* what closing the lid changed, to be put back when it opens */
enum {
	Nsaved	= 16,
};
static struct {
	char	path[256];
	long	val;
} saved[Nsaved];
static int nsaved;
static char cursrv[32];	/* the GPU driver's srv mounted on /mnt now: two have the same paths there */
static int lidclosed;
static char pspolicy[16];	/* pstate's policy before, "" if none */

enum {
	Nstep	= 16,
	Gamma	= 3,	/* brightness = max * (k/Nstep)^Gamma */
};

/* the value of step k */
static long
stepval(long max, int k)
{
	return (long)(max * pow((double)k/Nstep, Gamma) + 0.5);
}

/* the backlight's next value from cur, dir steps on the curve */
static long
next(long cur, long max, int dir)
{
	long v;
	int k;

	/* the step cur is at (rounded), then dir steps on */
	k = (int)(Nstep * pow((double)cur/max, 1.0/Gamma) + 0.5) + dir;
	if(k > Nstep)
		k = Nstep;
	if(k < 0)
		k = 0;
	v = stepval(max, k);
	/* a cur between steps: at least one unit in dir */
	if(dir > 0 && v <= cur)
		v = cur + 1;
	if(dir < 0 && v >= cur)
		v = cur - 1;
	if(v > max)
		v = max;
	if(v < max/32)
		v = max/32 > 0 ? max/32 : 1;
	return v;
}

/* the backlight file path moved by dir steps; 0, or -1 if it is none */
static int
step(char *path, int dir)
{
	char buf[64], *f[3];
	int fd, n;
	long cur, max, v;

	if((fd = open(path, ORDWR)) < 0)
		return -1;
	n = pread(fd, buf, sizeof buf - 1, 0);
	if(n <= 0){
		close(fd);
		return -1;
	}
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) != 2 || (max = strtol(f[1], nil, 0)) <= 0){
		close(fd);
		return -1;
	}
	cur = strtol(f[0], nil, 0);
	if(dir == 0)
		v = 0;
	else {
		v = next(cur, max, dir);
	}
	if(v != cur && fprint(fd, "%ld", v) < 0)
		fprint(2, "acpikeys: %s: %r\n", path);
	close(fd);
	return 0;
}

/*
 * the lid closed (dir -1): the backlight path saved and dark;
 * opened (dir 1): as saved; 0, or -1 if it is none
 */
static int
lidlight(char *path, int dir)
{
	char buf[64], key[256], *f[3];
	int fd, n, i;

	snprint(key, sizeof key, "%s:%s", cursrv, path);
	if((fd = open(path, ORDWR)) < 0)
		return -1;
	n = pread(fd, buf, sizeof buf - 1, 0);
	if(n <= 0 || (buf[n] = 0, tokenize(buf, f, nelem(f))) != 2){
		close(fd);
		return -1;
	}
	if(dir < 0){
		if(nsaved < Nsaved){
			snprint(saved[nsaved].path, sizeof saved[nsaved].path, "%s", key);
			saved[nsaved].val = strtol(f[0], nil, 0);
			nsaved++;
			if(fprint(fd, "0") < 0)
				fprint(2, "acpikeys: %s: %r\n", path);
		}
	} else {
		for(i = 0; i < nsaved; i++)
			if(strcmp(saved[i].path, key) == 0){
				if(fprint(fd, "%ld", saved[i].val) < 0)
					fprint(2, "acpikeys: %s: %r\n", path);
				break;
			}
	}
	close(fd);
	return 0;
}

static int (*lightfn)(char*, int) = step;

/* each panel's backlight below root (a gpu(4) tree) moved; how many */
static int
backlights(char *root, int dir)
{
	char path[256];
	Dir *g, *o;
	int ng, no, i, j, fd, nb;

	nb = 0;
	snprint(path, sizeof path, "%s/gpu", root);
	if((fd = open(path, OREAD)) < 0)
		return 0;
	ng = dirreadall(fd, &g);
	close(fd);
	for(i = 0; i < ng; i++){
		snprint(path, sizeof path, "%s/gpu/%s/outputs", root, g[i].name);
		if((fd = open(path, OREAD)) < 0)
			continue;
		no = dirreadall(fd, &o);
		close(fd);
		for(j = 0; j < no; j++){
			snprint(path, sizeof path, "%s/gpu/%s/outputs/%s/backlight", root, g[i].name, o[j].name);
			if(lightfn(path, dir) == 0)
				nb++;
		}
		free(o);
	}
	free(g);
	return nb;
}

/* every GPU driver's panels, its tree mounted in turn on /mnt (this namespace's own) */
static int
gpubacklights(int dir)
{
	Dir *d;
	int n, i, fd, nb;
	char path[64];

	if((fd = open("/srv", OREAD)) < 0)
		return 0;
	n = dirreadall(fd, &d);
	close(fd);
	nb = 0;
	for(i = 0; i < n; i++){
		if(strncmp(d[i].name, "gpu.", 4) != 0)
			continue;
		snprint(path, sizeof path, "/srv/%s", d[i].name);
		if((fd = open(path, ORDWR)) < 0)
			continue;
		if(mount(fd, -1, "/mnt", MREPL, "") < 0){
			close(fd);
			continue;
		}
		snprint(cursrv, sizeof cursrv, "%s", d[i].name);
		nb += backlights("/mnt", dir);
		cursrv[0] = 0;
		unmount(nil, "/mnt");
	}
	free(d);
	return nb;
}

/* the GPU drivers' panels, else the firmware's (aux/acpi's _BCM) */
static void
brightness(int dir)
{
	if(gpubacklights(dir) == 0)
		step("/dev/backlight", dir);
}

/*
 * pstate(8)'s policy: closed, the old one saved and low; open, the old
 * one again.  Its /srv/pstate, mounted for the while: it may have come
 * after this process's namespace was made.
 */
static void
lidclock(int closed)
{
	char buf[256], *p, *e;
	int fd, n;

	if((fd = open("/srv/pstate", ORDWR)) < 0)
		return;
	if(mount(fd, -1, "/mnt", MREPL, "") < 0){
		close(fd);
		return;
	}
	if((fd = open("/mnt/pstate", ORDWR)) >= 0){
		if(closed){
			pspolicy[0] = 0;
			n = pread(fd, buf, sizeof buf - 1, 0);
			if(n > 0){
				buf[n] = 0;
				if((p = strstr(buf, "policy ")) != nil){
					p += 7;
					if((e = strchr(p, '\n')) != nil)
						*e = 0;
					snprint(pspolicy, sizeof pspolicy, "%s", p);
				}
			}
			fprint(fd, "low");
		} else if(pspolicy[0] != 0)
			fprint(fd, "%s", pspolicy);
		close(fd);
	}
	unmount(nil, "/mnt");
}

/* the lid: the panels' backlights and the clock, as above */
static void
lid(int closed)
{
	if(closed == lidclosed)
		return;
	lidclosed = closed;
	lightfn = lidlight;
	if(closed){
		nsaved = 0;
		if(gpubacklights(-1) == 0)
			lidlight("/dev/backlight", -1);
	} else {
		if(gpubacklights(1) == 0)
			lidlight("/dev/backlight", 1);
		nsaved = 0;
	}
	lightfn = step;
	lidclock(closed);
}

static void
usage(void)
{
	fprint(2, "usage: aux/acpikeys [-f events]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[1024], *l[64], *f[3];
	int fd, n, nl, i;

	ARGBEGIN{
	case 'f':
		evfile = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	rfork(RFNAMEG);
	if((fd = open(evfile, OREAD)) < 0)
		sysfatal("%s: %r", evfile);
	while((n = read(fd, buf, sizeof buf - 1)) > 0){
		buf[n] = 0;
		nl = getfields(buf, l, nelem(l), 1, "\n");
		for(i = 0; i < nl; i++){
			if(tokenize(l[i], f, nelem(f)) != 2)
				continue;
			if(strcmp(f[0], "lid") == 0){
				lid(strcmp(f[1], "closed") == 0);
				continue;
			}
			if(strcmp(f[0], "key") != 0 || lidclosed)
				continue;
			if(strcmp(f[1], "brightnessup") == 0)
				brightness(1);
			else if(strcmp(f[1], "brightnessdown") == 0)
				brightness(-1);
			else if(strcmp(f[1], "brightnesszero") == 0)
				brightness(0);
		}
	}
	sysfatal("%s: %r", evfile);
}
