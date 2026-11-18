/*
 * GLFW 3.5 for 9front - www.glfw.org
 * SPDX-License-Identifier: Zlib
 *
 * The plan9 platform's libdraw side (APE's: _PLAN9_SOURCE; the rest of
 * the platform has GLFW's headers): a connection of its own to draw(3)
 * and the windows on it, each by name (dir/winname, as a rio client
 * finds its window; the process's: /dev, the screen without rio; more
 * under wsys(4): libdraw's wsysopen, at a directory that stays across
 * wsys restarts), where they are.  Pixels are not drawn here: GLFW's
 * windows are for OpenGL and Vulkan, whose plan9 WSI draws.
 */
#define _PLAN9_SOURCE
#include <draw.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plan9_draw.h"

struct _GLFWdrawwinPlan9 {
	Wsyswin	*ws;		/* nil: the process's window (/dev) */
	char	dir[64];
	Image	*win;		/* nil: the screen (no window system) */
	char	winname[128];
	int	border;		/* a rio window's border is not ours */
	_GLFWdrawwinPlan9	*next;
};

static Display *disp;
static _GLFWdrawwinPlan9 *wins;
static _GLFWdrawwinPlan9 proc = { nil, "/dev" };

static void
drawerr(Display *d, char *msg)
{
	(void)d;
	fprintf(stderr, "glfw plan9: draw: %s\n", msg);
}

int
_glfwDrawInitPlan9(void)
{
	if(disp == nil && (disp = initdisplay(nil, nil, drawerr)) == nil)
		return -1;
	return _glfwDrawAttachPlan9(&proc);
}

_GLFWdrawwinPlan9*
_glfwDrawProcessPlan9(void)
{
	return &proc;
}

static void
release(_GLFWdrawwinPlan9 *w)
{
	if(w->win != nil)
		freeimage(w->win);
	w->win = nil;
	w->winname[0] = 0;
}

void
_glfwDrawClosePlan9(void)
{
	_GLFWdrawwinPlan9 *w;

	release(&proc);
	for(w = wins; w != nil; w = w->next)
		release(w);
	if(disp != nil)
		closedisplay(disp);
	disp = nil;
}

_GLFWdrawwinPlan9*
_glfwDrawOpenPlan9(const char *msgs)
{
	_GLFWdrawwinPlan9 *w;
	Wsyswin *ws;

	if(disp == nil){
		werrstr("no draw(3) display");
		return nil;
	}
	if((ws = wsysopen(nil, (char*)msgs)) == nil)
		return nil;
	if((w = calloc(1, sizeof *w)) == nil){
		wsysclose(ws);
		return nil;
	}
	w->ws = ws;
	snprintf(w->dir, sizeof w->dir, "%s", ws->dir);
	w->next = wins;
	wins = w;
	if(_glfwDrawAttachPlan9(w) < 0){
		_glfwDrawCloseWinPlan9(w);
		return nil;
	}
	return w;
}

void
_glfwDrawCloseWinPlan9(_GLFWdrawwinPlan9 *w)
{
	_GLFWdrawwinPlan9 **l;

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
_glfwDrawDirPlan9(_GLFWdrawwinPlan9 *w)
{
	return w->dir;
}

int
_glfwDrawIsProcessPlan9(_GLFWdrawwinPlan9 *w)
{
	return w == &proc;
}

int
_glfwDrawCtlPlan9(_GLFWdrawwinPlan9 *w, const char *msg)
{
	if(w->ws == nil)
		return -1;
	return wsysctl(w->ws, (char*)msg);
}

void
_glfwDrawWsysPlan9(char *buf, int n)
{
	int fd, m;

	buf[0] = 0;
	if((fd = open("/env/wsys", O_RDONLY)) < 0)
		return;
	m = read(fd, buf, n-1);
	close(fd);
	buf[m > 0 ? m : 0] = 0;
}

int
_glfwDrawWsysLostPlan9(void)
{
	return wsyslost();
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
_glfwDrawAttachPlan9(_GLFWdrawwinPlan9 *w)
{
	char path[96], name[128], dpi[16];

	if(disp == nil)
		return -1;
	w->border = 0;
	snprintf(path, sizeof path, "%s/winname", w->dir);
	if(readfile(path, name, sizeof name) < 0){
		release(w);
		if(w == &proc)
			return 0;	/* no window system: the screen */
		werrstr("%s: no window's name", path);
		return -1;
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

int
_glfwDrawDpiPlan9(void)
{
	return disp != nil && disp->dpi > 0 ? disp->dpi : DefaultDPI;
}

void
_glfwDrawRectPlan9(_GLFWdrawwinPlan9 *w, int *x, int *y, int *dx, int *dy, int *border)
{
	Rectangle r;

	r = insetrect(w->win != nil ? w->win->r : disp->image->r, w->border);
	*x = r.min.x;
	*y = r.min.y;
	*dx = Dx(r);
	*dy = Dy(r);
	*border = w->border;
}

void
_glfwDrawScreenPlan9(int *w, int *h)
{
	*w = Dx(disp->image->r);
	*h = Dy(disp->image->r);
}

/*
 * the process's window's wsys is gone (killed, crashed): the window again
 * in the next one, where it was (libdraw's wsysagain), and with it the
 * others (wsysopen's, at their directories); 0 then, -1 if it was the
 * window that went.  The mouse's reader asks, the main thread then
 * reconnects.
 */
int
_glfwDrawAgainPlan9(void)
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
_glfwDrawReconnectPlan9(void)
{
	_GLFWdrawwinPlan9 *w;

	_glfwDrawClosePlan9();
	if(_glfwDrawInitPlan9() < 0)
		return -1;
	for(w = wins; w != nil; w = w->next)
		_glfwDrawAttachPlan9(w);
	return 0;
}

extern void rerrstr(char*, unsigned int);	/* lib9's (APE's libc.h for it is not ours here) */

void
_glfwDrawErrstrPlan9(char *buf, int n)
{
	rerrstr(buf, n);
}
