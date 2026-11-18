/*
 * SPDX-License-Identifier: MIT
 *
 * The draw(3) side of Mesa's plan9 WSI (include/plan9win.h): its own
 * connection to /dev/draw (initdisplay), the window by name (namedimage,
 * as a rio client finds its window), the pixels loaded into the window
 * (loadimage, flushimage), or through an image of their size and draw
 * where the window's pixels are not 32-bit x8r8g8b8.
 *
 * A surface's name (include/plan9win.h): NULL, the process's window (its
 * name space's /dev); a path, a window's directory of wsys(4) (/dev/wsys/n:
 * a program's other windows, its dialogs and popups); else a window
 * image's name in draw(3).
 */
#include <u.h>
#include <libc.h>
#include <draw.h>

#include "plan9win.h"

static Lock lk;
static Display *disp;
static Image *buf;

/* the windows we hold, by the surface's name: their images by name */
enum { Nwin = 16 };
typedef struct Win Win;
struct Win {
	char	key[128];	/* the surface's name ("" for NULL) */
	char	name[128];	/* its image's name in draw(3) */
	Image	*img;
	int	border;		/* a rio window's border is not ours */
	ulong	used;
};
static Win wins[Nwin];
static ulong usegen;
static Image *win;		/* the one window() gave last */
static char curname[128];	/* its image's name */
static int winborder;

static void
drawerr(Display *d, char *msg)
{
	(void)d;
	fprint(2, "plan9win: draw: %s\n", msg);
}

/* the window directory of a surface's name: /dev for NULL, the path for one */
static void
windir(const char *name, char *dir, int n)
{
	snprint(dir, n, "%s", name == nil ? "/dev" : name);
}

/*
 * a small file of a window's directory read whole (winname, ctl): kept
 * open and read again from the start (each frame asks: a pread, not an
 * open, read and close); -1 if there is none
 */
enum { Nkept = 8 };
static struct {
	char	path[160];
	int	fd;
} kept[Nkept];
static int nextkept;

static int
readkept(const char *path, char *buf, int n, int omode)
{
	int i, fd, m;

	for(i = 0; i < Nkept; i++)
		if(kept[i].path[0] && strcmp(kept[i].path, path) == 0){
			if((m = pread(kept[i].fd, buf, n, 0)) > 0)
				return m;
			close(kept[i].fd);	/* the window gone, or another wsys: open again */
			kept[i].path[0] = 0;
			break;
		}
	if((fd = open(path, omode|OCEXEC)) < 0)
		return -1;
	if((m = pread(fd, buf, n, 0)) <= 0){
		close(fd);
		return m;
	}
	i = nextkept++ % Nkept;
	if(kept[i].path[0])
		close(kept[i].fd);
	snprint(kept[i].path, sizeof kept[i].path, "%s", path);
	kept[i].fd = fd;
	return m;
}

/*
 * a window of wsys: its border, as wide as wsys' scale makes it (not
 * rio's Borderwidth); its ctl (wsys(4)) says where the content is on the
 * screen, image i is the window with the border.  -1 if it is not one
 * of wsys'
 */
static int
wsysborder(const char *name, Image *i)
{
	char b[256], path[160], *f[12];
	int n, x0;

	if(name != nil && name[0] != '/'){
		/* an image's name: the process's window's (its ctl is /dev/ctl) or not known */
		if((n = readkept("/dev/winname", b, sizeof b - 1, OREAD)) <= 0)
			return -1;
		b[n] = 0;
		if(strcmp(b, name) != 0)
			return -1;
		name = nil;
	}
	windir(name, path, sizeof path);
	strncat(path, "/ctl", sizeof path - strlen(path) - 1);
	if((n = readkept(path, b, sizeof b - 1, OREAD)) <= 0)
		return -1;
	b[n] = 0;
	if(tokenize(b, f, nelem(f)) != 10)
		return -1;
	x0 = atoi(f[1]);
	n = x0 - i->r.min.x;
	return n >= 0 && n <= 64 ? n : -1;
}

/* the window by name, again if its image's name changed (rio renames on resize) */
static Image*
window(const char *name)
{
	char wname[128], path[160];
	const char *key;
	Win *w, *lru;
	int n, i, border;

	if(disp == nil){
		disp = initdisplay(nil, nil, drawerr);
		if(disp == nil)
			return nil;
	}
	key = name == nil ? "" : name;
	border = 0;
	if(name == nil || name[0] == '/'){
		/* a window's directory: its winname (the process's: /dev/winname) */
		windir(name, path, sizeof path);
		strncat(path, "/winname", sizeof path - strlen(path) - 1);
		n = readkept(path, wname, sizeof wname - 1, OREAD);
		if(n < 0){
			win = nil;
			curname[0] = 0;
			if(name != nil)
				return nil;	/* another window, gone */
			return disp->image;	/* no rio: the screen */
		}
		if(n == 0)
			return name == nil ? disp->image : nil;
		wname[n] = 0;
		/* rio's windows have a border; devdraw's screen (no rio) is noborder.screen.N */
		if(strncmp(wname, "noborder.", 9) != 0)
			border = Borderwidth;
	}else
		snprint(wname, sizeof wname, "%s", name);
	lru = &wins[0];
	for(i = 0; i < Nwin; i++){
		w = &wins[i];
		if(w->img != nil && strcmp(w->key, key) == 0){
			if(strcmp(w->name, wname) == 0)
				goto Found;
			freeimage(w->img);
			w->img = nil;
			lru = w;
			break;
		}
		if(w->img == nil || (lru->img != nil && w->used < lru->used))
			lru = w;
	}
	w = lru;
	if(w->img != nil)
		freeimage(w->img);
	w->img = namedimage(disp, wname);
	if(w->img == nil){
		w->key[0] = 0;
		win = nil;
		curname[0] = 0;
		return nil;
	}
	snprint(w->key, sizeof w->key, "%s", key);
	snprint(w->name, sizeof w->name, "%s", wname);
Found:
	if((i = wsysborder(name, w->img)) >= 0)
		border = i;
	w->border = border;
	w->used = ++usegen;
	win = w->img;
	winborder = w->border;
	snprint(curname, sizeof curname, "%s", w->name);
	return win;
}

static Rectangle
area(Image *i)
{
	return insetrect(i->r, winborder);
}

int
plan9win_size(const char *name, int *w, int *h)
{
	Image *i;
	Rectangle r;

	lock(&lk);
	if((i = window(name)) == nil){
		unlock(&lk);
		return -1;
	}
	r = area(i);
	*w = Dx(r);
	*h = Dy(r);
	unlock(&lk);
	return 0;
}

int
plan9win_isscreen(const char *name)
{
	Image *i;
	int r;

	lock(&lk);
	r = 0;
	if((i = window(name)) != nil)
		r = i == disp->image || strncmp(curname, "noborder.screen.", 16) == 0;
	unlock(&lk);
	return r;
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
plan9win_put(const char *name, const void *pixels, int w, int h, int stride)
{
	Image *i;
	Rectangle r;

	lock(&lk);
	if((i = window(name)) == nil)
		goto Err;
	r = area(i);
	r.max.x = r.min.x + w < r.max.x ? r.min.x + w : r.max.x;
	r.max.y = r.min.y + h < r.max.y ? r.min.y + h : r.max.y;
	if(i->chan == XRGB32 || i->chan == ARGB32){
		/* straight into the window: no copy of our own */
		if(load(i, r, pixels, w, stride) < 0)
			goto Err;
	}else{
		if(buf == nil || Dx(buf->r) != w || Dy(buf->r) != h){
			if(buf != nil)
				freeimage(buf);
			buf = allocimage(disp, Rect(0, 0, w, h), XRGB32, 0, DNofill);
			if(buf == nil)
				goto Err;
		}
		if(load(buf, buf->r, pixels, w, stride) < 0)
			goto Err;
		draw(i, r, buf, nil, ZP);
	}
	if(flushimage(disp, 1) < 0)
		goto Err;
	unlock(&lk);
	return 0;
Err:
	unlock(&lk);
	return -1;
}

/*
 * wsys(4) gives each window a ctl file (/dev/ctl in the window's name
 * space); a read of it is the window's number, rectangle, size, the
 * range for its buffers and its focus, ten numbers.  Only the process's
 * own window: a surface named after another is not ours to fill.
 */
int
plan9win_wsys(const char *name)
{
	char b[256], path[160], *f[12];
	int fd, n;

	if(name != nil && name[0] != '/'){
		/* an image's name: ours only if it is the process's window */
		if((fd = open("/dev/winname", OREAD)) < 0)
			return -1;
		n = read(fd, b, sizeof b - 1);
		close(fd);
		if(n <= 0)
			return -1;
		b[n] = 0;
		if(strcmp(b, name) != 0)
			return -1;
		name = nil;
	}
	windir(name, path, sizeof path);
	strncat(path, "/ctl", sizeof path - strlen(path) - 1);
	if((fd = open(path, ORDWR|OCEXEC)) < 0)
		return -1;
	n = pread(fd, b, sizeof b - 1, 0);
	if(n <= 0){
		close(fd);
		return -1;
	}
	b[n] = 0;
	if(tokenize(b, f, nelem(f)) != 10){
		close(fd);
		return -1;
	}
	return fd;
}

/*
 * whether window name is the process's window in wsys (as plan9win_wsys,
 * without keeping a descriptor): asked each frame (the surface's
 * capabilities), its files kept open
 */
int
plan9win_inwsys(const char *name)
{
	char b[256], path[160], *f[12];
	int n;

	lock(&lk);
	if(name != nil && name[0] != '/'){
		n = readkept("/dev/winname", b, sizeof b - 1, OREAD);
		if(n <= 0){
			unlock(&lk);
			return 0;
		}
		b[n] = 0;
		if(strcmp(b, name) != 0){
			unlock(&lk);
			return 0;
		}
		name = nil;
	}
	windir(name, path, sizeof path);
	strncat(path, "/ctl", sizeof path - strlen(path) - 1);
	n = readkept(path, b, sizeof b - 1, OREAD);
	unlock(&lk);
	if(n <= 0)
		return 0;
	b[n] = 0;
	return tokenize(b, f, nelem(f)) == 10;
}

/* the window's events in wsys (wsys(4)'s event): a descriptor, -1 */
int
plan9win_wsysevents(const char *name)
{
	char path[160];

	windir(name != nil && name[0] != '/' ? nil : name, path, sizeof path);
	strncat(path, "/event", sizeof path - strlen(path) - 1);
	return open(path, OREAD|OCEXEC);
}

/*
 * the descriptors of plan9win_wsys and plan9win_wsysevents are the
 * system's, not APE's (whose read and close refuse them, EBADF): read
 * and close them here
 */
int
plan9win_wsysread(int fd, char *buf, int n)
{
	return read(fd, buf, n);
}

void
plan9win_wsysclose(int fd)
{
	close(fd);
}

int
plan9win_wsysctl(int fd, const char *msg)
{
	int n;

	n = strlen(msg);
	if(write(fd, msg, n) != n)
		return -1;
	return 0;
}

/*
 * the window's wsys gone (killed, crashed): the window again in the next
 * one (libdraw's wsysagain: once, whoever of the program asks first), our
 * draw(3) connection anew at the next use; 0, -1 if wsys is not gone,
 * -2 if no next one came in time ($wsyswait, libdraw's wsysagain)
 */
int
plan9win_again(void)
{
	char rect[64];
	int i;

	if(!wsyslost())
		return -1;
	lock(&lk);
	rect[0] = 0;
	if(win != nil)
		snprint(rect, sizeof rect, "%d %d %d %d", win->r.min.x, win->r.min.y, win->r.max.x, win->r.max.y);
	/* the dead devdraw's: closed (what it says there fails, drawerr) */
	if(buf != nil)
		freeimage(buf);
	for(i = 0; i < Nwin; i++){
		if(wins[i].img != nil)
			freeimage(wins[i].img);
		wins[i].img = nil;
		wins[i].key[0] = 0;
	}
	buf = win = nil;
	curname[0] = 0;
	/* the files kept open are the dead wsys': open again in the next */
	for(i = 0; i < Nkept; i++)
		if(kept[i].path[0]){
			close(kept[i].fd);
			kept[i].path[0] = 0;
		}
	if(disp != nil)
		closedisplay(disp);
	disp = nil;
	unlock(&lk);
	if(wsysagain(rect[0] ? rect : nil) < 0)
		return -2;	/* no next wsys in time ($wsyswait) */
	return 0;
}
