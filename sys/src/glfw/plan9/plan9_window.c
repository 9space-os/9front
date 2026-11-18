//========================================================================
// GLFW 3.5 for 9front - www.glfw.org
// SPDX-License-Identifier: Zlib
//------------------------------------------------------------------------
// The plan9 platform's windows: the first is the process's window of wsys
// or rio (the screen without either); under wsys more of their own come
// through its clone file (plan9_draw.c: libdraw's wsysopen), each with its
// files in a directory that stays across wsys restarts; the user's Delete
// asks (wsys' askclose: glfwWindowShouldClose).
// - size, place, hidden: /dev/wctl (resize, move, hide, unhide, current);
//   reading it says when the window changed, is current or hidden
// - the mouse: /dev/mouse (m x y buttons msec; r when the window changed,
//   looked up again); the pointer warped by writing m x y to it
// - the keyboard: /dev/kbd (kbd(3) as kbdfs(8) gives it: k and K list the
//   keys down, so each change is a key event; c the character typed)
// - the title: /dev/label; cursors: /dev/cursor (plan9_cursor.c)
// - OpenGL and GLES: EGL's plan9 platform, the native window "" (this
//   process's window); Vulkan: VK_PLAN9_surface
// Readers of each window's mouse, kbd and wctl (another's event too) are
// threads of their own; the event functions take what they read.
//========================================================================

#include "internal.h"

#if defined(_GLFW_PLAN9)

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void push(const _GLFWeventPlan9* e)
{
    int next;

    pthread_mutex_lock(&_glfw.plan9.lock);
    next = (_glfw.plan9.head + 1) % _GLFW_PLAN9_EVENTS;
    if (next != _glfw.plan9.tail)
    {
        _glfw.plan9.events[_glfw.plan9.head] = *e;
        _glfw.plan9.head = next;
    }
    pthread_cond_signal(&_glfw.plan9.cond);
    pthread_mutex_unlock(&_glfw.plan9.lock);
}

// another window's readers: its mouse's, kbd's, wctl's and event file's,
// each a thread holding a reference; the window's end stops them (deleted
// in wsys, their reads fail)
typedef struct _GLFWreaderPlan9
{
    int             id;
    char            dir[64];
    char            srv[256];   // the wsys it is in ($wsys)
    int             mousefd;
    volatile int    stop;
    int             refs;       // under _glfw.plan9.lock
} _GLFWreaderPlan9;

static _GLFWreaderPlan9* hold(_GLFWreaderPlan9* rd)
{
    pthread_mutex_lock(&_glfw.plan9.lock);
    rd->refs++;
    pthread_mutex_unlock(&_glfw.plan9.lock);
    return rd;
}

static void letgo(_GLFWreaderPlan9* rd)
{
    int last;

    pthread_mutex_lock(&_glfw.plan9.lock);
    last = --rd->refs == 0;
    pthread_mutex_unlock(&_glfw.plan9.lock);
    if (last)
    {
        if (rd->mousefd >= 0)
            close(rd->mousefd);
        free(rd);
    }
}

typedef struct
{
    int                 fd;
    _GLFWreaderPlan9*   rd;     // NULL: the process's window
} readerarg;

static void* kbdreader(void* arg);
static void* wctlreader(void* arg);

static void start(void* (*f)(void*), int fd, _GLFWreaderPlan9* rd)
{
    readerarg* a;
    pthread_t t;

    if (fd < 0)
        return;
    if (!(a = malloc(sizeof(*a))))
    {
        close(fd);
        return;
    }
    a->fd = fd;
    a->rd = rd ? hold(rd) : NULL;
    if (pthread_create(&t, NULL, f, a) == 0)
        pthread_detach(t);
    else
    {
        close(fd);
        if (rd)
            letgo(rd);
        free(a);
    }
}

// the process's window's keyboard and wctl readers, of its files now (their own descriptors)
static void startreaders(void)
{
    start(kbdreader, _glfw.plan9.kbdfd, NULL);
    start(wctlreader, _glfw.plan9.wctlfd, NULL);
}

static GLFWbool mousemsg(const char* buf, _GLFWeventPlan9* e)
{
    char* p;

    e->kind = buf[0];
    if (e->kind != 'm' && e->kind != 'r')
        return GLFW_FALSE;
    p = (char*) buf + 1;
    e->x = (int) strtol(p, &p, 10);
    e->y = (int) strtol(p, &p, 10);
    e->buttons = (int) strtol(p, &p, 10);
    return GLFW_TRUE;
}

// the process's window: m x y buttons msec, 49 bytes a message; r: the
// window changed. When the window's files fail and its wsys is gone, the
// window in the next one (_glfwDrawAgainPlan9, the others with it): A tells
// the main thread to reconnect; q the end.
static void* mousereader(void* arg)
{
    _GLFWeventPlan9 e;
    char buf[64];
    int n, fd;

    (void) arg;
    for (;;)
    {
        n = (int) read(_glfw.plan9.mousefd, buf, 49);
        if (n <= 0)
        {
            if (_glfwDrawAgainPlan9() < 0 || (fd = open("/dev/mouse", O_RDWR)) < 0)
                break;
            close(_glfw.plan9.mousefd);
            _glfw.plan9.mousefd = fd;
            // the old keyboard's and wctl's readers close theirs when their reads fail
            _glfw.plan9.kbdfd = open("/dev/kbd", O_RDONLY);
            _glfw.plan9.wctlfd = open("/dev/wctl", O_RDONLY);
            startreaders();
            memset(&e, 0, sizeof(e));
            e.kind = 'A';
            push(&e);
            continue;
        }
        buf[n] = 0;
        memset(&e, 0, sizeof(e));
        if (mousemsg(buf, &e))
            push(&e);
    }
    memset(&e, 0, sizeof(e));
    e.kind = 'q';       // the window is gone
    push(&e);
    return NULL;
}

// another window gone with its wsys: made again in the next one at its
// directory (by the process's window's reader); its mouse there once $wsys
// names a wsys that runs; -1 when the window ended meanwhile or none came
static int againat(_GLFWreaderPlan9* rd)
{
    char path[96], now[256];
    const char* e = getenv("wsyswait");
    int fd, i, secs = e ? atoi(e) : 300;
    struct timespec q = { 0, 250000000 };

    snprintf(path, sizeof(path), "%s/mouse", rd->dir);
    for (i = 0;  secs <= 0 || i < secs * 4;  i++)
    {
        if (rd->stop)
            return -1;
        _glfwDrawWsysPlan9(now, sizeof(now));
        if (strcmp(now, rd->srv) != 0 && !_glfwDrawWsysLostPlan9() && (fd = open(path, O_RDWR)) >= 0)
        {
            snprintf(rd->srv, sizeof(rd->srv), "%s", now);
            return fd;
        }
        nanosleep(&q, NULL);
    }
    return -1;
}

// another window's mouse. Its files fail: deleted in its wsys (Delete twice:
// wsys' askclose let it go), x; gone with its wsys, the window again in the
// next one (againat), A
static void* xmousereader(void* arg)
{
    _GLFWreaderPlan9* rd = arg;
    _GLFWeventPlan9 e;
    char buf[64], now[256];
    int n, fd;

    for (;;)
    {
        n = (int) read(rd->mousefd, buf, 49);
        if (rd->stop)
            break;
        if (n <= 0)
        {
            _glfwDrawWsysPlan9(now, sizeof(now));
            if (!_glfwDrawWsysLostPlan9() && strcmp(now, rd->srv) == 0)
            {
                memset(&e, 0, sizeof(e));
                e.kind = 'x';
                e.win = rd->id;
                push(&e);
                break;
            }
            if ((fd = againat(rd)) < 0)
                break;
            close(rd->mousefd);
            rd->mousefd = fd;
            memset(&e, 0, sizeof(e));
            e.kind = 'A';
            e.win = rd->id;
            push(&e);
            continue;
        }
        buf[n] = 0;
        memset(&e, 0, sizeof(e));
        if (mousemsg(buf, &e))
        {
            e.win = rd->id;
            push(&e);
        }
    }
    letgo(rd);
    return NULL;
}

// another window's event file (wsys(4)): close, the user's Delete (askclose)
static void* eventreader(void* arg)
{
    readerarg* a = arg;
    _GLFWeventPlan9 e;
    char buf[512], *l, *nl;
    int n;

    while ((n = (int) read(a->fd, buf, sizeof(buf) - 1)) > 0 && !a->rd->stop)
    {
        buf[n] = 0;
        for (l = buf;  *l;  l = nl)
        {
            if ((nl = strchr(l, '\n')) != NULL)
                *nl++ = 0;
            else
                nl = l + strlen(l);
            if (strcmp(l, "close") == 0)
            {
                memset(&e, 0, sizeof(e));
                e.kind = 'x';
                e.win = a->rd->id;
                push(&e);
            }
        }
    }
    close(a->fd);
    letgo(a->rd);
    free(a);
    return NULL;
}

// another window's kbd, wctl and event readers (again, in a new wsys)
static void startxreaders(_GLFWreaderPlan9* rd)
{
    char path[96];

    snprintf(path, sizeof(path), "%s/kbd", rd->dir);
    start(kbdreader, open(path, O_RDONLY), rd);
    snprintf(path, sizeof(path), "%s/wctl", rd->dir);
    start(wctlreader, open(path, O_RDONLY), rd);
    snprintf(path, sizeof(path), "%s/event", rd->dir);
    start(eventreader, open(path, O_RDONLY), rd);
}

// kbd(3): messages k, K, c and a UTF-8 string each, NUL after each
static void* kbdreader(void* arg)
{
    readerarg* a = arg;
    _GLFWeventPlan9 e;
    char buf[512];
    int n, i, len;

    for (;;)
    {
        n = (int) read(a->fd, buf, sizeof(buf) - 1);
        if (n <= 0 || (a->rd && a->rd->stop))
            break;
        buf[n] = 0;
        for (i = 0;  i < n;  i += len + 1)
        {
            len = (int) strlen(buf + i);
            if (len == 0)
                continue;
            memset(&e, 0, sizeof(e));
            e.kind = buf[i];
            e.win = a->rd ? a->rd->id : 0;
            snprintf(e.text, sizeof(e.text), "%s", buf + i + 1);
            push(&e);
        }
    }
    close(a->fd);  // its own: no other reader gets its number meanwhile
    if (a->rd)
        letgo(a->rd);
    free(a);
    return NULL;
}

// wctl: a line when the window changed (place, current, hidden)
static void* wctlreader(void* arg)
{
    readerarg* a = arg;
    _GLFWeventPlan9 e;
    int n;

    for (;;)
    {
        memset(&e, 0, sizeof(e));
        n = (int) read(a->fd, e.text, sizeof(e.text) - 1);
        if (n <= 0 || (a->rd && a->rd->stop))
            break;
        e.kind = 'w';
        e.win = a->rd ? a->rd->id : 0;
        push(&e);
    }
    close(a->fd);
    if (a->rd)
        letgo(a->rd);
    free(a);
    return NULL;
}

// a message to the window's wctl (rio(4): resize, move, hide, unhide, current)
static void wctl(_GLFWwindow* window, const char* fmt, ...)
{
    char msg[128], path[96];
    va_list arg;
    int fd, n;

    snprintf(path, sizeof(path), "%s/wctl", _glfwDrawDirPlan9(window->plan9.d));
    if ((fd = open(path, O_WRONLY)) < 0)
        return;     // no window system: the screen stays as it is
    va_start(arg, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, arg);
    va_end(arg);
    write(fd, msg, n);
    close(fd);
}

static void relook(_GLFWwindow* window);

// the window's contents to x y, w by h: the process's window by its wctl
// (with the border, rio's way), another by wsys' ctl rect (the contents)
static void place(_GLFWwindow* window, int x, int y, int w, int h)
{
    const int b = window->plan9.border;
    char msg[96];

    if (_glfwDrawIsProcessPlan9(window->plan9.d))
        wctl(window, "resize -r %d %d %d %d", x - b, y - b, x + w + b, y + h + b);
    else
    {
        snprintf(msg, sizeof(msg), "rect %d %d %d %d", x, y, x + w, y + h);
        _glfwDrawCtlPlan9(window->plan9.d, msg);
    }
    relook(window);
}

// where the window is now: GLFW hears of a new place or size
static void relook(_GLFWwindow* window)
{
    int x, y, w, h;

    _glfwDrawAttachPlan9(window->plan9.d);
    _glfwDrawRectPlan9(window->plan9.d, &x, &y, &w, &h, &window->plan9.border);
    // another scale (wsys' dpi): the program is told
    if (_glfwDrawDpiPlan9() != _glfw.plan9.dpi)
    {
        _glfw.plan9.dpi = _glfwDrawDpiPlan9();
        _glfwInputWindowContentScale(window, _glfw.plan9.dpi / 96.f, _glfw.plan9.dpi / 96.f);
    }
    if (x != window->plan9.xpos || y != window->plan9.ypos)
    {
        window->plan9.xpos = x;
        window->plan9.ypos = y;
        _glfwInputWindowPos(window, x, y);
    }
    if (w != window->plan9.width || h != window->plan9.height)
    {
        window->plan9.width = w;
        window->plan9.height = h;
        _glfwInputWindowSize(window, w, h);
        _glfwInputFramebufferSize(window, w, h);
    }
}

// the cursor the window shows now: its own, the arrow, none
void _glfwShowCursorPlan9(_GLFWwindow* window)
{
    static const unsigned char none[72];
    const unsigned char* c;

    if (_glfw.plan9.cursorfd < 0 && (_glfw.plan9.cursorfd = open("/dev/cursor", O_WRONLY)) < 0)
        return;
    if (window->cursorMode == GLFW_CURSOR_HIDDEN || window->cursorMode == GLFW_CURSOR_DISABLED)
        c = none;
    else if (window->cursor && !window->cursor->plan9.arrow)
        c = window->cursor->plan9.c;
    else
    {
        write(_glfw.plan9.cursorfd, "", 0);
        return;
    }
    write(_glfw.plan9.cursorfd, c, 72);
}

static void warp(_GLFWwindow* window, double x, double y)
{
    char msg[64];
    int n;

    n = snprintf(msg, sizeof(msg), "m%d %d", window->plan9.xpos + (int) x, window->plan9.ypos + (int) y);
    write(window->plan9.rd ? window->plan9.rd->mousefd : _glfw.plan9.mousefd, msg, n);
    window->plan9.lastx = (int) x;
    window->plan9.lasty = (int) y;
}

static void center(_GLFWwindow* window)
{
    warp(window, window->plan9.width / 2, window->plan9.height / 2);
}

static void mouse(_GLFWwindow* window, const _GLFWeventPlan9* e)
{
    static const int button[3] = { GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_MOUSE_BUTTON_RIGHT };
    const int mods = _glfwModsPlan9();
    const double x = e->x - window->plan9.xpos, y = e->y - window->plan9.ypos;
    const int changed = e->buttons ^ window->plan9.buttons;
    GLFWbool in;
    int i;

    in = x >= 0 && y >= 0 && x < window->plan9.width && y < window->plan9.height;
    if (in != window->plan9.hovered)
    {
        window->plan9.hovered = in;
        _glfwInputCursorEnter(window, in);
    }
    if (window->cursorMode == GLFW_CURSOR_DISABLED)
    {
        // where it would be: moved by as much as it moved from the middle, put back there
        if (x != window->plan9.lastx || y != window->plan9.lasty)
        {
            _glfwInputCursorPos(window,
                                window->virtualCursorPosX + x - window->plan9.lastx,
                                window->virtualCursorPosY + y - window->plan9.lasty);
            center(window);
        }
    }
    else if (x != window->plan9.lastx || y != window->plan9.lasty)
    {
        window->plan9.lastx = x;
        window->plan9.lasty = y;
        _glfwInputCursorPos(window, x, y);
    }
    for (i = 0;  i < 3;  i++)
    {
        if (changed & (1 << i))
        {
            _glfwInputMouseClick(window, button[i],
                                 (e->buttons & (1 << i)) ? GLFW_PRESS : GLFW_RELEASE, mods);
        }
    }
    // buttons 4 and 5: the wheel, one step each press
    if ((changed & 8) && (e->buttons & 8))
        _glfwInputScroll(window, 0, 1);
    if ((changed & 16) && (e->buttons & 16))
        _glfwInputScroll(window, 0, -1);
    window->plan9.buttons = e->buttons;
}

static int utf8(const char** s)
{
    const unsigned char* p = (const unsigned char*) *s;
    uint32_t c;
    int n, i;

    if (p[0] < 0x80)
        n = 0, c = p[0];
    else if ((p[0] & 0xe0) == 0xc0)
        n = 1, c = p[0] & 0x1f;
    else if ((p[0] & 0xf0) == 0xe0)
        n = 2, c = p[0] & 0x0f;
    else
        n = 3, c = p[0] & 0x07;
    for (i = 1;  i <= n;  i++)
    {
        if ((p[i] & 0xc0) != 0x80)
        {
            *s += i;
            return 0xfffd;
        }
        c = c << 6 | (p[i] & 0x3f);
    }
    *s += n + 1;
    return (int) c;
}

static GLFWbool inset(uint32_t k, const uint32_t* keys, int n)
{
    int i;

    for (i = 0;  i < n;  i++)
    {
        if (keys[i] == k)
            return GLFW_TRUE;
    }
    return GLFW_FALSE;
}

// the keys down now: an event for each that went down or up
static void keys(_GLFWwindow* window, const char* s)
{
    uint32_t now[_GLFW_PLAN9_KEYS], r;
    int n = 0, i;

    while (*s && n < _GLFW_PLAN9_KEYS)
        now[n++] = (uint32_t) utf8(&s);
    for (i = 0;  i < _glfw.plan9.nkeys;  i++)
    {
        r = _glfw.plan9.keys[i];
        if (!inset(r, now, n))
        {
            // released: its mods without it
            memmove(_glfw.plan9.keys + i, _glfw.plan9.keys + i + 1, (_glfw.plan9.nkeys - i - 1) * sizeof(r));
            _glfw.plan9.nkeys--;
            i--;
            _glfwInputKey(window, _glfwKeyPlan9(r), (int) r, GLFW_RELEASE, _glfwModsPlan9());
        }
    }
    for (i = 0;  i < n;  i++)
    {
        r = now[i];
        if (!inset(r, _glfw.plan9.keys, _glfw.plan9.nkeys))
        {
            _glfw.plan9.keys[_glfw.plan9.nkeys++] = r;
            _glfwInputKey(window, _glfwKeyPlan9(r), (int) r, GLFW_PRESS, _glfwModsPlan9());
        }
    }
}

// wctl's line: minx miny maxx maxy (with the border), current or not, visible or hidden
static void wctlline(_GLFWwindow* window, const char* s)
{
    const GLFWbool focused = strstr(s, "notcurrent") == NULL && strstr(s, "current") != NULL;
    const GLFWbool hidden = strstr(s, "hidden") != NULL;

    relook(window);
    if (focused != window->plan9.focused)
    {
        window->plan9.focused = focused;
        _glfwInputWindowFocus(window, focused);
    }
    if (hidden != window->plan9.hidden)
    {
        window->plan9.hidden = hidden;
        window->plan9.iconified = hidden;
        _glfwInputWindowIconify(window, hidden);
    }
}

// the window of an event: by its id (it may be gone meanwhile)
static _GLFWwindow* bywin(int id)
{
    _GLFWwindow* w;

    for (w = _glfw.windowListHead;  w;  w = w->next)
    {
        if (w->plan9.d && w->plan9.id == id)
            return w;
    }
    return NULL;
}

static void handle(_GLFWwindow* window, const _GLFWeventPlan9* e)
{
    const char* s;
    int c, mods;

    if (!window || !window->plan9.d)
        return;
    switch (e->kind)
    {
        case 'A':
            if (e->win == 0)
            {
                // the windows in a new wsys: draw(3) anew, each drawn again
                _GLFWwindow* w;

                _glfwDrawReconnectPlan9();
                for (w = _glfw.windowListHead;  w;  w = w->next)
                {
                    relook(w);
                    _glfwInputWindowDamage(w);
                }
            }
            else
            {
                // another window there again: its keyboard, wctl and events
                // (its image: with the process's A)
                startxreaders(window->plan9.rd);
                relook(window);
                _glfwInputWindowDamage(window);
            }
            break;
        case 'x':
            _glfwInputWindowCloseRequest(window);
            break;
        case 'r':
            relook(window);
            // fall through: where the pointer is
        case 'm':
            mouse(window, e);
            break;
        case 'k':
        case 'K':
            keys(window, e->text);
            break;
        case 'c':
            s = e->text;
            c = utf8(&s);
            mods = _glfwModsPlan9();
            // characters, not the keys' private runes or control characters
            if (c >= 0x20 && c != 0x7f && (c < 0xF000 || c >= 0xF900))
                _glfwInputChar(window, (uint32_t) c, mods, !(mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)));
            break;
        case 'w':
            wctlline(window, e->text);
            break;
        case 'q':
            _glfwInputWindowCloseRequest(window);
            break;
    }
}

// the process's window: the first one
static GLFWbool processwindow(_GLFWwindow* window, const _GLFWwndconfig* wndconfig)
{
    int x, y, w, h, b, sw, sh;

    if (_glfw.plan9.mousefd < 0 && (_glfw.plan9.mousefd = open("/dev/mouse", O_RDWR)) < 0)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "plan9: /dev/mouse: %s", strerror(errno));
        return GLFW_FALSE;
    }
    if (_glfw.plan9.kbdfd < 0)
        _glfw.plan9.kbdfd = open("/dev/kbd", O_RDONLY);
    if (_glfw.plan9.wctlfd < 0)
        _glfw.plan9.wctlfd = open("/dev/wctl", O_RDONLY);

    window->plan9.d = _glfwDrawProcessPlan9();
    window->plan9.id = 0;
    _glfwDrawAttachPlan9(window->plan9.d);
    _glfwDrawRectPlan9(window->plan9.d, &x, &y, &w, &h, &b);
    if (window->monitor)
    {
        // the whole screen, the border outside it
        _glfwDrawScreenPlan9(&sw, &sh);
        wctl(window, "resize -r %d %d %d %d", -b, -b, sw + b, sh + b);
    }
    else
        wctl(window, "resize -dx %d -dy %d", wndconfig->width + 2 * b, wndconfig->height + 2 * b);
    _glfwDrawAttachPlan9(window->plan9.d);
    _glfwDrawRectPlan9(window->plan9.d, &window->plan9.xpos, &window->plan9.ypos,
                       &window->plan9.width, &window->plan9.height, &window->plan9.border);
    _glfw.plan9.window = window;
    if (!_glfw.plan9.readers)
    {
        _glfw.plan9.readers = GLFW_TRUE;
        pthread_create(&_glfw.plan9.mousereader, NULL, mousereader, NULL);
        startreaders();
    }
    return GLFW_TRUE;
}

// another window (wsys only): one of its own, asking before Delete closes it
static GLFWbool otherwindow(_GLFWwindow* window, const _GLFWwndconfig* wndconfig)
{
    _GLFWreaderPlan9* rd;
    _GLFWwindow* p = _glfw.plan9.window;
    char msgs[128], path[96], why[128];
    int x, y, w = wndconfig->width, h = wndconfig->height, sw, sh;

    // where: GLFW's place if it has one, else over the process's window, or the screen's middle
    _glfwDrawScreenPlan9(&sw, &sh);
    if (wndconfig->xpos != GLFW_ANY_POSITION && wndconfig->ypos != GLFW_ANY_POSITION)
        x = wndconfig->xpos, y = wndconfig->ypos;
    else if (p)
        x = p->plan9.xpos + (p->plan9.width - w) / 2, y = p->plan9.ypos + (p->plan9.height - h) / 2;
    else
        x = (sw - w) / 2, y = (sh - h) / 2;
    if (window->monitor)
        x = 0, y = 0, w = sw, h = sh;
    snprintf(msgs, sizeof(msgs), "parent -\naskclose\nrect %d %d %d %d\n", x, y, x + w, y + h);
    if (!(window->plan9.d = _glfwDrawOpenPlan9(msgs)))
    {
        _glfwDrawErrstrPlan9(why, sizeof(why));
        _glfwInputError(GLFW_PLATFORM_ERROR, "plan9: no window of wsys' clone file (rio has one window a process): %s", why);
        return GLFW_FALSE;
    }
    if (!(rd = calloc(1, sizeof(*rd))))
    {
        _glfwDrawCloseWinPlan9(window->plan9.d);
        return GLFW_FALSE;
    }
    rd->id = window->plan9.id = ++_glfw.plan9.nextid;
    snprintf(rd->dir, sizeof(rd->dir), "%s", _glfwDrawDirPlan9(window->plan9.d));
    _glfwDrawWsysPlan9(rd->srv, sizeof(rd->srv));
    rd->refs = 1;
    snprintf(path, sizeof(path), "%s/mouse", rd->dir);
    if ((rd->mousefd = open(path, O_RDWR)) < 0)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "plan9: %s: %s", path, strerror(errno));
        free(rd);
        _glfwDrawCloseWinPlan9(window->plan9.d);
        return GLFW_FALSE;
    }
    window->plan9.rd = rd;
    if (window->monitor)
        wctl(window, "fullscreen");
    _glfwDrawAttachPlan9(window->plan9.d);
    _glfwDrawRectPlan9(window->plan9.d, &window->plan9.xpos, &window->plan9.ypos,
                       &window->plan9.width, &window->plan9.height, &window->plan9.border);
    {
        pthread_t t;

        if (pthread_create(&t, NULL, xmousereader, hold(rd)) == 0)
            pthread_detach(t);
        else
            letgo(rd);
    }
    startxreaders(rd);
    return GLFW_TRUE;
}

GLFWbool _glfwCreateWindowPlan9(_GLFWwindow* window,
                                const _GLFWwndconfig* wndconfig,
                                const _GLFWctxconfig* ctxconfig,
                                const _GLFWfbconfig* fbconfig)
{
    if (!(_glfw.plan9.window ? otherwindow(window, wndconfig) : processwindow(window, wndconfig)))
        return GLFW_FALSE;
    window->plan9.visible = GLFW_TRUE;
    window->plan9.lastx = window->plan9.lasty = -1;
    window->plan9.focused = GLFW_TRUE;
    if (window->title)
        _glfwSetWindowTitlePlan9(window, window->title);

    if (ctxconfig->client != GLFW_NO_API)
    {
        if (ctxconfig->source == GLFW_NATIVE_CONTEXT_API ||
            ctxconfig->source == GLFW_EGL_CONTEXT_API)
        {
            if (!_glfwInitEGL())
                return GLFW_FALSE;
            if (!_glfwCreateContextEGL(window, ctxconfig, fbconfig))
                return GLFW_FALSE;
        }
        else if (ctxconfig->source == GLFW_OSMESA_CONTEXT_API)
        {
            if (!_glfwInitOSMesa())
                return GLFW_FALSE;
            if (!_glfwCreateContextOSMesa(window, ctxconfig, fbconfig))
                return GLFW_FALSE;
        }

        if (!_glfwRefreshContextAttribs(window, ctxconfig))
            return GLFW_FALSE;
    }

    if (wndconfig->focused || window->monitor)
        wctl(window, "current");
    return GLFW_TRUE;
}

void _glfwDestroyWindowPlan9(_GLFWwindow* window)
{
    if (window->context.destroy)
        window->context.destroy(window);
    if (_glfw.plan9.window == window)
        _glfw.plan9.window = NULL;     // the readers go with the process
    if (window->plan9.rd)
    {
        // its readers stop: the window deleted (its clone file closed), their reads fail
        window->plan9.rd->stop = 1;
        _glfwDrawCloseWinPlan9(window->plan9.d);
        letgo(window->plan9.rd);
        window->plan9.rd = NULL;
    }
    window->plan9.d = NULL;
}

void _glfwSetWindowTitlePlan9(_GLFWwindow* window, const char* title)
{
    char path[96], msg[256];
    int fd;

    if (!_glfwDrawIsProcessPlan9(window->plan9.d))
    {
        // through ctl: kept for the next wsys
        snprintf(msg, sizeof(msg), "label %s", title);
        _glfwDrawCtlPlan9(window->plan9.d, msg);
        return;
    }
    snprintf(path, sizeof(path), "%s/label", _glfwDrawDirPlan9(window->plan9.d));
    if ((fd = open(path, O_WRONLY | O_TRUNC)) >= 0)
    {
        write(fd, title, strlen(title));
        close(fd);
    }
}

void _glfwSetWindowIconPlan9(_GLFWwindow* window, int count, const GLFWimage* images)
{
    (void) window;
    (void) count;
    (void) images;
    _glfwInputError(GLFW_FEATURE_UNAVAILABLE, "plan9: windows have no icon");
}

void _glfwSetWindowMonitorPlan9(_GLFWwindow* window,
                                _GLFWmonitor* monitor,
                                int xpos, int ypos,
                                int width, int height,
                                int refreshRate)
{
    int sw, sh, b = window->plan9.border;

    (void) refreshRate;
    _glfwInputWindowMonitor(window, monitor);
    if (monitor)
    {
        _glfwDrawScreenPlan9(&sw, &sh);
        wctl(window, "resize -r %d %d %d %d", -b, -b, sw + b, sh + b);
    }
    else
    {
        place(window, xpos, ypos, width, height);
        return;
    }
    relook(window);
}

void _glfwGetWindowPosPlan9(_GLFWwindow* window, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = window->plan9.xpos;
    if (ypos)
        *ypos = window->plan9.ypos;
}

void _glfwSetWindowPosPlan9(_GLFWwindow* window, int xpos, int ypos)
{
    if (_glfwDrawIsProcessPlan9(window->plan9.d))
    {
        wctl(window, "move -minx %d -miny %d", xpos - window->plan9.border, ypos - window->plan9.border);
        relook(window);
    }
    else
        place(window, xpos, ypos, window->plan9.width, window->plan9.height);
}

void _glfwGetWindowSizePlan9(_GLFWwindow* window, int* width, int* height)
{
    if (width)
        *width = window->plan9.width;
    if (height)
        *height = window->plan9.height;
}

void _glfwSetWindowSizePlan9(_GLFWwindow* window, int width, int height)
{
    if (_glfwDrawIsProcessPlan9(window->plan9.d))
    {
        wctl(window, "resize -dx %d -dy %d", width + 2 * window->plan9.border, height + 2 * window->plan9.border);
        relook(window);
    }
    else
        place(window, window->plan9.xpos, window->plan9.ypos, width, height);
}

void _glfwSetWindowSizeLimitsPlan9(_GLFWwindow* window,
                                   int minwidth, int minheight,
                                   int maxwidth, int maxheight)
{
    int w = window->plan9.width, h = window->plan9.height;

    // no limits in the window system: the size now kept inside them
    if (minwidth != GLFW_DONT_CARE && w < minwidth)
        w = minwidth;
    if (minheight != GLFW_DONT_CARE && h < minheight)
        h = minheight;
    if (maxwidth != GLFW_DONT_CARE && w > maxwidth)
        w = maxwidth;
    if (maxheight != GLFW_DONT_CARE && h > maxheight)
        h = maxheight;
    if (w != window->plan9.width || h != window->plan9.height)
        _glfwSetWindowSizePlan9(window, w, h);
}

void _glfwSetWindowAspectRatioPlan9(_GLFWwindow* window, int n, int d)
{
    (void) window;
    (void) n;
    (void) d;
}

void _glfwGetFramebufferSizePlan9(_GLFWwindow* window, int* width, int* height)
{
    _glfwGetWindowSizePlan9(window, width, height);
}

void _glfwGetWindowFrameSizePlan9(_GLFWwindow* window,
                                  int* left, int* top,
                                  int* right, int* bottom)
{
    const int b = window->monitor ? 0 : window->plan9.border;

    if (left)
        *left = b;
    if (top)
        *top = b;
    if (right)
        *right = b;
    if (bottom)
        *bottom = b;
}

// the window system's scale (wsys' dpi/96; 1 under rio)
void _glfwGetWindowContentScalePlan9(_GLFWwindow* window, float* xscale, float* yscale)
{
    (void) window;
    if (xscale)
        *xscale = _glfwDrawDpiPlan9() / 96.f;
    if (yscale)
        *yscale = _glfwDrawDpiPlan9() / 96.f;
}

void _glfwIconifyWindowPlan9(_GLFWwindow* window)
{
    wctl(window, "hide");
}

void _glfwRestoreWindowPlan9(_GLFWwindow* window)
{
    const int* r = window->plan9.restore;

    if (window->plan9.iconified)
        wctl(window, "unhide");
    else if (window->plan9.maximized)
    {
        window->plan9.maximized = GLFW_FALSE;
        wctl(window, "resize -r %d %d %d %d", r[0], r[1], r[2], r[3]);
        _glfwInputWindowMaximize(window, GLFW_FALSE);
        relook(window);
    }
}

void _glfwMaximizeWindowPlan9(_GLFWwindow* window)
{
    int* r = window->plan9.restore;
    const int b = window->plan9.border;
    int sw, sh;

    if (window->plan9.maximized)
        return;
    r[0] = window->plan9.xpos - b;
    r[1] = window->plan9.ypos - b;
    r[2] = window->plan9.xpos + window->plan9.width + b;
    r[3] = window->plan9.ypos + window->plan9.height + b;
    _glfwDrawScreenPlan9(&sw, &sh);
    wctl(window, "resize -r 0 0 %d %d", sw, sh);
    window->plan9.maximized = GLFW_TRUE;
    _glfwInputWindowMaximize(window, GLFW_TRUE);
    relook(window);
}

GLFWbool _glfwWindowMaximizedPlan9(_GLFWwindow* window)
{
    return window->plan9.maximized;
}

GLFWbool _glfwWindowHoveredPlan9(_GLFWwindow* window)
{
    return window->plan9.hovered;
}

GLFWbool _glfwFramebufferTransparentPlan9(_GLFWwindow* window)
{
    (void) window;
    return GLFW_FALSE;
}

void _glfwSetWindowResizablePlan9(_GLFWwindow* window, GLFWbool enabled)
{
    (void) window;
    (void) enabled;
}

void _glfwSetWindowDecoratedPlan9(_GLFWwindow* window, GLFWbool enabled)
{
    (void) window;
    (void) enabled;
}

void _glfwSetWindowFloatingPlan9(_GLFWwindow* window, GLFWbool enabled)
{
    (void) window;
    (void) enabled;
}

void _glfwSetWindowMousePassthroughPlan9(_GLFWwindow* window, GLFWbool enabled)
{
    (void) window;
    (void) enabled;
}

float _glfwGetWindowOpacityPlan9(_GLFWwindow* window)
{
    (void) window;
    return 1.f;
}

void _glfwSetWindowOpacityPlan9(_GLFWwindow* window, float opacity)
{
    (void) window;
    (void) opacity;
}

void _glfwSetRawMouseMotionPlan9(_GLFWwindow *window, GLFWbool enabled)
{
    (void) window;
    (void) enabled;
}

GLFWbool _glfwRawMouseMotionSupportedPlan9(void)
{
    return GLFW_FALSE;
}

void _glfwShowWindowPlan9(_GLFWwindow* window)
{
    window->plan9.visible = GLFW_TRUE;
    wctl(window, "unhide");
}

void _glfwRequestWindowAttentionPlan9(_GLFWwindow* window)
{
    (void) window;
}

void _glfwHideWindowPlan9(_GLFWwindow* window)
{
    window->plan9.visible = GLFW_FALSE;
    wctl(window, "hide");
}

void _glfwFocusWindowPlan9(_GLFWwindow* window)
{
    wctl(window, "current");
}

GLFWbool _glfwWindowFocusedPlan9(_GLFWwindow* window)
{
    return window->plan9.focused;
}

GLFWbool _glfwWindowIconifiedPlan9(_GLFWwindow* window)
{
    return window->plan9.iconified;
}

GLFWbool _glfwWindowVisiblePlan9(_GLFWwindow* window)
{
    return window->plan9.visible;
}

void _glfwPollEventsPlan9(void)
{
    _GLFWeventPlan9 e;

    for (;;)
    {
        pthread_mutex_lock(&_glfw.plan9.lock);
        if (_glfw.plan9.tail == _glfw.plan9.head)
        {
            pthread_mutex_unlock(&_glfw.plan9.lock);
            break;
        }
        e = _glfw.plan9.events[_glfw.plan9.tail];
        _glfw.plan9.tail = (_glfw.plan9.tail + 1) % _GLFW_PLAN9_EVENTS;
        pthread_mutex_unlock(&_glfw.plan9.lock);
        handle(e.win ? bywin(e.win) : _glfw.plan9.window, &e);
    }
}

// until an event or glfwPostEmptyEvent; until the time, if one is given
static void waitfor(const struct timespec* until)
{
    pthread_mutex_lock(&_glfw.plan9.lock);
    while (_glfw.plan9.tail == _glfw.plan9.head && !_glfw.plan9.wake)
    {
        if (!until)
            pthread_cond_wait(&_glfw.plan9.cond, &_glfw.plan9.lock);
        else if (pthread_cond_timedwait(&_glfw.plan9.cond, &_glfw.plan9.lock, until) != 0)
            break;
    }
    _glfw.plan9.wake = GLFW_FALSE;
    pthread_mutex_unlock(&_glfw.plan9.lock);
    _glfwPollEventsPlan9();
}

void _glfwWaitEventsPlan9(void)
{
    waitfor(NULL);
}

void _glfwWaitEventsTimeoutPlan9(double timeout)
{
    struct timespec t;
    long long ns;

    clock_gettime(CLOCK_MONOTONIC, &t);
    ns = t.tv_nsec + (long long) (timeout * 1e9);
    t.tv_sec += (time_t) (ns / 1000000000);
    t.tv_nsec = (long) (ns % 1000000000);
    waitfor(&t);
}

void _glfwPostEmptyEventPlan9(void)
{
    pthread_mutex_lock(&_glfw.plan9.lock);
    _glfw.plan9.wake = GLFW_TRUE;
    pthread_cond_signal(&_glfw.plan9.cond);
    pthread_mutex_unlock(&_glfw.plan9.lock);
}

void _glfwGetCursorPosPlan9(_GLFWwindow* window, double* xpos, double* ypos)
{
    if (xpos)
        *xpos = window->plan9.lastx;
    if (ypos)
        *ypos = window->plan9.lasty;
}

void _glfwSetCursorPosPlan9(_GLFWwindow* window, double x, double y)
{
    warp(window, x, y);
}

void _glfwSetCursorModePlan9(_GLFWwindow* window, int mode)
{
    if (mode == GLFW_CURSOR_DISABLED)
        center(window);
    _glfwShowCursorPlan9(window);
}

void _glfwSetCursorPlan9(_GLFWwindow* window, _GLFWcursor* cursor)
{
    (void) cursor;      // window->cursor already
    _glfwShowCursorPlan9(window);
}

EGLenum _glfwGetEGLPlatformPlan9(EGLint** attribs)
{
    (void) attribs;
    return 0;   // eglGetDisplay: Mesa's plan9 platform is its default
}

EGLNativeDisplayType _glfwGetEGLNativeDisplayPlan9(void)
{
    return EGL_DEFAULT_DISPLAY;
}

// "": the process's window; another's directory
EGLNativeWindowType _glfwGetEGLNativeWindowPlan9(_GLFWwindow* window)
{
    if (!window->plan9.d || _glfwDrawIsProcessPlan9(window->plan9.d))
        return (EGLNativeWindowType) "";
    return (EGLNativeWindowType) _glfwDrawDirPlan9(window->plan9.d);
}

void _glfwGetRequiredInstanceExtensionsPlan9(char** extensions)
{
    if (!_glfw.vk.KHR_surface)
        return;
    extensions[0] = "VK_KHR_surface";
    extensions[1] = "VK_PLAN9_surface";
}

GLFWbool _glfwGetPhysicalDevicePresentationSupportPlan9(VkInstance instance,
                                                        VkPhysicalDevice device,
                                                        uint32_t queuefamily)
{
    (void) instance;
    (void) device;
    (void) queuefamily;
    return GLFW_TRUE;
}

VkResult _glfwCreateWindowSurfacePlan9(VkInstance instance,
                                       _GLFWwindow* window,
                                       const VkAllocationCallbacks* allocator,
                                       VkSurfaceKHR* surface)
{
    PFN_vkCreatePlan9SurfacePLAN9 create;
    VkPlan9SurfaceCreateInfoPLAN9 info;
    VkResult err;

    create = (PFN_vkCreatePlan9SurfacePLAN9) vkGetInstanceProcAddr(instance, "vkCreatePlan9SurfacePLAN9");
    if (!create)
    {
        _glfwInputError(GLFW_API_UNAVAILABLE, "plan9: Vulkan instance missing VK_PLAN9_surface extension");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_PLAN9_SURFACE_CREATE_INFO_PLAN9;
    // NULL: the process's window (/dev/winname, else the screen); another's directory
    info.pWindowName = _glfwDrawIsProcessPlan9(window->plan9.d) ? NULL : _glfwDrawDirPlan9(window->plan9.d);
    err = create(instance, &info, allocator, surface);
    if (err)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "plan9: Failed to create Vulkan surface: %s",
                        _glfwGetVulkanResultString(err));
    }
    return err;
}

#endif // _GLFW_PLAN9
