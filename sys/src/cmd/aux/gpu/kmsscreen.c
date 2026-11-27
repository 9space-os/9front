/*
 * SPDX-License-Identifier: MIT
 *
 * aux/amdgpu, aux/i915: the screen of draw(3) on the card, the Plan 9
 * side, shared (each driver's screen.c includes this file), as
 * aux/virtiogpu makes it: #G's framebuffer (gpu(3)) is what devdraw
 * draws into (vga(3) type gpu), and what changes there (#G/n/dirty) is
 * copied into the screen's object (kmsout.c; the driver's linux/kms.c
 * makes it: amdgpu's in VRAM, i915's in the GGTT), which the outputs
 * show; through the driver's aperture (kmsout_aperture: amdgpu's BAR 0,
 * an APU's video memory, i915's BAR 2), write-combining.  The screen spans the outputs
 * that are on (kmsout.c: at the start every connected one, side by
 * side); writing an output's mode changes it, and devdraw's screen
 * changes size with it (vgactl size, as aux/vga -m does: rio follows).
 * #G's framebuffer (the kernel's memory) is as big as the layout: at
 * the start the connected monitors side by side in their preferred
 * modes.  A monitor connected later goes on right of the others
 * (hotplugproc); when the layout does not fit, a bigger framebuffer is
 * made (growfb) and the screen goes onto it, the old one freed; one
 * gone gives a smaller one back.  Without the memory for a bigger one
 * the new monitor gets the biggest of its modes that fits.  The mouse
 * cursor is the display's (vga hwgc gpu: the kernel passes it on in
 * #G/n/cursor, cursorproc puts it on the outputs' cursor planes); soft,
 * drawn by devdraw, if the kernel or the display cannot.
 * Where the card has room the screen is two objects (kmsout.c): the
 * changes go into the one not shown, with the changes of the frame
 * before (that one lacks them), then it is flipped to, at most one a
 * vblank (flipproc; dirtyproc only collects what changed meanwhile):
 * the display never shows a copy half done (no tearing).  With one
 * object the changes are copied into the one shown, as they come.
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <kpi/p9.h>
#include "kmsout.h"

static char *gdir, *drv;
static QLock scrlock;		/* the geometry, the copies */
static uchar *fb;		/* #G's framebuffer: w x h x 4, stride w*4 */
static ulong fbsize;
static uchar *nextfb;		/* a new one (growfb, smaller): the screen goes onto it at newscreen */
static ulong nextsize;
enum { Shrinkslack = 8<<20 };	/* a framebuffer this much bigger than the screen is given back */
static uchar *bar;		/* the aperture the processor writes the screen through */
static uchar *vram;		/* the screen's object shown, through it */
static uchar *vback;		/* the one not shown, nil: one object */
static int w, h;
static uint pitch;
/* the copies of devdraw's changes, for status */
static uvlong ncopy, copybytes, copyns, copymaxns;

/*
 * what devdraw changed since the last copy (dirtyproc collects, flipproc
 * copies): rects, all of them in one when there are more; and what the
 * last copy had (the object not shown lacks it)
 */
enum { Nrect = 32 };
typedef struct Rects Rects;
struct Rects {
	int	n;
	int	r[Nrect][4];
};
static QLock dlock;
static Rendez dready;
static Rects dirt;
static Rects lastdirt;		/* scrlock */
static uvlong nflip, nflipfail;

static void	hotplugproc(void*);
static void	cursorproc(void*);

static int
vgactl(char *fmt, ...)
{
	char buf[128];
	va_list arg;
	int fd, r;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	if((fd = open("#v/vgactl", OWRITE)) < 0)
		return -1;
	r = write(fd, buf, strlen(buf));
	close(fd);
	return r < 0 ? -1 : 0;
}

/* the output's biggest mode, and its preferred one (the first line) */
static void
outmodes(char *name, int *bw, int *bh, int *pw, int *ph)
{
	char *m, *s, *l, *e;
	int x, y;

	*bw = *bh = *pw = *ph = 0;
	if((m = kmsout_modes(name)) == nil)
		return;
	for(l = m; l != nil && *l; l = e){
		e = strchr(l, '\n');
		if(e != nil)
			*e++ = 0;
		x = strtol(l, &s, 10);
		if(*s++ != 'x')
			continue;
		y = strtol(s, nil, 10);
		if(*pw == 0){
			*pw = x;
			*ph = y;
		}
		if((vlong)x*y > (vlong)*bw * *bh){
			*bw = x;
			*bh = y;
		}
	}
	kmsout_free(m);
}

/*
 * the framebuffer's sizes to try, biggest first (the first that can be
 * made): the connected outputs side by side in their preferred modes
 * (kmsout_screen's layout), the first one's preferred mode; 0 if none
 * is connected.  A bigger layout later makes a bigger one (growfb)
 */
static int
fbsizes(uvlong *sz)
{
	char *st, *l, *e, *f[4];
	int bw, bh, pw, ph, sw, mh, mw, sh, qw, qh;

	sw = mh = mw = sh = qw = qh = 0;
	sz[3] = 0;
	if((st = kmsout_status()) == nil)
		return 0;
	for(l = st; l != nil && *l; l = e){
		e = strchr(l, '\n');
		if(e != nil)
			*e++ = 0;
		if(tokenize(l, f, nelem(f)) < 3 || strcmp(f[0], "output") != 0 || strcmp(f[2], "connected") != 0)
			continue;
		outmodes(f[1], &bw, &bh, &pw, &ph);
		if(pw == 0)
			continue;
		if(sz[3] == 0)
			sz[3] = (uvlong)pw*ph*4;
		sw += bw;
		mh = bh > mh ? bh : mh;
		sh += bh;
		mw = bw > mw ? bw : mw;
		qw += pw;
		qh = ph > qh ? ph : qh;
	}
	kmsout_free(st);
	if(sz[3] == 0)
		return 0;
	sz[1] = (uvlong)sw*mh*4;
	if((uvlong)mw*sh*4 > sz[1])
		sz[1] = (uvlong)mw*sh*4;
	sz[2] = (uvlong)qw*qh*4;
	USED(bw, bh, sw, sh, mw, mh);
	sz[0] = sz[2];
	sz[1] = sz[3];
	return 2;
}

/* #G's framebuffer, size bytes at least: its segment, attached */
static uchar*
fbattach(ulong size)
{
	char path[64], buf[1024], *l, *e, *f[4];
	uchar *p;
	int fd, n;

	snprint(path, sizeof path, "%s/ctl", gdir);
	if((fd = open(path, ORDWR)) < 0)
		return nil;
	if(fprint(fd, "fb %lud", size) < 0){
		close(fd);
		return nil;
	}
	n = pread(fd, buf, sizeof buf - 1, 0);
	close(fd);
	if(n <= 0)
		return nil;
	buf[n] = 0;
	for(l = buf; l != nil && *l; l = e){
		e = strchr(l, '\n');
		if(e != nil)
			*e++ = 0;
		/* "fb pa size seg" */
		if(tokenize(l, f, nelem(f)) == 4 && strcmp(f[0], "fb") == 0){
			p = segattach(0, f[3], nil, strtoul(f[2], nil, 0));
			if(p == (void*)-1)
				return nil;
			nextsize = strtoul(f[2], nil, 0);
			return p;
		}
	}
	werrstr("no framebuffer in %s", path);
	return nil;
}

/* what devdraw changed, into the screen's object dst; scrlock held */
static void
copyto(uchar *dst, int x0, int y0, int x1, int y1)
{
	int y, n;

	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > w) x1 = w;
	if(y1 > h) y1 = h;
	if(x0 >= x1 || y0 >= y1)
		return;
	n = (x1 - x0) * 4;
	for(y = y0; y < y1; y++)
		memmove(dst + (uvlong)y*pitch + x0*4, fb + (uvlong)y*w*4 + x0*4, n);
}

/* the same into the object shown */
static void
copyrect(int x0, int y0, int x1, int y1)
{
	copyto(vram, x0, y0, x1, y1);
}

/* a rect to rs: all of them in one (the box around) when it is full */
static void
addrect(Rects *rs, int x0, int y0, int x1, int y1)
{
	int i, *r;

	if(x0 >= x1 || y0 >= y1)
		return;
	for(i = 0; i < rs->n; i++){
		r = rs->r[i];
		if(r[0] <= x0 && r[1] <= y0 && r[2] >= x1 && r[3] >= y1)
			return;	/* in one already */
	}
	if(rs->n == Nrect){
		r = rs->r[0];
		for(i = 1; i < rs->n; i++){
			if(rs->r[i][0] < r[0]) r[0] = rs->r[i][0];
			if(rs->r[i][1] < r[1]) r[1] = rs->r[i][1];
			if(rs->r[i][2] > r[2]) r[2] = rs->r[i][2];
			if(rs->r[i][3] > r[3]) r[3] = rs->r[i][3];
		}
		rs->n = 1;
		if(x0 < r[0]) r[0] = x0;
		if(y0 < r[1]) r[1] = y0;
		if(x1 > r[2]) r[2] = x1;
		if(y1 > r[3]) r[3] = y1;
		return;
	}
	r = rs->r[rs->n++];
	r[0] = x0;
	r[1] = y0;
	r[2] = x1;
	r[3] = y1;
}

/* the rects of rs into dst, counted for the status; scrlock held */
static void
copyrects(uchar *dst, Rects *rs)
{
	vlong t0, t;
	int i, *r;

	for(i = 0; i < rs->n; i++){
		r = rs->r[i];
		t0 = nsec();
		copyto(dst, r[0], r[1], r[2], r[3]);
		t = nsec() - t0;
		ncopy++;
		copybytes += (uvlong)(r[2] - r[0]) * (r[3] - r[1]) * 4;
		copyns += t;
		if(t > copymaxns)
			copymaxns = t;
	}
}

/* the screen's objects as kmsout has them now (after a layout: both new); scrlock held */
static void
screenobjs(void)
{
	unsigned long long front, back;

	if(kmsout_screen_objs(&front, &back) == 2){
		vram = bar + front;
		vback = bar + back;
	}else{
		vram = bar + front;
		vback = nil;
	}
}

static void
dirtyproc(void *a)
{
	char buf[64], *f[4];
	int fd, n;

	fd = (int)(uintptr)a;
	for(;;){
		n = read(fd, buf, sizeof buf - 1);
		if(n <= 0){
			fprint(2, "%s: screen: %s/dirty: %r\n", drv, gdir);
			return;
		}
		buf[n] = 0;
		if(tokenize(buf, f, nelem(f)) == 4){
			qlock(&dlock);
			addrect(&dirt, atoi(f[0]), atoi(f[1]), atoi(f[2]), atoi(f[3]));
			rwakeup(&dready);
			qunlock(&dlock);
		}
	}
}

/*
 * what changed onto the display: into the object not shown, with what
 * the last copy had, then the flip to it (its wait: one a vblank; what
 * changes meanwhile is collected for the next); with one object into the
 * one shown
 */
static void
flipproc(void*)
{
	Rects d, both;
	int i;

	for(;;){
		qlock(&dlock);
		while(dirt.n == 0)
			rsleep(&dready);
		d = dirt;
		dirt.n = 0;
		qunlock(&dlock);

		qlock(&scrlock);
		screenobjs();	/* which one is shown, as kmsout knows it (a late flip) */
		if(vback == nil){
			copyrects(vram, &d);
			lastdirt.n = 0;
			qunlock(&scrlock);
			continue;
		}
		both = lastdirt;
		for(i = 0; i < d.n; i++)
			addrect(&both, d.r[i][0], d.r[i][1], d.r[i][2], d.r[i][3]);
		copyrects(vback, &both);
		if(kmsout_screen_flip() == 0){
			lastdirt = d;
			nflip++;
		}else{
			/*
			 * not on the display (refused, or its event not yet come):
			 * the one shown gets them as well, both whole again
			 */
			copyrects(vram, &d);
			lastdirt.n = 0;
			nflipfail++;
		}
		screenobjs();
		qunlock(&scrlock);
	}
}

/*
 * a line for gpu(4)'s status: the copies of devdraw's changes to the
 * card (how many, MB, the time per MB and the longest); malloc'd
 */
char*
screenstatus(void)
{
	uvlong n, b, ns, mx;

	if(fb == nil)
		return nil;
	uvlong nf, nff;
	int two;

	qlock(&scrlock);
	n = ncopy;
	b = copybytes;
	ns = copyns;
	mx = copymaxns;
	nf = nflip;
	nff = nflipfail;
	two = vback != nil;
	qunlock(&scrlock);
	return smprint("screen %dx%d %s, flips %llud (not %llud), copies %llud, %llud MB, %llud us/MB, longest %llud us\n",
		w, h, two ? "double" : "single", nf, nff, n, b>>20, b >= 1<<20 ? ns/1000/(b>>20) : 0, mx/1000);
}

/* devdraw's screen w x h */
static int
setsize(void)
{
	if(vgactl("size %dx%dx32 x8r8g8b8", w, h) < 0 || vgactl("drawinit") < 0){
		werrstr("vgactl: %r");
		return -1;
	}
	return 0;
}

/*
 * the screen on the card, written through the driver's aperture; 0, or
 * -1 (and the screen stays where it was)
 */
int
screenstart(char *dir, char *name)
{
	unsigned long long off;
	uvlong sz[4];
	char path[64];
	uint uw, uh;
	int r, i, n, dfd;

	gdir = dir;
	drv = name;
	if((n = fbsizes(sz)) == 0){
		werrstr("no connected output");
		return -1;
	}
	for(i = 0; i < n && fb == nil; i++)
		if(sz[i] > 0 && sz[i] < 1ULL<<31)
			fb = fbattach(sz[i]);
	if(fb == nil)
		return -1;
	fbsize = nextsize;
	if((bar = p9_apwc(kmsout_aperture())) == nil){
		werrstr("aperture %#llux write-combining: %r", kmsout_aperture());
		return -1;
	}
	if((r = kmsout_screen(fbsize, &uw, &uh, &pitch, &off)) < 0){
		bar = nil;
		werrstr("screen object: error %d", -r);
		return -1;
	}
	w = uw;
	h = uh;
	screenobjs();
	/*
	 * #G keeps the changes for an open dirty file only: open before the
	 * switch, the clients' drawing after it (wsys and rio draw all again
	 * at the mouse's r) is kept for dirtyproc; the copy of all of it
	 * after the switch has what was drawn before
	 */
	snprint(path, sizeof path, "%s/dirty", gdir);
	if((dfd = open(path, OREAD)) < 0)
		return -1;
	if(vgactl("type gpu") < 0 || setsize() < 0){
		close(dfd);
		return -1;
	}
	copyrect(0, 0, w, h);
	if(vback != nil)
		addrect(&lastdirt, 0, 0, w, h);	/* the other one lacks all of it */
	dready.l = &dlock;
	proccreate(flipproc, nil, 16384);
	proccreate(dirtyproc, (void*)(uintptr)dfd, 16384);
	if(vgactl("hwgc gpu") < 0)
		vgactl("hwgc soft");
	else
		proccreate(cursorproc, nil, 1024*1024);	/* amdgpu: DC's cursor update */
	proccreate(hotplugproc, nil, 1024*1024);	/* amdgpu: DC's modeset */
	fprint(2, "%s: screen %dx%d, %s (framebuffer %lud bytes)\n", drv, w, h,
		vback != nil ? "two objects (flips)" : "one object", fbsize);
	return 0;
}

/*
 * an output's mode written (outputs/conn/mode): the layout, then
 * devdraw's screen of its size; nil or the error.  On a proc with a big
 * stack (amdgpu: DC's modeset).
 */
/* the screen's object as kmsout made it, devdraw's screen of its size; scrlock held */
static void
newscreen(uint uw, uint uh, uint up, unsigned long long off)
{
	char path[64];
	uchar *old;
	int fd;

	USED(off);
	screenobjs();
	pitch = up;
	old = nil;
	if(nextfb != nil){
		/* the kernel's screen onto the new framebuffer (gpufb), at its size below */
		if(vgactl("type gpu") < 0)
			fprint(2, "%s: vgactl type gpu: %r\n", drv);
		old = fb;
		fb = nextfb;
		fbsize = nextsize;
		nextfb = nil;
		w = h = 0;
	}
	if(uw != w || uh != h){
		w = uw;
		h = uh;
		if(setsize() < 0)
			fprint(2, "%s: screen %dx%d: %r\n", drv, w, h);
		else
			fprint(2, "%s: screen %dx%d\n", drv, w, h);
	}
	copyrect(0, 0, w, h);
	lastdirt.n = 0;
	if(vback != nil)
		addrect(&lastdirt, 0, 0, w, h);	/* the other one (new, or not shown) lacks all of it */
	if(old != nil){
		/* the old one: ours let go, the kernel frees it when no one has it */
		segdetach(old);
		snprint(path, sizeof path, "%s/ctl", gdir);
		if((fd = open(path, OWRITE)) >= 0){
			fprint(fd, "fbfree");
			close(fd);
		}
		fprint(2, "%s: framebuffer %lud bytes\n", drv, fbsize);
	}
}

/*
 * kmsout's Kmsgrow: a framebuffer of need bytes for a layout that does
 * not fit (newscreen puts the screen on it); scrlock held
 */
static int
growfb(unsigned long long need, unsigned long long *max)
{
	uchar *p;

	if(need >= 1ULL<<31 || (p = fbattach(need)) == nil){
		fprint(2, "%s: no framebuffer of %llud bytes: %r\n", drv, need);
		return -1;
	}
	if(nextfb != nil)
		segdetach(nextfb);	/* one made before and not used */
	nextfb = p;
	*max = nextsize;
	return 0;
}

/* a framebuffer much bigger than the screen w x h: a smaller one; scrlock held */
static void
shrinkfb(uint uw, uint uh, uint up, unsigned long long off)
{
	uvlong want;
	uchar *p;

	want = (uvlong)uw*uh*4;
	if(fbsize <= want + Shrinkslack || (p = fbattach(want)) == nil)
		return;
	nextfb = p;
	newscreen(uw, uh, up, off);
}

char*
screenset(char *name, char *spec)
{
	unsigned long long off;
	const char *e;
	uint uw, uh, up;

	if(bar == nil)
		return "no screen of the driver's (-S)";
	qlock(&scrlock);
	e = kmsout_set(name, spec, fbsize, growfb, &uw, &uh, &up, &off);
	newscreen(uw, uh, up, off);
	if(e == nil)
		shrinkfb(uw, uh, up, off);
	qunlock(&scrlock);
	return (char*)e;
}

/*
 * a monitor connected or gone (DRM's hotplug event): the layout after
 * it (a client's picture on the display goes: kmsout_relayout)
 */
static void
hotplugproc(void*)
{
	unsigned long long off;
	const char *e;
	uint seen, uw, uh, up;
	int changed;

	seen = 0;
	for(;;){
		if(kmsout_hotplug_wait(&seen) < 0)
			continue;
		qlock(&scrlock);
		e = kmsout_relayout(fbsize, growfb, &changed, &uw, &uh, &up, &off);
		if(changed){
			newscreen(uw, uh, up, off);
			shrinkfb(uw, uh, up, off);
		}else if(nextfb != nil){
			segdetach(nextfb);	/* made for a layout that did not come */
			nextfb = nil;
		}
		qunlock(&scrlock);
		if(e != nil)
			fprint(2, "%s: hotplug: %s\n", drv, e);
	}
}

/*
 * the requests of the outputs' files: a mode set, the backlight set or
 * read (spec nil), on a proc of their own with a big stack (amdgpu: DC's
 * modeset needs more than lib9p's procs have)
 */
typedef struct Oreq Oreq;
struct Oreq {
	char	*out, *spec;
	int	bl;
	char	*e, *s;
	Channel	*done;
};
static Channel *outc;
static QLock outlock;

static void
outproc(void*)
{
	Oreq *q;

	for(;;){
		q = recvp(outc);
		if(!q->bl)
			q->e = screenset(q->out, q->spec);
		else if(q->spec != nil)
			q->e = (char*)kmsout_setbacklight(q->out, q->spec);
		else
			q->s = kmsout_backlight(q->out);
		sendul(q->done, 1);
	}
}

/* nil or the error; a backlight read: *s, kmsout_free it */
char*
outrequest(char *out, char *spec, int bl, char **s)
{
	Oreq q;

	qlock(&outlock);
	if(outc == nil){
		outc = chancreate(sizeof(void*), 0);
		proccreate(outproc, nil, 1024*1024);
	}
	qunlock(&outlock);
	memset(&q, 0, sizeof q);
	q.out = out;
	q.spec = spec;
	q.bl = bl;
	q.done = chancreate(sizeof(ulong), 0);
	sendp(outc, &q);
	recvul(q.done);
	chanfree(q.done);
	if(s != nil)
		*s = q.s;
	return q.e;
}

static int
hexbytes(char *s, uchar *b, int n)
{
	int i, x;
	char t[3];

	for(i = 0; i < n; i++){
		if(s[0] == 0 || s[1] == 0)
			return -1;
		t[0] = s[0];
		t[1] = s[1];
		t[2] = 0;
		x = strtol(t, nil, 16);
		b[i] = x;
		s += 2;
	}
	return 0;
}

/*
 * the cursor from the kernel (#G/n/cursor: x y on gen ox oy clrset size) onto
 * the display; if the display cannot show it, devdraw draws it again
 */
static void
cursorproc(void*)
{
	char path[64], buf[256], *f[8];
	uchar clr[32], set[32];
	int fd, n, gen, lastgen, r;

	snprint(path, sizeof path, "%s/cursor", gdir);
	if((fd = open(path, OREAD)) < 0){
		fprint(2, "%s: cursor: %s: %r\n", drv, path);
		vgactl("hwgc soft");
		return;
	}
	lastgen = -1;
	for(;;){
		n = read(fd, buf, sizeof buf - 1);
		if(n <= 0){
			fprint(2, "%s: cursor: %s: %r\n", drv, path);
			break;
		}
		buf[n] = 0;
		n = tokenize(buf, f, nelem(f));
		if(n != 7 && n != 8)
			continue;
		gen = atoi(f[3]);
		if(gen != lastgen){
			if(hexbytes(f[6], clr, 32) < 0 || hexbytes(f[6]+64, set, 32) < 0)
				continue;
			if((r = kmsout_cursor_image(clr, set, atoi(f[4]), atoi(f[5]), n == 8 ? atoi(f[7]) : 16)) < 0){
				fprint(2, "%s: cursor: the display's: error %d: soft\n", drv, -r);
				break;
			}
			lastgen = gen;
		}
		kmsout_cursor_move(atoi(f[0]), atoi(f[1]), atoi(f[2]));
	}
	close(fd);
	kmsout_cursor_move(0, 0, 0);
	vgactl("hwgc soft");
}
