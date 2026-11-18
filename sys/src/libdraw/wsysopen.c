#include <u.h>
#include <libc.h>
#include <draw.h>

/*
 * 9space: more windows than the process's, under wsys(4): its clone files.
 * A window of another (parent) is a dialog or popup of it: above it,
 * moving, hidden and going with it; without a parent the process's window
 * is it (/dev/clone), "parent -" in msgs makes a window of its own.  msgs:
 * wsys(4) ctl lines, one a line (rect, modal, popup [grab], askclose,
 * parent, label), written before the window comes on the screen (top).
 * Its files are in w->dir (mouse, kbd, cons, wctl, event, label, ...); its
 * image and screen in w->image, w->screen (display's: the same draw(3)
 * connection as the process's window), again after a resize
 * (wsysgetwindow).  The window lives while its clone file is open
 * (wsysclose).
 *
 * w->dir stays: wsys' /dev/wsys/n is bound at /mnt/drawwin.k (mntgen's
 * /mnt; k the window's key in the process).  When the wsys is gone
 * (killed, crashed) wsysagain makes the process's window in the next one,
 * then each window of wsysopen again there as it was (its messages, its
 * place, its label), parents first, and binds it at the same directory:
 * the program opens its files again where they were, as the process's
 * window's at /dev.  As p9win's win.rs (toolchain/rust) does for Rust.
 */

static QLock wlk;
static Wsyswin *wins;		/* the windows of wsysopen, in the order made */
static int nextkey;

/*
 * the directory the windows of wsys are in: in a window's name space
 * /mnt/wsys is the window (newwindow's mount) and its wsys/ the root; where
 * wsys' root is mounted there (its -i program), /mnt/wsys itself; else
 * /dev/wsys (bound there only; alone it may be rio's: wsys in a rio window)
 */
static char*
wsysdir(void)
{
	if(access("/mnt/wsys/wsys", AEXIST) == 0)
		return "/mnt/wsys/wsys";	/* (its windows only: the clone files are theirs) */
	if(access("/mnt/wsys/clone", AEXIST) == 0)
		return "/mnt/wsys";
	return "/dev/wsys";
}

static int
ctlmsgs(int fd, char *msgs)
{
	char *s, *e, *l;
	int n;

	if(msgs == nil)
		return 0;
	s = strdup(msgs);
	if(s == nil)
		return -1;
	for(l = s; *l != 0; l = e){
		if((e = strchr(l, '\n')) != nil)
			*e++ = 0;
		else
			e = l + strlen(l);
		n = strlen(l);
		if(n > 0 && write(fd, l, n) != n){
			free(s);
			return -1;
		}
	}
	free(s);
	return 0;
}

static int
readid(int fd)
{
	char buf[128], *f[10];
	int n;

	n = pread(fd, buf, sizeof buf - 1, 0);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) < 1)
		return -1;
	return atoi(f[0]);
}

static void
readsrv(char *buf, int n)
{
	int fd, m;

	buf[0] = 0;
	if((fd = open("/env/wsys", OREAD)) < 0)
		return;
	m = read(fd, buf, n-1);
	close(fd);
	if(m < 0)
		m = 0;
	buf[m] = 0;
	if(m > 0 && buf[m-1] == 0)
		buf[m-1] = 0;
}

/* w made in the process's wsys (again): its clone file, its messages, its place and label; at w->dir */
static int
make(Wsyswin *w)
{
	char path[64], buf[128];
	int fd, id;

	if(!w->own && w->parent != nil)
		snprint(path, sizeof path, "%s/clone", w->parent->dir);
	else if(access("/dev/clone", AEXIST) == 0)
		strcpy(path, "/dev/clone");
	else
		snprint(path, sizeof path, "%s/clone", wsysdir());
	if((fd = open(path, ORDWR|OCEXEC)) < 0)
		return -1;
	if((id = readid(fd)) < 0){
		werrstr("%s: no window number", path);
		close(fd);
		return -1;
	}
	if((w->own && write(fd, "parent -", 8) != 8) || ctlmsgs(fd, w->msgs) < 0){
		close(fd);
		return -1;
	}
	if(!badrect(w->r)){
		snprint(buf, sizeof buf, "rect %d %d %d %d", w->r.min.x, w->r.min.y, w->r.max.x, w->r.max.y);
		write(fd, buf, strlen(buf));
	}
	if(w->label != nil){
		snprint(buf, sizeof buf, "label %s", w->label);
		write(fd, buf, strlen(buf));
	}
	/* the old one's files (gone with its wsys) out, the new one's in */
	snprint(path, sizeof path, "/mnt/drawwin.%d", w->key);
	while(unmount(nil, path) >= 0)
		;
	snprint(buf, sizeof buf, "%s/%d", wsysdir(), id);
	if(bind(buf, path, MREPL) >= 0)
		snprint(w->dir, sizeof w->dir, "%s", path);
	else
		snprint(w->dir, sizeof w->dir, "%s", buf);	/* no mntgen: it does not stay */
	if(w->ctl >= 0)
		close(w->ctl);
	w->ctl = fd;
	w->id = id;
	readsrv(w->srv, sizeof w->srv);
	return 0;
}

/* the length of msg's command (its first word) */
static int
cmdlen(char *msg)
{
	int n;

	for(n = 0; msg[n] != 0 && msg[n] != ' ' && msg[n] != '\n'; n++)
		;
	return n;
}

/*
 * w->msgs without the lines of msg's command; with msg instead if keep.
 * Whether there was such a line
 */
static int
setmsg(Wsyswin *w, char *msg, int keep)
{
	char *s, *l, *e, *o;
	int n, had;

	n = cmdlen(msg);
	had = 0;
	s = w->msgs != nil ? w->msgs : "";
	o = mallocz(strlen(s) + strlen(msg) + 3, 1);
	if(o == nil)
		return 0;
	for(l = s; *l != 0; l = e){
		if((e = strchr(l, '\n')) != nil)
			e++;
		else
			e = l + strlen(l);
		if(cmdlen(l) == n && strncmp(l, msg, n) == 0){
			had = 1;
			continue;
		}
		strncat(o, l, e-l);
		if(e[-1] != '\n')
			strcat(o, "\n");
	}
	if(keep){
		strcat(o, msg);
		strcat(o, "\n");
	}
	free(w->msgs);
	w->msgs = o;
	return had;
}

Wsyswin*
wsysopen(Wsyswin *parent, char *msgs)
{
	Wsyswin *w, **l;

	w = mallocz(sizeof *w, 1);
	if(w == nil)
		return nil;
	w->ctl = -1;
	w->parent = parent;
	w->r = Rect(0, 0, 0, 0);
	if(msgs != nil){
		if((w->msgs = strdup(msgs)) == nil){
			free(w);
			return nil;
		}
		/* "parent -" kept as own (wsysparent changes it), the rest as they are */
		if(setmsg(w, "parent -", 0))
			w->own = 1;
	}
	qlock(&wlk);
	w->key = ++nextkey;
	if(make(w) < 0)
		goto Err;
	qunlock(&wlk);
	if(write(w->ctl, "top", 3) != 3){
		werrstr("wsys: top: %r");
		goto Err1;
	}
	/* (the process's draw(3) connection: the window's image; else the caller's own) */
	if(display != nil && wsysgetwindow(w) < 0)
		goto Err1;
	qlock(&wlk);
	for(l = &wins; *l != nil; l = &(*l)->next)
		;
	*l = w;
	qunlock(&wlk);
	return w;
Err1:
	qlock(&wlk);
Err:
	qunlock(&wlk);
	if(w->ctl >= 0)
		close(w->ctl);
	free(w->msgs);
	free(w);
	return nil;
}

/*
 * the windows of wsysopen made again in the process's next wsys (wsysagain
 * calls it once the process's window is there), each once, parents first;
 * one gone with its parent stays gone (w->gone)
 */
void
_wsysremake(void)
{
	Wsyswin *w;
	int again, ok;

	qlock(&wlk);
	/* passes until none comes: a parent made later than its dialog (wsysparent) first */
	do{
		again = 0;
		for(w = wins; w != nil; w = w->next){
			if(w->gone || readid(w->ctl) >= 0)
				continue;	/* gone, or in this wsys already */
			if(!w->own && w->parent != nil){
				if(w->parent->gone){
					w->gone = 1;
					continue;
				}
				if(readid(w->parent->ctl) < 0)
					continue;	/* its parent not yet */
			}
			ok = make(w) == 0 && write(w->ctl, "top", 3) == 3;
			if(!ok)
				w->gone = 1;
			again = 1;
		}
	}while(again);
	qunlock(&wlk);
}

/* whether w's wsys is gone and w not yet made again in the next one */
int
wsyswinlost(Wsyswin *w)
{
	char srv[sizeof w->srv];

	readsrv(srv, sizeof srv);
	return wsyslost() || strcmp(srv, w->srv) != 0;
}

/*
 * where the window is now: its contents (ctl: id x0 y0 x1 y1 ...), as the
 * ctl message rect wants them (wctl's is the frame: border, title bar);
 * kept for the next wsys
 */
void
wsysseen(Wsyswin *w)
{
	char buf[256], *f[5];
	int n;

	n = pread(w->ctl, buf, sizeof buf - 1, 0);
	if(n <= 0)
		return;
	buf[n] = 0;
	if(tokenize(buf, f, nelem(f)) == 5)
		w->r = Rect(atoi(f[1]), atoi(f[2]), atoi(f[3]), atoi(f[4]));
}

/* the window's image (again, after a resize: its name changes) */
int
wsysgetwindow(Wsyswin *w)
{
	char name[64];

	Image *si;
	int r;

	snprint(name, sizeof name, "%s/winname", w->dir);
	/* gengetwindow notes it as the display's screenimage: that is the process's window's */
	si = display->screenimage;
	r = gengetwindow(display, name, &w->image, &w->screen, Refnone);
	if(si != nil)
		display->screenimage = si;	/* (a display of the window's alone: it then) */
	if(r < 0)
		return -1;
	wsysseen(w);
	return 0;
}

/* a ctl message to the window (wsys(4): rect, label, top, delete, ...); its place and label kept for the next wsys */
int
wsysctl(Wsyswin *w, char *msg)
{
	Rectangle r;
	char *s;
	int n;

	n = strlen(msg);
	if(write(w->ctl, msg, n) != n)
		return -1;
	if(strncmp(msg, "rect ", 5) == 0){
		s = msg+5;
		r.min.x = strtol(s, &s, 10);
		r.min.y = strtol(s, &s, 10);
		r.max.x = strtol(s, &s, 10);
		r.max.y = strtol(s, &s, 10);
		w->r = r;
	}else if(strncmp(msg, "label ", 6) == 0){
		free(w->label);
		w->label = strdup(msg+6);
	}else if(strncmp(msg, "modal", 5) == 0 || strncmp(msg, "askclose", 8) == 0 || strncmp(msg, "popup", 5) == 0){
		qlock(&wlk);
		setmsg(w, msg, 1);
		qunlock(&wlk);
	}
	return 0;
}

/*
 * w a dialog or popup of p (nil: of the process's window), or with none a
 * window of its own; so in the next wsys too
 */
int
wsysparent(Wsyswin *w, Wsyswin *p, int none)
{
	char buf[32];
	int fd, n, id;

	if(none)
		strcpy(buf, "parent -");
	else{
		if(p != nil)
			id = p->id;
		else{
			if((fd = open("/dev/winid", OREAD)) < 0)
				return -1;
			n = read(fd, buf, sizeof buf - 1);
			close(fd);
			if(n <= 0)
				return -1;
			buf[n] = 0;
			id = atoi(buf);
		}
		snprint(buf, sizeof buf, "parent %d", id);
	}
	n = strlen(buf);
	if(write(w->ctl, buf, n) != n)
		return -1;
	qlock(&wlk);
	w->own = none;
	w->parent = none ? nil : p;
	qunlock(&wlk);
	return 0;
}

/* the window gone (its clone file closed); its dialogs and popups go with it */
void
wsysclose(Wsyswin *w)
{
	Wsyswin **l, *c;
	Display *d;
	char path[64];

	if(w == nil)
		return;
	qlock(&wlk);
	for(l = &wins; *l != nil; l = &(*l)->next)
		if(*l == w){
			*l = w->next;
			break;
		}
	for(c = wins; c != nil; c = c->next)
		if(c->parent == w){
			c->parent = nil;
			c->gone = 1;
		}
	qunlock(&wlk);
	/* its images let go at once: draw(3) keeps a window held by a client */
	d = w->image != nil ? w->image->display : nil;
	if(w->image != nil)
		freeimage(w->image);
	if(w->screen != nil){
		/* the window by name under it (gengetwindow's) */
		if(w->screen->image != nil && (d == nil || w->screen->image != d->image))
			freeimage(w->screen->image);
		freescreen(w->screen);
	}
	if(d != nil)
		flushimage(d, 1);
	close(w->ctl);
	snprint(path, sizeof path, "/mnt/drawwin.%d", w->key);
	while(unmount(nil, path) >= 0)
		;
	free(w->msgs);
	free(w->label);
	free(w);
}
