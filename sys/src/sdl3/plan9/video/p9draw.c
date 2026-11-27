/*
 * SPDX-License-Identifier: Zlib
 *
 * SDL's plan9 video driver, its libdraw side (APE's: _PLAN9_SOURCE, the
 * rest of the driver has SDL's headers): a connection of its own to
 * draw(3) (initdisplay) and the windows on it, each by name (dir/winname,
 * as a rio client finds its window; the process's: /dev, the screen
 * without rio; more under wsys(4): libdraw's wsysopen, at a directory
 * that stays across wsys restarts), the pixels loaded into a window
 * (loadimage) or, where its pixels are not x8r8g8b8, into an image of
 * theirs and drawn.  rio and wsys alike.
 */
#define _PLAN9_SOURCE
#include <draw.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "p9draw.h"


struct P9dwin {
	Wsyswin	*ws;		/* nil: the process's window (/dev) */
	char	dir[64];
	Image	*win;		/* nil: the screen (no window system) */
	char	winname[128];
	int	border;		/* a rio window's border is not ours */
	Image	*buf;
	P9dwin	*next;
};

static Display *disp;
static P9dwin *wins;
static P9dwin proc = { nil, "/dev" };

static void
drawerr(Display *d, char *msg)
{
	(void)d;
	fprintf(stderr, "sdl plan9: draw: %s\n", msg);
}

int
p9draw_init(void)
{
	if(disp == nil && (disp = initdisplay(nil, nil, drawerr)) == nil)
		return -1;
	return p9draw_attach(&proc);
}

P9dwin*
p9draw_process(void)
{
	return &proc;
}

static void
release(P9dwin *w)
{
	if(w->buf != nil)
		freeimage(w->buf);
	if(w->win != nil)
		freeimage(w->win);
	w->buf = w->win = nil;
	w->winname[0] = 0;
}

void
p9draw_close(void)
{
	P9dwin *w;

	release(&proc);
	for(w = wins; w != nil; w = w->next)
		release(w);
	if(disp != nil)
		closedisplay(disp);
	disp = nil;
}

/*
 * another window (wsys(4) only): a dialog or popup of parent (nil: of the
 * process's window), or with "parent -" in msgs one of its own; msgs are
 * wsys ctl lines (graphics(2)'s wsysopen)
 */
P9dwin*
p9draw_open(P9dwin *parent, const char *msgs)
{
	P9dwin *w;
	Wsyswin *ws;

	if(disp == nil){
		werrstr("no draw(3) display");
		return nil;
	}
	ws = wsysopen(parent != nil ? parent->ws : nil, (char*)msgs);
	if(ws == nil)
		return nil;
	if((w = calloc(1, sizeof *w)) == nil){
		wsysclose(ws);
		return nil;
	}
	w->ws = ws;
	snprintf(w->dir, sizeof w->dir, "%s", ws->dir);
	w->next = wins;
	wins = w;
	if(p9draw_attach(w) < 0){
		p9draw_closewin(w);
		return nil;
	}
	return w;
}

/* the window gone (the process's stays) */
void
p9draw_closewin(P9dwin *w)
{
	P9dwin **l;

	if(w == nil || w == &proc)
		return;
	for(l = &wins; *l != nil; l = &(*l)->next)
		if(*l == w){
			*l = w->next;
			break;
		}
	if(disp != nil){
		release(w);
		flushimage(disp, 1);	/* its image let go at once: draw(3) keeps a window held */
	}
	wsysclose(w->ws);
	free(w);
}

const char*
p9draw_dir(P9dwin *w)
{
	return w->dir;
}

int
p9draw_isprocess(P9dwin *w)
{
	return w == &proc;
}

/* a wsys ctl message (rect, label, top, ...): the process's window has none (its wctl) */
int
p9draw_ctl(P9dwin *w, const char *msg)
{
	if(w->ws == nil)
		return -1;
	return wsysctl(w->ws, (char*)msg);
}

/* w a dialog or popup of p (the process's window too), or with p nil a window of its own */
int
p9draw_parent(P9dwin *w, P9dwin *p)
{
	if(w->ws == nil)
		return -1;
	return wsysparent(w->ws, p != nil ? p->ws : nil, p == nil);
}

/* the wsys the process's windows are in now ($wsys) */
void
p9draw_wsys(char *buf, int n)
{
	int fd, m;

	buf[0] = 0;
	if((fd = open("/env/wsys", O_RDONLY)) < 0)
		return;
	m = read(fd, buf, n-1);
	close(fd);
	buf[m > 0 ? m : 0] = 0;
}

/* the wsys $wsys names is gone (libdraw's wsyslost) */
int
p9draw_wsyslost(void)
{
	return wsyslost();
}

int
p9draw_border(P9dwin *w)
{
	return w->border;
}

int
p9draw_dpi(void)
{
	return disp != nil && disp->dpi > 0 ? disp->dpi : DefaultDPI;
}

static int
readfile(char *path, char *buf, int n)
{
	int fd, m;

	if((fd = open(path, O_RDONLY)) < 0)
		return -1;
	m = read(fd, buf, n-1);
	close(fd);
	if(m <= 0)
		return -1;
	buf[m] = 0;
	return m;
}

int
p9draw_attach(P9dwin *w)
{
	char path[96], name[128], dpi[16];

	if(disp == nil)
		return -1;
	w->border = 0;
	snprintf(path, sizeof path, "%s/winname", w->dir);
	if(readfile(path, name, sizeof name) < 0){
		if(w != &proc)
			werrstr("%s: no window's name", path);
		release(w);
		/* no window system: the screen; another window: gone */
		return w == &proc ? 0 : -1;
	}
	/*
	 * rio's windows have a border; devdraw's screen (noborder.screen.N) has
	 * none; at the window system's scale (wsys' dpi) scalesize(2)'s
	 */
	disp->dpi = DefaultDPI;
	snprintf(path, sizeof path, "%s/dpi", w->dir);
	if(readfile(path, dpi, sizeof dpi) > 0 && atoi(dpi) > 0)
		disp->dpi = atoi(dpi);
	if(strncmp(name, "noborder.", 9) != 0)
		w->border = scalesize(disp, Borderwidth);
	if(w->ws != nil)
		wsysseen(w->ws);	/* its place, for the next wsys */
	if(w->win != nil && strcmp(w->winname, name) == 0)
		return 0;
	if(w->win != nil)
		freeimage(w->win);
	if((w->win = namedimage(disp, name)) == nil){
		werrstr("namedimage %s: %r", name);
		w->winname[0] = 0;
		return -1;
	}
	snprintf(w->winname, sizeof w->winname, "%s", name);
	return 0;
}

static Image*
target(P9dwin *w)
{
	return w->win != nil ? w->win : disp->image;
}

/* the window's contents on the screen (inside its border) */
void
p9draw_rect(P9dwin *w, int *x, int *y, int *dx, int *dy)
{
	Rectangle r;

	r = insetrect(target(w)->r, w->border);
	*x = r.min.x;
	*y = r.min.y;
	*dx = Dx(r);
	*dy = Dy(r);
}

void
p9draw_screen(int *w, int *h)
{
	*w = Dx(disp->image->r);
	*h = Dy(disp->image->r);
}

/* the pixels at the top left of r: whole if they fit as they are, else by rows */
static int
load(Image *i, Rectangle r, const void *pixels, int w, int stride)
{
	const uchar *p;
	int y;

	p = pixels;
	if(stride == w*4 && Dx(r) == w)
		return loadimage(i, r, (uchar*)p, stride*Dy(r));
	for(y = r.min.y; y < r.max.y; y++, p += stride)
		if(loadimage(i, Rect(r.min.x, y, r.max.x, y+1), (uchar*)p, Dx(r)*4) < 0)
			return -1;
	return 0;
}

int
p9draw_put(P9dwin *pw, const void *pixels, int w, int h, int stride)
{
	Image *i;
	Rectangle r;

	if(disp == nil)
		return -1;
	if(pw->win == nil && pw != &proc)
		return -1;
	i = target(pw);
	r = insetrect(i->r, pw->border);
	if(r.min.x + w < r.max.x)
		r.max.x = r.min.x + w;
	if(r.min.y + h < r.max.y)
		r.max.y = r.min.y + h;
	if(i->chan == XRGB32 || i->chan == ARGB32){
		if(load(i, r, pixels, w, stride) < 0)
			return -1;
	}else{
		if(pw->buf == nil || Dx(pw->buf->r) != w || Dy(pw->buf->r) != h){
			if(pw->buf != nil)
				freeimage(pw->buf);
			if((pw->buf = allocimage(disp, Rect(0, 0, w, h), XRGB32, 0, DNofill)) == nil)
				return -1;
		}
		if(load(pw->buf, pw->buf->r, pixels, w, stride) < 0)
			return -1;
		draw(i, r, pw->buf, nil, ZP);
	}
	return flushimage(disp, 1);
}

/*
 * the process's window's wsys is gone (killed, crashed): the window again
 * in the next one, where it was (libdraw's wsysagain), and with it the
 * others (wsysopen's, at their directories); 0 then, -1 if it was the
 * window that went.  The mouse's reader asks, the main thread then
 * reconnects.
 */
int
p9draw_again(void)
{
	char rect[64];

	if(!wsyslost())
		return -1;
	rect[0] = 0;
	if(proc.win != nil)
		snprintf(rect, sizeof rect, "%d %d %d %d", proc.win->r.min.x, proc.win->r.min.y, proc.win->r.max.x, proc.win->r.max.y);
	return wsysagain(rect[0] ? rect : nil);
}

/* a new connection to draw(3): the old one's devdraw went with its wsys; every window found again */
int
p9draw_reconnect(void)
{
	P9dwin *w;

	/* its descriptors closed (what it says to the dead devdraw fails: drawerr) */
	p9draw_close();
	if(p9draw_init() < 0)
		return -1;
	for(w = wins; w != nil; w = w->next)
		p9draw_attach(w);
	return 0;
}

extern void rerrstr(char*, unsigned int);	/* lib9's (APE's libc.h for it is not ours here) */

/* the system's error string of the last call that failed (APE's errno is only its kind) */
void
p9draw_errstr(char *buf, int n)
{
	rerrstr(buf, n);
}
