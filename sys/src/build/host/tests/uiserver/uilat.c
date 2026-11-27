/*
 * SPDX-License-Identifier: MIT
 *
 * uilat: ui(4)'s start and its latency from input to pixels, measured.
 * Run in a window system (wsys) with /mnt/wsys and /dev/mousein:
 *	start	uinew (ui as a new window of wsys) until it is ready (ctl
 *		answers), until ui drew its first frame (mtpt/frames), and
 *		until its window as wsys composed it is not all one colour
 *		(the window's image(6), /mnt/wsys/n/window: one read, one frame)
 *	latency	n times (-n, default 20) a click on a button through
 *		/dev/mousein; the program sets a label in answer, as a client
 *		does; until the click came (the event), until ui drew a frame
 *		after it, until the window as composed changed
 * Lines "start ...", "click i: ...", then "latency ..." (median and
 * most, ms), "uilat: done". An alarm ends it after 120 s.
 *	uilat [-n clicks]
 */
#include <u.h>
#include <libc.h>
#include <ui.h>

enum { Maxn = 200 };

static Ui *u;
static char win[64];		/* /mnt/wsys/n: ui's window */
/* the clicks on b as the reader (a proc sharing memory) got them, the last one's time */
static volatile long nclick;
static volatile vlong tclick;

/* ui's events: each click on b counted, with its time */
static void
reader(void)
{
	Uievent e;

	while(uievent(u, &e) >= 0)
		if(strcmp(e.kind, "click") == 0 && strcmp(e.id, "b") == 0){
			tclick = nsec();
			nclick++;
		}
	exits(nil);
}

static double
ms(vlong t)
{
	return t / 1e6;
}

/* a number from a file of ui's or wsys' (its first field); -1 */
static vlong
num(char *path)
{
	char buf[64];
	int fd, n;

	if((fd = open(path, OREAD)) < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	return strtoll(buf, nil, 0);
}

/* the frames ui drew */
static vlong
frames(void)
{
	char path[128];

	snprint(path, sizeof path, "%s/frames", u->mtpt);
	return num(path);
}

/* the window as composed, in buf (one read: one frame); its length, or -1 */
static long
snapshot(uchar *buf, long max)
{
	char path[80];
	long n, m;
	int fd;

	snprint(path, sizeof path, "%s/window", win);
	if((fd = open(path, OREAD)) < 0)
		return -1;
	for(n = 0; n < max; n += m)
		if((m = read(fd, buf + n, max - n)) <= 0)
			break;
	close(fd);
	return n;
}

/* whether the pixels after the image(6) header are not all the same */
static int
drawn(uchar *b, long n)
{
	long i;

	for(i = 60 + 4; i + 4 <= n; i += 4)
		if(memcmp(b + i, b + 60, 4) != 0)
			return 1;
	return 0;
}

/* wsys' window whose label is the title: its directory */
static int
findwin(char *title)
{
	char path[64], buf[128];
	Dir *d;
	int fd, n, i, k;

	if((fd = open("/mnt/wsys", OREAD)) < 0)
		return -1;
	n = dirreadall(fd, &d);
	close(fd);
	for(i = 0; i < n; i++){
		if(d[i].name[0] < '0' || d[i].name[0] > '9')
			continue;
		snprint(path, sizeof path, "/mnt/wsys/%s/label", d[i].name);
		if((fd = open(path, OREAD)) < 0)
			continue;
		k = read(fd, buf, sizeof buf - 1);
		close(fd);
		if(k > 0){
			buf[k] = 0;
			if(strncmp(buf, title, strlen(title)) == 0){
				snprint(win, sizeof win, "/mnt/wsys/%s", d[i].name);
				free(d);
				return 0;
			}
		}
	}
	free(d);
	return -1;
}

/* the middle of node id on the screen: ui's ctl gives where the window's contents are */
static int
where(char *id, int *x, int *y)
{
	char path[128], buf[256], *f[8];
	int fd, n, ox, oy;

	snprint(path, sizeof path, "%s/ctl", u->mtpt);
	if((fd = open(path, OREAD)) < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) < 2)
		return -1;
	ox = atoi(f[0]);
	oy = atoi(f[1]);
	snprint(path, sizeof path, "%s/nodes/%s/rect", u->mtpt, id);
	if((fd = open(path, OREAD)) < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) < 4)
		return -1;
	*x = ox + (atoi(f[0]) + atoi(f[2])) / 2;
	*y = oy + (atoi(f[1]) + atoi(f[3])) / 2;
	return 0;
}

static void
mouse(int fd, int x, int y, int b)
{
	fprint(fd, "A %d %d %d\n", x, y, b);
}

static int
cmp(void *a, void *b)
{
	vlong x = *(vlong*)a, y = *(vlong*)b;

	return x < y ? -1 : x > y;
}

/* "median most" of n times, ms */
static void
report(char *what, vlong *t, int n)
{
	qsort(t, n, sizeof t[0], cmp);
	print("latency %s: median %.1f ms, most %.1f ms (%d clicks)\n", what, ms(t[n/2]), ms(t[n-1]), n);
}

void
main(int argc, char **argv)
{
	static vlong tev[Maxn], tfr[Maxn], tpx[Maxn];
	vlong t0, t1, t2, t3, f0, now;
	long c0;
	uchar *before, *after;
	long nb, na, max;
	int n, i, k, x, y, mfd, label;

	n = 20;
	ARGBEGIN{
	case 'n':
		n = atoi(EARGF(sysfatal("usage: uilat [-n clicks]")));
		break;
	default:
		sysfatal("usage: uilat [-n clicks]");
	}ARGEND
	if(n < 1 || n > Maxn)
		sysfatal("clicks: 1 to %d", Maxn);
	alarm(120*1000);
	if((mfd = open("/dev/mousein", OWRITE)) < 0)
		sysfatal("/dev/mousein: %r");
	max = 16<<20;
	before = malloc(max);
	after = malloc(max);
	if(before == nil || after == nil)
		sysfatal("malloc: %r");

	/* the start */
	t0 = nsec();
	if((u = uinew("uilat", "/mnt/uilat")) == nil)
		sysfatal("uinew: %r");
	t1 = nsec();
	if(uictl(u, "new l label\nset l text count 0\nnew b button\nset b text add one\n") < 0)
		sysfatal("uictl: %r");
	while(frames() < 1 && nsec() - t0 < 30*1000000000LL)
		sleep(1);
	t2 = nsec();
	if(findwin("uilat") < 0)
		sysfatal("no window of wsys labelled uilat");
	while(((nb = snapshot(before, max)) < 0 || !drawn(before, nb)) && nsec() - t0 < 30*1000000000LL)
		sleep(1);
	t3 = nsec();
	print("start: ready %.1f ms, first frame %.1f ms, its pixels composed %.1f ms\n",
		ms(t1 - t0), ms(t2 - t0), ms(t3 - t0));

	/* the latency: clicks on b, the label set in answer */
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		reader();
	}
	sleep(500);
	if(where("b", &x, &y) < 0)
		sysfatal("where is b: %r");
	/* a first click may only make the window current */
	mouse(mfd, x, y, 0);
	mouse(mfd, x, y, 1);
	mouse(mfd, x, y, 0);
	sleep(500);
	label = 0;
	for(i = 0; i < n; i++){
		sleep(100);
		nb = snapshot(before, max);
		f0 = frames();
		c0 = nclick;
		mouse(mfd, x, y, 1);
		t0 = nsec();
		mouse(mfd, x, y, 0);
		/* the click's event (a down and up make it) */
		while(nclick == c0 && nsec() - t0 < 5*1000000000LL)
			sleep(0);
		if(nclick == c0)
			sysfatal("click %d: no event in 5 s", i);
		tev[i] = tclick - t0;
		if(uictl(u, "set l text count %d", ++label) < 0)
			sysfatal("uictl: %r");
		while(frames() <= f0 && nsec() - t0 < 5*1000000000LL)
			;
		tfr[i] = nsec() - t0;
		for(k = 0;; k++){
			na = snapshot(after, max);
			now = nsec();
			if(na != nb || memcmp(before, after, na) != 0 || now - t0 > 5*1000000000LL)
				break;
		}
		tpx[i] = now - t0;
		print("click %d: event %.1f ms, ui's frame %.1f ms, pixels composed %.1f ms (%d reads)\n",
			i, ms(tev[i]), ms(tfr[i]), ms(tpx[i]), k + 1);
	}
	report("click to event", tev, n);
	report("click to ui's frame", tfr, n);
	report("click to pixels composed", tpx, n);
	print("uilat: done\n");
	uiclose(u);
	exits(nil);
}
