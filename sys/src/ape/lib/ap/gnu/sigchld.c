/*
 * POSIX's SIGCHLD for APE, which Plan 9 does not send: a program with a
 * handler for it gets, at its first fork, a watcher thread that reads the
 * forking proc's wait messages (/proc/n/wait: it waits while there are
 * none, also when there are no children), keeps them and, while there
 * is a handler, posts the note "child" to that proc (plan9/signal.c:
 * SIGCHLD; its default, as POSIX's, is to ignore it, and an ignored one
 * interrupts no system call: no note then). wait4 (plan9/wait.c) takes them from here then, as
 * only one may read the wait messages (await would fail: "in use").
 * Procs other than the one that forked first (a thread that forks) wait
 * as before. The watcher goes with the program (pthread's exit) and before
 * an exec (plan9/execve.c): the new program's children are its own.
 */
#define _PLAN9_SOURCE
#define _RESEARCH_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "../plan9/lib.h"
#include "../plan9/sys9.h"

extern Waitmsg *_parsewait(char *);

typedef struct Msg Msg;
struct Msg {
	Waitmsg	*w;
	Msg	*next;
};

static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static int watched;	/* the proc whose children are watched; 0: none */
static int watcher;	/* the watcher's proc */
static pthread_cond_t more = PTHREAD_COND_INITIALIZER;	/* a message came */
static int pending;	/* a SIGCHLD while it was blocked: sent when unblocked */

/*
 * the children each proc forked, whose wait message has not come (the
 * watcher's, or wait's own read of it: __sigchld_reaped): with none of
 * the watched proc's (or not pid among them) and no message kept, wait
 * is ECHILD, as the kernel's await says "no living children"
 * (/proc/n/wait just blocks then). Kept from the first fork on, so the
 * children forked before the handler was set are known too; a forked
 * child's copy holds its parent's, which its own pid leaves out.
 */
typedef struct Kid Kid;
struct Kid {
	int	parent;
	int	pid;
};
static Kid *kids;
static int nkids, akids;

/* with lk: pid's entry among parent's children, or -1 */
static int
kidat(int parent, int pid)
{
	int i;

	for(i = 0; i < nkids; i++)
		if(kids[i].parent == parent && kids[i].pid == pid)
			return i;
	return -1;
}

/* with lk: parent has a child that has not ended */
static int
haskids(int parent)
{
	int i;

	for(i = 0; i < nkids; i++)
		if(kids[i].parent == parent)
			return 1;
	return 0;
}

/* with lk */
static void
dropkid(int parent, int pid)
{
	int i;

	if((i = kidat(parent, pid)) >= 0)
		kids[i] = kids[--nkids];
}

/*
 * the signal masks of the program's procs: POSIX's are per thread, APE's
 * _psigblocked is one for all (a thread that blocks everything, as foot's
 * render threads do, blocked SIGCHLD for main too); sigprocmask tells
 * each proc's here, the watcher asks for the watched proc's
 */
enum { Nmask = 64 };
static struct {
	int	pid;
	long	mask;
} masks[Nmask];

/* with lk */
static long
maskof(int pid)
{
	int i;

	for(i = 0; i < Nmask; i++)
		if(masks[i].pid == pid)
			return masks[i].mask;
	return 0;	/* never changed: none blocked */
}
static Msg *msgs, **lastmsg = &msgs;

/* the note "child" to pid (SIGCHLD) */
static void
post(int pid)
{
	char path[64];
	int fd;

	snprintf(path, sizeof path, "/proc/%d/note", pid);
	if((fd = open(path, O_WRONLY)) >= 0){
		write(fd, "child", 5);
		close(fd);
	}
}

/*
 * a proc's signal mask changed (plan9/sigprocmask.c): kept; SIGCHLD no
 * longer blocked in the watched proc: a pending one now, as POSIX's
 * sigprocmask does
 */
void
__sigchld_mask(long mask)
{
	int i, free, pid, p;

	pid = getpid();
	pthread_mutex_lock(&lk);
	free = -1;
	for(i = 0; i < Nmask; i++){
		if(masks[i].pid == pid)
			break;
		if(masks[i].pid == 0 && free < 0)
			free = i;
	}
	if(i == Nmask)
		i = free >= 0 ? free : pid % Nmask;
	masks[i].pid = pid;
	masks[i].mask = mask;
	p = pending && watched == pid && !(mask & (2<<SIGCHLD));
	if(p)
		pending = 0;
	pthread_mutex_unlock(&lk);
	if(p)
		post(pid);	/* delivered as this write returns */
}

/* with lk: pid is parent's child (no room: not counted, its wait may say ECHILD early) */
static void
addkid(int parent, int pid)
{
	Kid *k;

	if(nkids == akids){
		k = realloc(kids, (akids+16) * sizeof *kids);
		if(k == NULL)
			return;
		kids = k;
		akids += 16;
	}
	kids[nkids].parent = parent;
	kids[nkids].pid = pid;
	nkids++;
}

/* wait read pid's message itself (no watcher then): no longer a child */
void
__sigchld_reaped(int pid)
{
	pthread_mutex_lock(&lk);
	dropkid(getpid(), pid);
	pthread_mutex_unlock(&lk);
}

static void *
watch(void *a)
{
	char path[64], buf[512];
	Msg *m;
	Waitmsg *w;
	void (*h)(int, char*, Ureg*);
	int pid, fd, n;

	pid = (int)(intptr_t)a;
	watcher = getpid();
	snprintf(path, sizeof path, "/proc/%d/wait", pid);
	if((fd = open(path, O_RDONLY)) < 0)
		return NULL;
	for(;;){
		n = read(fd, buf, sizeof buf - 1);
		if(n <= 0){
			if(n < 0 && errno == EINTR)
				continue;
			break;	/* the proc is gone */
		}
		buf[n] = 0;
		if((w = _parsewait(buf)) == NULL || (m = malloc(sizeof *m)) == NULL){
			free(w);
			continue;
		}
		m->w = w;
		m->next = NULL;
		pthread_mutex_lock(&lk);
		dropkid(pid, w->pid);
		*lastmsg = m;
		lastmsg = &m->next;
		pthread_cond_broadcast(&more);
		pthread_mutex_unlock(&lk);
		/* the note only for a handler: an ignored signal must not interrupt a system call */
		h = _sighdlr[SIGCHLD];
		if(h == SIG_DFL || h == SIG_IGN || h == SIG_ERR)
			continue;
		/* blocked (sigprocmask): pending, sent by sigprocmask's unblocking (a note would interrupt a system call now) */
		pthread_mutex_lock(&lk);
		if(maskof(pid) & (2<<SIGCHLD)){
			pending = 1;
			pthread_mutex_unlock(&lk);
			continue;
		}
		pthread_mutex_unlock(&lk);
		post(pid);
	}
	close(fd);
	return NULL;
}

/* fork made child pid (in its parent): the watcher, if SIGCHLD is handled and there is none */
void
__sigchld_forked(int pid)
{
	pthread_t t;
	pthread_attr_t a;
	void (*h)(int, char*, Ureg*);

	pthread_mutex_lock(&lk);
	addkid(getpid(), pid);
	h = _sighdlr[SIGCHLD];
	if(h == SIG_DFL || h == SIG_IGN || h == SIG_ERR || watched != 0){
		pthread_mutex_unlock(&lk);
		return;
	}
	watched = getpid();
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&a, 64*1024);
	if(pthread_create(&t, &a, watch, (void*)(intptr_t)watched) != 0)
		watched = 0;
	pthread_attr_destroy(&a);
	pthread_mutex_unlock(&lk);
}

/* 1 if this proc's wait messages are the watcher's */
int
__sigchld_watching(void)
{
	return watched != 0 && watched == getpid();
}

/*
 * a child's wait message the watcher kept: pid's (<= 0: any); nil if none
 * and nohang, or (*gone set) if no such child is left: ECHILD; else
 * waits for the next
 */
Waitmsg *
__sigchld_take(int pid, int nohang, int *gone)
{
	Msg **l, *m;
	Waitmsg *w;

	*gone = 0;
	pthread_mutex_lock(&lk);
	for(;;){
		for(l = &msgs; (m = *l) != NULL; l = &m->next)
			if(pid <= 0 || m->w->pid == pid)
				break;
		if(m != NULL)
			break;
		if(pid <= 0 ? !haskids(watched) : kidat(watched, pid) < 0){
			*gone = 1;
			pthread_mutex_unlock(&lk);
			return NULL;
		}
		if(nohang){
			pthread_mutex_unlock(&lk);
			return NULL;
		}
		pthread_cond_wait(&more, &lk);
	}
	*l = m->next;
	if(lastmsg == &m->next)
		lastmsg = l;
	pthread_mutex_unlock(&lk);
	w = m->w;
	free(m);
	return w;
}

/* an exec: the new program's children are not the old one's watcher's */
void
__sigchld_exec(void)
{
	char path[64];
	int fd;

	if(!__sigchld_watching() || watcher == 0)
		return;
	snprintf(path, sizeof path, "/proc/%d/ctl", watcher);
	if((fd = open(path, O_WRONLY)) >= 0){
		write(fd, "kill", 4);
		close(fd);
	}
	watched = 0;
	watcher = 0;
}
