/*
  SPDX-License-Identifier: Zlib

  SDL's video driver for 9front ("plan9"): the process's window of rio or
  wsys (or the screen without either), as draw(3) gives it.

  - pixels: SDL's window framebuffer, loaded into the window with libdraw
    (p9draw.c, the native side) on each update
  - the mouse: /dev/mouse (m x y buttons msec; r when the window changed:
    it is looked up again, SDL hears of its new size)
  - the keyboard: /dev/kbd (kbd(3) as kbdfs(8) gives it: k and K list the
    keys down after a press or release, so each change is a key event;
    c the character typed, SDL's text input)
  - the title: /dev/label; the clipboard: /dev/snarf; the size: /dev/wctl
  - cursors: /dev/cursor (SDL_plan9mouse.c); warping the pointer: writing
    m x y to /dev/mouse
  - Vulkan: VK_PLAN9_surface (SDL_plan9vulkan.c)

  The first window is the process's (its window of the window system,
  /dev); under wsys(4) more come through its clone files (p9draw_open:
  libdraw's wsysopen): a window of its own, or with a parent a dialog
  (modal: SDL_WINDOW_MODAL) or a popup (SDL_WINDOW_POPUP_MENU takes the
  focus, SDL_WINDOW_TOOLTIP not), each with its files in a directory of its
  own. Each window's mouse and kbd (and another window's event: Delete as
  SDL_EVENT_WINDOW_CLOSE_REQUESTED, wsys' askclose) are read by threads of
  their own; PumpEvents takes what they read. When the wsys is gone the
  process's window is made again in the next one (libdraw's wsysagain), the
  others with it at their directories, and all are drawn again: the
  program goes on.
*/
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PLAN9

#include "video/SDL_sysvideo.h"
#include "events/SDL_events_c.h"
#include "events/SDL_keyboard_c.h"
#include "events/SDL_mouse_c.h"
#include "events/SDL_windowevents_c.h"
#include "SDL_plan9opengl.h"
#include "SDL_plan9mouse.h"
#include "SDL_plan9video.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "p9draw.h"
#include "SDL_plan9vulkan.h"

#define PLAN9_DRIVER_NAME "plan9"

/* Plan 9's keyboard.h, its runes for keys that are not characters */
enum {
    KF = 0xF000, Spec = 0xF800,
    Khome = KF | 0x0D, Kup = KF | 0x0E, Kdown = Spec | 0x00, Kpgup = KF | 0x0F, Kprint = KF | 0x10,
    Kleft = KF | 0x11, Kright = KF | 0x12, Kpgdown = KF | 0x13, Kins = KF | 0x14, Kalt = KF | 0x15,
    Kshift = KF | 0x16, Kctl = KF | 0x17, Kend = KF | 0x18, Kscroll = KF | 0x19,
    Kbs = 0x08, Kdel = 0x7f, Kesc = 0x1b,
    Kbreak = Spec | 0x61, Kcaps = Spec | 0x64, Knum = Spec | 0x65, Kaltgr = Spec | 0x67, Kmod4 = Spec | 0x68,
};

/* what the readers read, for PumpEvents */
typedef struct Raw {
    char kind;       /* m mouse, r resize, k K c keyboard, A a new wsys, x closed (Delete), q the end */
    SDL_WindowID win;  /* 0: the process's window */
    int x, y, b;
    char text[64];   /* k K c: UTF-8 */
} Raw;

enum { Nraw = 512, Nkeys = PLAN9_NKEYS };

static struct {
    SDL_Window *window;          /* the process's */
    SDL_Mutex *lock;
    Raw raw[Nraw];
    int head, tail;
    int mousefd, kbdfd;          /* the process's window's */
    bool readers;                /* its readers started (once a process) */
    SDL_DisplayID display;
    int dpi;                     /* the window system's (wsys' scale ×96): the content scale */
} p9 = { .mousefd = -1, .kbdfd = -1 };

/*
 * another window's readers: its mouse's, its kbd's, its event file's, each
 * a thread holding a reference; the window's end stops them (its window
 * deleted in wsys, their reads fail)
 */
typedef struct Plan9Reader {
    SDL_WindowID id;
    char dir[64];
    char srv[256];               /* the wsys its window is in ($wsys) */
    int mousefd;
    SDL_AtomicInt stop;
    SDL_AtomicInt refs;
} Reader;

static void push(const Raw *r)
{
    SDL_LockMutex(p9.lock);
    if ((p9.head + 1) % Nraw != p9.tail) {
        p9.raw[p9.head] = *r;
        p9.head = (p9.head + 1) % Nraw;
    }
    SDL_UnlockMutex(p9.lock);
}

static Reader *hold(Reader *rd)
{
    SDL_AddAtomicInt(&rd->refs, 1);
    return rd;
}

static void letgo(Reader *rd)
{
    if (SDL_AddAtomicInt(&rd->refs, -1) == 1) {
        if (rd->mousefd >= 0) {
            close(rd->mousefd);
        }
        SDL_free(rd);
    }
}

static int SDLCALL kbdreader(void *arg);

/* SDL_PLAN9_DEBUG=1: what the readers do when the window's files fail */
static int debug(void)
{
    static int d = -1;

    if (d < 0) {
        const char *e = SDL_getenv("SDL_PLAN9_DEBUG");
        d = e != NULL && SDL_atoi(e) != 0;
    }
    return d;
}

/* a keyboard's reader: fd its kbd file, rd the window's readers (NULL: the process's window) */
typedef struct Kbdarg {
    int fd;
    Reader *rd;
} Kbdarg;

static void startkbdon(int fd, Reader *rd)
{
    Kbdarg *a;

    if (fd < 0 || !(a = SDL_malloc(sizeof *a))) {
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    a->fd = fd;
    a->rd = rd ? hold(rd) : NULL;
    SDL_DetachThread(SDL_CreateThread(kbdreader, "plan9 kbd", a));
}

/* the process's window's keyboard reader, of its /dev/kbd now (a descriptor of its own) */
static void startkbd(void)
{
    if (p9.kbdfd >= 0) {
        startkbdon(p9.kbdfd, NULL);
    }
}

/* m x y buttons msec into r; false for what is not m or r */
static bool mousemsg(const char *buf, Raw *r)
{
    char *p;

    r->kind = buf[0];
    if (r->kind != 'm' && r->kind != 'r') {
        return false;
    }
    p = (char *)buf + 1;
    r->x = (int)strtol(p, &p, 10);
    r->y = (int)strtol(p, &p, 10);
    r->b = (int)strtol(p, &p, 10);
    return true;
}

/*
 * the process's window: m x y buttons msec, 49 bytes a message; r: the
 * window changed. When the window's files fail and its wsys is gone, the
 * window in the next one (p9draw_again, the others with it): A tells the
 * main thread to reconnect; q the end.
 */
static int SDLCALL mousereader(void *arg)
{
    char buf[64];
    Raw r;
    int n, fd;

    (void)arg;
    for (;;) {
        n = read(p9.mousefd, buf, 49);
        if (n <= 0) {
            int again, rfd = p9.mousefd;
            char why[128];

            p9draw_errstr(why, sizeof why);
            if (debug()) {
                fprintf(stderr, "sdl plan9: pid %d: mouse read %d on %d (%s): the window again\n", (int)getpid(), n, rfd, why);
            }
            again = p9draw_again();
            fd = again < 0 ? -1 : open("/dev/mouse", O_RDWR);
            if (debug()) {
                fprintf(stderr, "sdl plan9: pid %d: mouse read %d on %d (%s), the window again: %d, /dev/mouse %d\n",
                        (int)getpid(), n, rfd, why, again, fd);
            }
            if (fd < 0) {
                break;
            }
            close(p9.mousefd);
            p9.mousefd = fd;
            /* the old keyboard's reader closes its own when its read fails */
            p9.kbdfd = open("/dev/kbd", O_RDONLY);
            startkbd();
            SDL_zero(r);
            r.kind = 'A';
            push(&r);
            continue;
        }
        buf[n] = 0;
        SDL_zero(r);
        if (mousemsg(buf, &r)) {
            push(&r);
        }
    }
    SDL_zero(r);
    r.kind = 'q';
    push(&r);
    return 0;
}

/*
 * another window gone with its wsys: made again in the next one at its
 * directory (by the process's window's reader, p9draw_again): its mouse
 * there, once $wsys names a wsys that runs; -1 when the window ended
 * meanwhile or no wsys came ($wsyswait seconds, 5 minutes without it)
 */
static int againat(Reader *rd)
{
    char path[96], now[256];
    const char *e;
    int fd, i, secs;

    e = SDL_getenv("wsyswait");
    secs = e ? SDL_atoi(e) : 300;
    SDL_snprintf(path, sizeof path, "%s/mouse", rd->dir);
    for (i = 0; secs <= 0 || i < secs * 4; i++) {
        if (SDL_GetAtomicInt(&rd->stop)) {
            return -1;
        }
        p9draw_wsys(now, sizeof now);
        if (SDL_strcmp(now, rd->srv) != 0 && !p9draw_wsyslost() && (fd = open(path, O_RDWR)) >= 0) {
            SDL_strlcpy(rd->srv, now, sizeof rd->srv);
            return fd;
        }
        SDL_Delay(250);
    }
    return -1;
}

/*
 * another window's mouse. Its files fail: deleted in its wsys (Delete
 * twice: wsys' askclose let it go), x, a close request; gone with its
 * wsys, the window again in the next one (againat), A
 */
static int SDLCALL xmousereader(void *arg)
{
    Reader *rd = arg;
    char buf[64], now[256];
    Raw r;
    int n, fd;

    for (;;) {
        n = read(rd->mousefd, buf, 49);
        if (SDL_GetAtomicInt(&rd->stop)) {
            break;
        }
        if (n <= 0) {
            p9draw_wsys(now, sizeof now);
            if (debug()) {
                fprintf(stderr, "sdl plan9: pid %d: %s/mouse read %d: wsys %s (was %s), lost %d\n",
                        (int)getpid(), rd->dir, n, now, rd->srv, p9draw_wsyslost());
            }
            if (!p9draw_wsyslost() && SDL_strcmp(now, rd->srv) == 0) {
                SDL_zero(r);
                r.kind = 'x';
                r.win = rd->id;
                push(&r);
                break;
            }
            fd = againat(rd);
            if (debug()) {
                fprintf(stderr, "sdl plan9: pid %d: %s again: mouse %d\n", (int)getpid(), rd->dir, fd);
            }
            if (fd < 0) {
                break;
            }
            close(rd->mousefd);
            rd->mousefd = fd;
            SDL_zero(r);
            r.kind = 'A';
            r.win = rd->id;
            push(&r);
            continue;
        }
        buf[n] = 0;
        SDL_zero(r);
        if (mousemsg(buf, &r)) {
            r.win = rd->id;
            push(&r);
        }
    }
    letgo(rd);
    return 0;
}

/* another window's event file (wsys(4)): close, the user's Delete (askclose) */
static int SDLCALL eventreader(void *arg)
{
    Reader *rd = arg;
    char buf[512], path[96], *l, *e;
    Raw r;
    int fd, n;

    SDL_snprintf(path, sizeof path, "%s/event", rd->dir);
    if ((fd = open(path, O_RDONLY)) >= 0) {
        while ((n = read(fd, buf, sizeof buf - 1)) > 0 && !SDL_GetAtomicInt(&rd->stop)) {
            buf[n] = 0;
            for (l = buf; *l; l = e) {
                if ((e = SDL_strchr(l, '\n')) != NULL) {
                    *e++ = 0;
                } else {
                    e = l + SDL_strlen(l);
                }
                if (SDL_strcmp(l, "close") == 0) {
                    SDL_zero(r);
                    r.kind = 'x';
                    r.win = rd->id;
                    push(&r);
                }
            }
        }
        close(fd);
    }
    letgo(rd);
    return 0;
}

/* another window's kbd and event readers (again, in a new wsys) */
static void startxreaders(Reader *rd)
{
    char path[96];

    SDL_snprintf(path, sizeof path, "%s/kbd", rd->dir);
    startkbdon(open(path, O_RDONLY), rd);
    SDL_DetachThread(SDL_CreateThread(eventreader, "plan9 event", hold(rd)));
}

/* kbd(3): messages k, K, c and a UTF-8 string each, NUL after each */
static int SDLCALL kbdreader(void *arg)
{
    Kbdarg *a = arg;
    char buf[512];
    Raw r;
    int n, i, len;

    for (;;) {
        n = read(a->fd, buf, sizeof buf - 1);
        if (n <= 0 || (a->rd && SDL_GetAtomicInt(&a->rd->stop))) {
            close(a->fd);  /* its own: no other reader gets its number meanwhile */
            break;
        }
        buf[n] = 0;
        for (i = 0; i < n; i += len + 1) {
            len = (int)strlen(buf + i);
            if (len == 0) {
                continue;
            }
            SDL_zero(r);
            r.kind = buf[i];
            r.win = a->rd ? a->rd->id : 0;
            SDL_strlcpy(r.text, buf + i + 1, sizeof r.text);
            push(&r);
        }
    }
    if (a->rd) {
        letgo(a->rd);
    }
    SDL_free(a);
    return 0;
}

static SDL_Scancode scancode(Uint32 r)
{
    if (r >= 'A' && r <= 'Z') {
        r += 'a' - 'A';
    }
    if (r >= 'a' && r <= 'z') {
        return (SDL_Scancode)(SDL_SCANCODE_A + (r - 'a'));
    }
    if (r >= '1' && r <= '9') {
        return (SDL_Scancode)(SDL_SCANCODE_1 + (r - '1'));
    }
    if (r >= KF + 1 && r <= KF + 12) {
        return (SDL_Scancode)(SDL_SCANCODE_F1 + (r - KF - 1));
    }
    switch (r) {
    case '0': return SDL_SCANCODE_0;
    case '\n': return SDL_SCANCODE_RETURN;
    case '\t': return SDL_SCANCODE_TAB;
    case ' ': return SDL_SCANCODE_SPACE;
    case '-': return SDL_SCANCODE_MINUS;
    case '=': return SDL_SCANCODE_EQUALS;
    case '[': return SDL_SCANCODE_LEFTBRACKET;
    case ']': return SDL_SCANCODE_RIGHTBRACKET;
    case '\\': return SDL_SCANCODE_BACKSLASH;
    case ';': return SDL_SCANCODE_SEMICOLON;
    case '\'': return SDL_SCANCODE_APOSTROPHE;
    case '`': return SDL_SCANCODE_GRAVE;
    case ',': return SDL_SCANCODE_COMMA;
    case '.': return SDL_SCANCODE_PERIOD;
    case '/': return SDL_SCANCODE_SLASH;
    case Kbs: return SDL_SCANCODE_BACKSPACE;
    case Kesc: return SDL_SCANCODE_ESCAPE;
    case Kdel: return SDL_SCANCODE_DELETE;
    case Kup: return SDL_SCANCODE_UP;
    case Kdown: return SDL_SCANCODE_DOWN;
    case Kleft: return SDL_SCANCODE_LEFT;
    case Kright: return SDL_SCANCODE_RIGHT;
    case Khome: return SDL_SCANCODE_HOME;
    case Kend: return SDL_SCANCODE_END;
    case Kpgup: return SDL_SCANCODE_PAGEUP;
    case Kpgdown: return SDL_SCANCODE_PAGEDOWN;
    case Kins: return SDL_SCANCODE_INSERT;
    case Kprint: return SDL_SCANCODE_PRINTSCREEN;
    case Kscroll: return SDL_SCANCODE_SCROLLLOCK;
    case Kbreak: return SDL_SCANCODE_PAUSE;
    case Kcaps: return SDL_SCANCODE_CAPSLOCK;
    case Knum: return SDL_SCANCODE_NUMLOCKCLEAR;
    case Kshift: return SDL_SCANCODE_LSHIFT;
    case Kctl: return SDL_SCANCODE_LCTRL;
    case Kalt: return SDL_SCANCODE_LALT;
    case Kaltgr: return SDL_SCANCODE_RALT;
    case Kmod4: return SDL_SCANCODE_LGUI;
    }
    return SDL_SCANCODE_UNKNOWN;
}

/* the keys a k/K message lists */
static int keyset(const char *s, Uint32 *keys)
{
    int n = 0;

    while (*s && n < Nkeys) {
        keys[n++] = SDL_StepUTF8(&s, NULL);
    }
    return n;
}

static bool inset(Uint32 k, const Uint32 *keys, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (keys[i] == k) {
            return true;
        }
    }
    return false;
}

/* the keys down now in window w: an event for each that went down or up */
static void keys(SDL_WindowData *w, const char *s)
{
    Uint32 now[Nkeys];
    int n, i;

    n = keyset(s, now);
    for (i = 0; i < w->nkeys; i++) {
        if (!inset(w->keys[i], now, n)) {
            SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, (int)w->keys[i], scancode(w->keys[i]), false);
        }
    }
    for (i = 0; i < n; i++) {
        if (!inset(now[i], w->keys, w->nkeys)) {
            SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, (int)now[i], scancode(now[i]), true);
        }
    }
    SDL_memcpy(w->keys, now, sizeof now[0] * n);
    w->nkeys = n;
}

/* where window w is now (after r): its place and size to SDL if they changed */
static void relook(SDL_WindowData *w)
{
    int x, y, dx, dy;

    p9draw_attach(w->d);
    p9draw_rect(w->d, &x, &y, &dx, &dy);
    /* another scale (wsys' dpi): the display's content scale, SDL tells the program */
    if (p9draw_dpi() != p9.dpi) {
        p9.dpi = p9draw_dpi();
        SDL_SetDisplayContentScale(SDL_GetVideoDisplay(p9.display), p9.dpi / 96.0f);
    }
    if (x != w->wx || y != w->wy) {
        w->wx = x;
        w->wy = y;
        if (!SDL_WINDOW_IS_POPUP(w->window)) {
            SDL_SendWindowEvent(w->window, SDL_EVENT_WINDOW_MOVED, x, y);
        }
    }
    if (dx != w->ww || dy != w->wh) {
        w->ww = dx;
        w->wh = dy;
        SDL_SendWindowEvent(w->window, SDL_EVENT_WINDOW_RESIZED, dx, dy);
    }
}

static void PLAN9_PumpEvents(SDL_VideoDevice *_this)
{
    static const Uint8 button[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT };
    SDL_Window *win, *o;
    SDL_WindowData *w;
    Raw r;
    int i, changed;

    for (;;) {
        SDL_LockMutex(p9.lock);
        if (p9.tail == p9.head) {
            SDL_UnlockMutex(p9.lock);
            break;
        }
        r = p9.raw[p9.tail];
        p9.tail = (p9.tail + 1) % Nraw;
        SDL_UnlockMutex(p9.lock);
        win = r.win ? SDL_GetWindowFromID(r.win) : p9.window;
        if (!win || !(w = win->internal)) {
            continue;
        }
        switch (r.kind) {
        case 'A':
            if (r.win == 0) {
                /* the windows in a new wsys: draw(3) anew, each drawn again */
                p9draw_reconnect();
                for (o = _this->windows; o; o = o->next) {
                    if (o->internal) {
                        o->internal->ww = o->internal->wh = -1;
                        relook(o->internal);
                        SDL_SendWindowEvent(o, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
                    }
                }
            } else {
                /* another window there again: its keyboard and events (its image: with the process's A) */
                startxreaders(w->rd);
                w->ww = w->wh = -1;
                relook(w);
                SDL_SendWindowEvent(win, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
            }
            break;
        case 'x':
            SDL_SendWindowEvent(win, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
            break;
        case 'r':
            relook(w);
            SDL_FALLTHROUGH;
        case 'm':
            SDL_SendMouseMotion(0, win, SDL_DEFAULT_MOUSE_ID, false, (float)(r.x - w->wx), (float)(r.y - w->wy));
            changed = r.b ^ w->buttons;
            for (i = 0; i < 3; i++) {
                if (changed & (1 << i)) {
                    SDL_SendMouseButton(0, win, SDL_DEFAULT_MOUSE_ID, button[i], (r.b & (1 << i)) != 0);
                }
            }
            /* buttons 4 and 5: the wheel, one step each press */
            if ((changed & 8) && (r.b & 8)) {
                SDL_SendMouseWheel(0, win, SDL_DEFAULT_MOUSE_ID, 0, 1, SDL_MOUSEWHEEL_NORMAL);
            }
            if ((changed & 16) && (r.b & 16)) {
                SDL_SendMouseWheel(0, win, SDL_DEFAULT_MOUSE_ID, 0, -1, SDL_MOUSEWHEEL_NORMAL);
            }
            w->buttons = r.b;
            break;
        case 'k':
        case 'K':
            /* wsys gives the keyboard to the window with the focus */
            if (SDL_GetKeyboardFocus() != win) {
                SDL_SetKeyboardFocus(win);
            }
            keys(w, r.text);
            break;
        case 'c': {
            const char *s = r.text;
            Uint32 c = SDL_StepUTF8(&s, NULL);

            /* characters, not the keys' private runes or control characters */
            if (c >= 0x20 && c != Kdel && (c < KF || c >= 0xF900)) {
                SDL_SendKeyboardText(r.text);
            }
            break;
        }
        case 'q':
            SDL_SendQuit();
            break;
        }
    }
}

/* a file of window w (wctl, label, ...) */
static int openwin(SDL_WindowData *w, const char *f, int mode)
{
    char path[96];

    SDL_snprintf(path, sizeof path, "%s/%s", p9draw_dir(w->d), f);
    return open(path, mode);
}

/* the process's window's size by its wctl (with the border); none without a window system */
static void wctlsize(SDL_WindowData *w, int dx, int dy)
{
    char msg[64];
    int fd;

    if ((fd = openwin(w, "wctl", O_WRONLY)) < 0) {
        return;
    }
    SDL_snprintf(msg, sizeof msg, "resize -dx %d -dy %d", dx + 2 * p9draw_border(w->d), dy + 2 * p9draw_border(w->d));
    write(fd, msg, SDL_strlen(msg));
    close(fd);
}

/* another window's contents to x y (on the screen), dx by dy: wsys' ctl rect (the frame lies around them) */
static void xplace(SDL_WindowData *w, int x, int y, int dx, int dy)
{
    char msg[96];

    SDL_snprintf(msg, sizeof msg, "rect %d %d %d %d", x, y, x + dx, y + dy);
    p9draw_ctl(w->d, msg);
    relook(w);
}

/*
 * fullscreen: wsys' wctl fullscreen (all of the screen, no border or title
 * bar, above its bars) and unfullscreen (the frame back); rio has neither
 * (its window stays as it is). The new size comes as a resize ('r').
 */
static SDL_FullscreenResult PLAN9_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    const char *msg = fullscreen == SDL_FULLSCREEN_OP_LEAVE ? "unfullscreen" : "fullscreen";
    SDL_WindowData *w = window->internal;
    int fd, n;

    (void)_this;
    (void)display;
    if (!w || (fd = openwin(w, "wctl", O_WRONLY)) < 0) {
        return SDL_FULLSCREEN_FAILED;
    }
    n = write(fd, msg, SDL_strlen(msg));
    close(fd);
    if (n != (int)SDL_strlen(msg)) {
        return SDL_FULLSCREEN_FAILED;
    }
    relook(w);
    return SDL_FULLSCREEN_SUCCEEDED;
}

static void PLAN9_SetWindowTitle(SDL_VideoDevice *_this, SDL_Window *window)
{
    const char *t = window->title ? window->title : "";
    SDL_WindowData *w = window->internal;
    char msg[256];
    int fd;

    (void)_this;
    if (!w) {
        return;
    }
    if (!p9draw_isprocess(w->d)) {
        /* through ctl: kept for the next wsys */
        SDL_snprintf(msg, sizeof msg, "label %s", t);
        p9draw_ctl(w->d, msg);
        return;
    }
    if ((fd = openwin(w, "label", O_WRONLY | O_TRUNC)) >= 0) {
        write(fd, t, SDL_strlen(t));
        close(fd);
    }
}

const char *PLAN9_SurfaceName(SDL_Window *window)
{
    SDL_WindowData *w = window ? window->internal : NULL;

    return w && !p9draw_isprocess(w->d) ? p9draw_dir(w->d) : NULL;
}

/* the process's window: the first one without a parent */
static bool processwindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_WindowData *w)
{
    if (p9.mousefd < 0 && (p9.mousefd = open("/dev/mouse", O_RDWR)) < 0) {
        return SDL_SetError("plan9: /dev/mouse: %s", strerror(errno));
    }
    if (p9.kbdfd < 0) {
        p9.kbdfd = open("/dev/kbd", O_RDONLY);
    }
    w->d = p9draw_process();
    window->internal = w;
    if (!(window->flags & SDL_WINDOW_FULLSCREEN)) {
        wctlsize(w, window->w, window->h);
    } else {
        PLAN9_SetWindowFullscreen(_this, window, NULL, SDL_FULLSCREEN_OP_ENTER);
    }
    p9.window = window;
    relook(w);
    /*
     * the readers once a process: SDL makes the window anew now and then
     * (destroy, create), and readers of a window before would read on, and
     * each of them make the window again when its wsys is gone
     */
    if (!p9.readers) {
        p9.readers = true;
        SDL_DetachThread(SDL_CreateThread(mousereader, "plan9 mouse", NULL));
        startkbd();
    }
    return true;
}

/*
 * another window (wsys(4) only): one of its own, or a dialog (modal) or
 * popup of its parent, asking before Delete closes it (its event close:
 * SDL_EVENT_WINDOW_CLOSE_REQUESTED)
 */
static bool otherwindow(SDL_Window *window, SDL_WindowData *w)
{
    SDL_WindowData *pw = window->parent ? window->parent->internal : NULL;
    char msgs[256], path[96];
    Reader *rd;
    int x, y, n = 0;

    if (!window->parent) {
        n += SDL_snprintf(msgs + n, sizeof msgs - n, "parent -\n");
    } else if (window->flags & SDL_WINDOW_POPUP_MENU) {
        n += SDL_snprintf(msgs + n, sizeof msgs - n, "popup grab\n");
    } else if (window->flags & SDL_WINDOW_TOOLTIP) {
        n += SDL_snprintf(msgs + n, sizeof msgs - n, "popup\n");
    }
    if (window->parent && (window->flags & SDL_WINDOW_MODAL)) {
        n += SDL_snprintf(msgs + n, sizeof msgs - n, "modal\n");
    }
    if (!SDL_WINDOW_IS_POPUP(window)) {
        n += SDL_snprintf(msgs + n, sizeof msgs - n, "askclose\n");
    }
    /* where: a popup's place is its parent's; a dialog put nowhere, over its parent's middle */
    if (SDL_WINDOW_IS_POPUP(window)) {
        SDL_RelativeToGlobalForWindow(window, window->x, window->y, &x, &y);
    } else if (pw && window->undefined_x && window->undefined_y) {
        x = pw->wx + (pw->ww - window->w) / 2;
        y = pw->wy + (pw->wh - window->h) / 2;
    } else {
        x = window->x;
        y = window->y;
    }
    SDL_snprintf(msgs + n, sizeof msgs - n, "rect %d %d %d %d\n", x, y, x + window->w, y + window->h);
    if (!(w->d = p9draw_open(pw ? pw->d : NULL, msgs))) {
        char why[128];

        p9draw_errstr(why, sizeof why);
        return SDL_SetError("plan9: no window of wsys' clone file (rio has one window a process): %s", why);
    }
    if (!(rd = SDL_calloc(1, sizeof *rd))) {
        p9draw_closewin(w->d);
        return false;
    }
    rd->id = window->id;
    SDL_strlcpy(rd->dir, p9draw_dir(w->d), sizeof rd->dir);
    p9draw_wsys(rd->srv, sizeof rd->srv);
    SDL_SetAtomicInt(&rd->refs, 1);
    SDL_snprintf(path, sizeof path, "%s/mouse", rd->dir);
    if ((rd->mousefd = open(path, O_RDWR)) < 0) {
        SDL_free(rd);
        p9draw_closewin(w->d);
        return SDL_SetError("plan9: %s: %s", path, strerror(errno));
    }
    w->rd = rd;
    window->internal = w;
    w->wx = w->wy = w->ww = w->wh = -1;
    xplace(w, x, y, window->w, window->h);
    SDL_DetachThread(SDL_CreateThread(xmousereader, "plan9 mouse", hold(rd)));
    startxreaders(rd);
    return true;
}

static bool PLAN9_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    SDL_WindowData *w;
    bool ok;

    (void)props;
    if (!(w = SDL_calloc(1, sizeof *w))) {
        return false;
    }
    w->window = window;
    ok = !p9.window && !window->parent ? processwindow(_this, window, w) : otherwindow(window, w);
    if (!ok) {
        window->internal = NULL;
        SDL_free(w);
        return false;
    }
    window->x = w->wx;
    window->y = w->wy;
    window->w = w->ww;
    window->h = w->wh;
    if (window->title) {
        PLAN9_SetWindowTitle(_this, window);
    }
    if (!(window->flags & SDL_WINDOW_TOOLTIP)) {
        SDL_SetMouseFocus(window);
        SDL_SetKeyboardFocus(window);
    }
    return true;
}

static void PLAN9_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *w = window->internal;

    (void)_this;
    if (!w) {
        return;
    }
    if (p9draw_isprocess(w->d)) {
        wctlsize(w, window->pending.w, window->pending.h);
        relook(w);
    } else {
        xplace(w, w->wx, w->wy, window->pending.w, window->pending.h);
    }
}

static bool PLAN9_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *w = window->internal;
    int x, y;

    (void)_this;
    if (!w || p9draw_isprocess(w->d)) {
        return SDL_Unsupported();   /* the process's window: the user's to move */
    }
    if (SDL_WINDOW_IS_POPUP(window)) {
        SDL_RelativeToGlobalForWindow(window, window->pending.x, window->pending.y, &x, &y);
    } else {
        x = window->pending.x;
        y = window->pending.y;
    }
    xplace(w, x, y, w->ww, w->wh);
    return true;
}

/* another window a dialog or popup of parent, or (none) one of its own */
static bool PLAN9_SetWindowParent(SDL_VideoDevice *_this, SDL_Window *window, SDL_Window *parent)
{
    SDL_WindowData *w = window->internal, *pw = parent ? parent->internal : NULL;

    (void)_this;
    if (!w || p9draw_isprocess(w->d)) {
        return SDL_Unsupported();   /* the process's window has none */
    }
    if (parent && !pw) {
        return SDL_SetError("plan9: the parent has no window");
    }
    if (p9draw_parent(w->d, pw ? pw->d : NULL) < 0) {
        return SDL_SetError("plan9: wsys: parent: %s", strerror(errno));
    }
    return true;
}

/* modal: its parent takes no clicks while it is shown (wsys' modal) */
static bool PLAN9_SetWindowModal(SDL_VideoDevice *_this, SDL_Window *window, bool modal)
{
    SDL_WindowData *w = window->internal;

    (void)_this;
    if (!w || p9draw_isprocess(w->d)) {
        return SDL_Unsupported();
    }
    if (p9draw_ctl(w->d, modal ? "modal" : "modal 0") < 0) {
        return SDL_SetError("plan9: wsys: modal: %s", strerror(errno));
    }
    return true;
}

static void PLAN9_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *w = window->internal;

    (void)_this;
    if (p9.window == window) {
        p9.window = NULL;   /* the readers go with the process */
    }
    if (!w) {
        return;
    }
#if defined(SDL_VIDEO_VULKAN) && (defined(SDL_VIDEO_OPENGL) || defined(SDL_VIDEO_OPENGL_ES2))
    PLAN9_GL_DestroySurface(window);
#endif
    if (w->rd) {
        /* its readers stop: the window deleted (its clone file closed), their reads fail */
        SDL_SetAtomicInt(&w->rd->stop, 1);
        p9draw_closewin(w->d);
        letgo(w->rd);
    }
    SDL_free(w->fb);
    SDL_free(w);
    window->internal = NULL;
}

static bool PLAN9_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_WindowData *w = window->internal;
    int dx, dy;

    (void)_this;
    SDL_GetWindowSizeInPixels(window, &dx, &dy);
    SDL_free(w->fb);
    if (!(w->fb = SDL_calloc(1, (size_t)dx * dy * 4))) {
        return false;
    }
    *format = SDL_PIXELFORMAT_XRGB8888;
    *pixels = w->fb;
    *pitch = dx * 4;
    return true;
}

static bool PLAN9_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_WindowData *w = window->internal;
    int dx, dy;

    (void)_this;
    (void)rects;
    (void)numrects;
    SDL_GetWindowSizeInPixels(window, &dx, &dy);
    if (p9draw_put(w->d, w->fb, dx, dy, dx * 4) < 0) {
        return SDL_SetError("plan9: draw: the window's pixels");
    }
    return true;
}

static void PLAN9_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *w = window->internal;

    (void)_this;
    if (w) {
        SDL_free(w->fb);
        w->fb = NULL;
    }
}

static bool PLAN9_SetClipboardText(SDL_VideoDevice *_this, const char *text)
{
    int fd;

    (void)_this;
    if ((fd = open("/dev/snarf", O_WRONLY | O_TRUNC)) < 0) {
        return SDL_SetError("plan9: /dev/snarf: %s", strerror(errno));
    }
    write(fd, text, SDL_strlen(text));
    close(fd);
    return true;
}

static char *PLAN9_GetClipboardText(SDL_VideoDevice *_this)
{
    char *s = NULL, *t;
    size_t n = 0;
    ssize_t m;
    int fd;

    (void)_this;
    if ((fd = open("/dev/snarf", O_RDONLY)) < 0) {
        return SDL_strdup("");
    }
    for (;;) {
        if (!(t = SDL_realloc(s, n + 8192 + 1))) {
            break;
        }
        s = t;
        if ((m = read(fd, s + n, 8192)) <= 0) {
            break;
        }
        n += (size_t)m;
    }
    close(fd);
    if (!s) {
        return SDL_strdup("");
    }
    s[n] = 0;
    return s;
}

static bool PLAN9_HasClipboardText(SDL_VideoDevice *_this)
{
    char *s = PLAN9_GetClipboardText(_this);
    bool has = s && *s;

    SDL_free(s);
    return has;
}

/* the pointer to x y in the window: m x y on the screen to its mouse file */
static bool PLAN9_WarpMouse(SDL_Window *window, float x, float y)
{
    SDL_WindowData *w = window ? window->internal : NULL;
    char msg[64];
    int n, fd;

    if (!w) {
        return SDL_SetError("plan9: no window");
    }
    fd = w->rd ? w->rd->mousefd : p9.mousefd;
    if (fd < 0) {
        return SDL_SetError("plan9: no mouse file");
    }
    n = SDL_snprintf(msg, sizeof msg, "m%d %d", w->wx + (int)x, w->wy + (int)y);
    if (write(fd, msg, n) != n) {
        return SDL_SetError("plan9: mouse: %s", strerror(errno));
    }
    return true;
}

static bool PLAN9_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    int w, h;

    (void)_this;
    if (!(p9.lock = SDL_CreateMutex())) {
        return false;
    }
    if (p9draw_init() < 0) {
        return SDL_SetError("plan9: no draw(3) display");
    }
    p9draw_screen(&w, &h);
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = w;
    mode.h = h;
    if ((p9.display = SDL_AddBasicVideoDisplay(&mode)) == 0) {
        return false;
    }
    /* the window system's scale (wsys' dpi) as the display's content scale */
    p9.dpi = p9draw_dpi();
    SDL_SetDisplayContentScale(SDL_GetVideoDisplay(p9.display), p9.dpi / 96.0f);
    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, "plan9 kbd");
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, "plan9 mouse");
    PLAN9_InitMouse();
    SDL_GetMouse()->WarpMouse = PLAN9_WarpMouse;
    return true;
}

static void PLAN9_VideoQuit(SDL_VideoDevice *_this)
{
    (void)_this;
    PLAN9_QuitMouse();
    p9draw_close();
}

static void PLAN9_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PLAN9_CreateDevice(void)
{
    SDL_VideoDevice *device;

    /* draw(3) there: a 9front terminal */
    if (access("/dev/draw/new", F_OK) < 0) {
        return NULL;
    }
    if (!(device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice)))) {
        return NULL;
    }
    device->VideoInit = PLAN9_VideoInit;
    device->VideoQuit = PLAN9_VideoQuit;
    device->PumpEvents = PLAN9_PumpEvents;
    device->CreateSDLWindow = PLAN9_CreateWindow;
    device->SetWindowTitle = PLAN9_SetWindowTitle;
    device->SetWindowSize = PLAN9_SetWindowSize;
    device->SetWindowPosition = PLAN9_SetWindowPosition;
    device->SetWindowParent = PLAN9_SetWindowParent;
    device->SetWindowModal = PLAN9_SetWindowModal;
    device->SetWindowFullscreen = PLAN9_SetWindowFullscreen;
    device->DestroyWindow = PLAN9_DestroyWindow;
    device->CreateWindowFramebuffer = PLAN9_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PLAN9_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PLAN9_DestroyWindowFramebuffer;
    device->SetClipboardText = PLAN9_SetClipboardText;
    device->GetClipboardText = PLAN9_GetClipboardText;
    device->HasClipboardText = PLAN9_HasClipboardText;
#ifdef SDL_VIDEO_VULKAN
#if defined(SDL_VIDEO_OPENGL) || defined(SDL_VIDEO_OPENGL_ES2)
    device->GL_LoadLibrary = PLAN9_GL_LoadLibrary;
    device->GL_GetProcAddress = PLAN9_GL_GetProcAddress;
    device->GL_UnloadLibrary = PLAN9_GL_UnloadLibrary;
    device->GL_CreateContext = PLAN9_GL_CreateContext;
    device->GL_MakeCurrent = PLAN9_GL_MakeCurrent;
    device->GL_SetSwapInterval = PLAN9_GL_SetSwapInterval;
    device->GL_GetSwapInterval = PLAN9_GL_GetSwapInterval;
    device->GL_SwapWindow = PLAN9_GL_SwapWindow;
    device->GL_DestroyContext = PLAN9_GL_DestroyContext;
#endif
    device->Vulkan_LoadLibrary = PLAN9_Vulkan_LoadLibrary;
    device->Vulkan_UnloadLibrary = PLAN9_Vulkan_UnloadLibrary;
    device->Vulkan_GetInstanceExtensions = PLAN9_Vulkan_GetInstanceExtensions;
    device->Vulkan_CreateSurface = PLAN9_Vulkan_CreateSurface;
    device->Vulkan_DestroySurface = PLAN9_Vulkan_DestroySurface;
#endif
    device->device_caps = VIDEO_DEVICE_CAPS_HAS_POPUP_WINDOW_SUPPORT;
    device->free = PLAN9_DeleteDevice;
    return device;
}

/* SDL's slot for a driver outside its tree (SDL_VIDEO_DRIVER_PRIVATE): no change to SDL_video.c */
VideoBootStrap PRIVATE_bootstrap = {
    PLAN9_DRIVER_NAME, "SDL video driver for 9front (rio, wsys: draw(3))",
    PLAN9_CreateDevice,
    NULL, /* no message box of its own */
    false
};

#endif /* SDL_VIDEO_DRIVER_PLAN9 */
