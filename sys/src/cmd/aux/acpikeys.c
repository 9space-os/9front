/*
 * aux/acpikeys: the brightness keys aux/acpi reports (its acpievents:
 * key brightnessup, brightnessdown, brightnesszero) on the backlight of
 * every panel a GPU driver serves (gpu(4): /shr/gpu/N, its
 * outputs/conn/backlight), one of sixteen steps at a time on a curve
 * the eye sees as even (a cube, as brightnessctl -e: a linear
 * sixteenth was hardly seen near the top, coarse near the bottom),
 * never dark but by brightnesszero: the cube starts at a thirty-second
 * (max * ((k+a)/(16+a))^3, a such that step 0 is max/32), so that each
 * of the sixteen steps is one the eye sees (a cube from 0, cut off at
 * max/32, made steps 0-5 one: six steps lost at the bottom).  The drivers
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
static int lidclosed;
static char pspolicy[16];	/* pstate's policy before, "" if none */

enum {
	Nstep	= 16,
	Gamma	= 3,	/* brightness = max * ((k+a)/(Nstep+a))^Gamma */
	Low	= 32,	/* step 0 is max/Low */
};

/* the curve's offset a: ((0+a)/(Nstep+a))^Gamma = 1/Low */
static double
offset(void)
{
	double c;

	c = pow(1.0/Low, 1.0/Gamma);
	return Nstep * c / (1 - c);
}

/* the value of step k */
static long
stepval(long max, int k)
{
	double a;
	long v;

	a = offset();
	v = (long)(max * pow((k + a) / (Nstep + a), Gamma) + 0.5);
	return v > 0 ? v : 1;
}

/* the backlight's next value from cur, dir steps on the curve */
static long
next(long cur, long max, int dir)
{
	double a, x;
	long v;
	int k;

	/* the step cur is at (rounded), then dir steps on */
	a = offset();
	x = (Nstep + a) * pow((double)cur/max, 1.0/Gamma) - a;
	k = (int)floor(x + 0.5) + dir;
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
	if(v < stepval(max, 0))
		v = stepval(max, 0);
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
	char buf[64], *f[3];
	int fd, n, i;

	if((fd = open(path, ORDWR)) < 0)
		return -1;
	n = pread(fd, buf, sizeof buf - 1, 0);
	if(n <= 0 || (buf[n] = 0, tokenize(buf, f, nelem(f))) != 2){
		close(fd);
		return -1;
	}
	if(dir < 0){
		if(nsaved < Nsaved){
			snprint(saved[nsaved].path, sizeof saved[nsaved].path, "%s", path);
			saved[nsaved].val = strtol(f[0], nil, 0);
			nsaved++;
			if(fprint(fd, "0") < 0)
				fprint(2, "acpikeys: %s: %r\n", path);
		}
	} else {
		for(i = 0; i < nsaved; i++)
			if(strcmp(saved[i].path, path) == 0){
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

/* every GPU driver's panels: shr(3) has each one's tree in every namespace, /shr/gpu/N */
static int
gpubacklights(int dir)
{
	return backlights("/shr", dir);
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
