/*
 * SPDX-License-Identifier: Zlib
 *
 * SDL's plan9 video driver: the native Plan 9 side (p9draw.c: libdraw),
 * plain C types so that the driver's files (SDL's headers, APE) call it.
 */
#ifndef P9DRAW_H
#define P9DRAW_H

typedef struct P9dwin P9dwin;	/* a window: the process's, or one of wsys' clone file */

int	p9draw_init(void);	/* the display and the process's window (rio, wsys, the screen): 0, -1 */
void	p9draw_close(void);
P9dwin*	p9draw_process(void);	/* the process's window (/dev) */
/* another window (wsys only): a dialog or popup of parent (nil: the process's), msgs wsys ctl lines */
P9dwin*	p9draw_open(P9dwin *parent, const char *msgs);
void	p9draw_closewin(P9dwin*);
const char*	p9draw_dir(P9dwin*);	/* its files' directory: /dev, or one that stays across wsys restarts */
int	p9draw_isprocess(P9dwin*);
int	p9draw_ctl(P9dwin*, const char *msg);	/* a wsys ctl message (not the process's window): 0, -1 */
int	p9draw_parent(P9dwin*, P9dwin *p);	/* a dialog or popup of p (p nil: a window of its own): 0, -1 */
void	p9draw_wsys(char *buf, int n);	/* $wsys: the wsys the windows are in now */
int	p9draw_wsyslost(void);	/* that wsys is gone (libdraw's wsyslost) */
int	p9draw_attach(P9dwin*);	/* the window again (resized: rio gives it a new name) */
int	p9draw_border(P9dwin*);	/* its border: the window system's, at its scale */
int	p9draw_dpi(void);	/* the window system's dots per inch (wsys' scale ×96), as of the last attach */
int	p9draw_again(void);	/* the window's wsys gone: the window in the next one: 0, -1 (the window went) */
int	p9draw_reconnect(void);	/* draw(3) anew after p9draw_again */
void	p9draw_errstr(char *buf, int n);	/* the system's error string of the last failed call */
/* the window's contents on the screen (inside its border) */
void	p9draw_rect(P9dwin*, int *x, int *y, int *w, int *h);
void	p9draw_screen(int *w, int *h);	/* the whole screen */
/* w x h XRGB8888 pixels, stride bytes a row, at the window's top left: 0, -1 */
int	p9draw_put(P9dwin*, const void *pixels, int w, int h, int stride);

#endif
