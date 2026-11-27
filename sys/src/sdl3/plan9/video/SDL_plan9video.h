/*
  SPDX-License-Identifier: Zlib

  SDL's plan9 video driver: a window's data (SDL_Window's internal), for
  the driver's files.
*/
#ifndef SDL_plan9video_h_
#define SDL_plan9video_h_

#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"
#include "p9draw.h"

enum { PLAN9_NKEYS = 32 };

struct SDL_WindowData {
    SDL_Window *window;
    P9dwin *d;                   /* the process's window (p9draw_process) or one of wsys' clone file */
    struct Plan9Reader *rd;      /* another window's readers (NULL: the process's window, its are the process's) */
    int wx, wy, ww, wh;          /* its contents on the screen */
    int buttons;
    Uint32 keys[PLAN9_NKEYS];    /* down, as its last k/K said */
    int nkeys;
    void *fb;
    void *eglsurf;               /* its EGL surface (SDL_plan9opengl.c) */
};

/* the name of window's surface for Mesa's plan9 WSI: NULL, the process's window; else its directory */
const char *PLAN9_SurfaceName(SDL_Window *window);

#endif
