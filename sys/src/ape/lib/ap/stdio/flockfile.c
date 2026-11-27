/*
 * 9space: a stream's lock, flockfile, ftrylockfile, funlockfile (POSIX)
 * and every stdio function's (iolib.h's _IO_lock), as OpenBSD's libc
 * stdio (FLOCKFILE, flockfile.c; ISC/BSD licence) does it: recursive,
 * the owner a thread (here a proc: its pid), and only once a second
 * thread runs (__isthreaded, set by pthread.c): a program of one proc
 * pays nothing.
 *
 * The locks are not in the FILE: its size is APE's ABI (stdout is
 * &_IO_stream[1] in code already compiled, gcc's libstdc++ among it).
 * They are found by the FILE's address in a table that only grows (a
 * FILE is never freed either: _IO_newfile.c): a lookup reads a chain
 * without a lock, an insertion takes one.  The word is the three-state
 * mutex of pthread.c (Drepper's "Futexes are tricky", with a 9front
 * semaphore for the futex).
 *
 * kencc has no __atomic builtins, and its programs no pthreads
 * (gnu/pthread.c is gcc's): there the word is a lock.h Lock and a lookup
 * takes the table's lock; only an explicit flockfile gets here.
 */
#define _PLAN9_SOURCE
#include "iolib.h"
#include <lock.h>
#include <stdint.h>

extern int _SEMACQUIRE(int *, int);
extern int _SEMRELEASE(int *, int);

int __isthreaded;

typedef struct Flock Flock;
struct Flock {
	FILE	*f;
	int	lk;	/* 0 free, 1 locked, 2 locked with waiters */
	int	sem;
	int	owner;	/* the holder's pid */
	int	count;	/* how often it holds it */
	Flock	*next;
#ifndef __GNUC_MINOR__
	Lock	wl;	/* kencc: the word */
#endif
};

enum {
	Nhash = 64,
};

static Flock *table[Nhash];
static Lock tablelock;

#ifdef __GNUC_MINOR__
#define	loadhead(h)	__atomic_load_n(h, __ATOMIC_ACQUIRE)
#define	storehead(h, l)	__atomic_store_n(h, l, __ATOMIC_RELEASE)
#else
#define	loadhead(h)	NULL	/* kencc: no lookup without the lock */
#define	storehead(h, l)	(*(h) = (l))
#endif

static Flock *
flockof(FILE *f)
{
	Flock **h, *l;

	h = &table[((uintptr_t)f / sizeof(FILE)) % Nhash];
	for(l = loadhead(h); l != NULL; l = l->next)
		if(l->f == f)
			return l;
	lock(&tablelock);
	for(l = *h; l != NULL; l = l->next)
		if(l->f == f)
			break;
	if(l == NULL){
		l = calloc(1, sizeof *l);
		if(l == NULL){
			unlock(&tablelock);
			abort();	/* out of memory: no way to lock the stream */
		}
		l->f = f;
		l->next = *h;
		storehead(h, l);
	}
	unlock(&tablelock);
	return l;
}

#ifdef __GNUC_MINOR__
static int
trylockword(Flock *l)
{
	int c;

	c = 0;
	return __atomic_compare_exchange_n(&l->lk, &c, 1, 0,
	    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void
lockword(Flock *l)
{
	int c;

	c = 0;
	if(__atomic_compare_exchange_n(&l->lk, &c, 1, 0,
	    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return;
	if(c != 2)
		c = __atomic_exchange_n(&l->lk, 2, __ATOMIC_ACQUIRE);
	while(c != 0){
		while(_SEMACQUIRE(&l->sem, 1) < 0)
			;	/* a note: the wait goes on */
		c = __atomic_exchange_n(&l->lk, 2, __ATOMIC_ACQUIRE);
	}
}

static void
unlockword(Flock *l)
{
	if(__atomic_fetch_sub(&l->lk, 1, __ATOMIC_RELEASE) != 1){
		__atomic_store_n(&l->lk, 0, __ATOMIC_RELEASE);
		_SEMRELEASE(&l->sem, 1);
	}
}
#else
static int
trylockword(Flock *l)
{
	return canlock(&l->wl);
}

static void
lockword(Flock *l)
{
	lock(&l->wl);
}

static void
unlockword(Flock *l)
{
	unlock(&l->wl);
}
#endif

void
flockfile(FILE *f)
{
	Flock *l;
	int me;

	l = flockof(f);
	me = getpid();
	if(l->owner == me){
		l->count++;
		return;
	}
	lockword(l);
	l->owner = me;
	l->count = 1;
}

int
ftrylockfile(FILE *f)
{
	Flock *l;
	int me;

	l = flockof(f);
	me = getpid();
	if(l->owner == me){
		l->count++;
		return 0;
	}
	if(!trylockword(l))
		return -1;
	l->owner = me;
	l->count = 1;
	return 0;
}

void
funlockfile(FILE *f)
{
	Flock *l;

	l = flockof(f);
	if(l->owner != getpid() || l->count <= 0)
		return;	/* not the holder's: POSIX leaves it undefined */
	if(--l->count == 0){
		l->owner = 0;
		unlockword(l);
	}
}

/* fork's child (_IO_newfile.c's _IO_forkchild): every lock free */
void
_IO_flockforkchild(void)
{
	Flock *l;
	int i;

	tablelock.val = 0;
	for(i = 0; i < Nhash; i++)
		for(l = table[i]; l != NULL; l = l->next){
			l->lk = 0;
			l->sem = 0;
			l->owner = 0;
			l->count = 0;
#ifndef __GNUC_MINOR__
			l->wl.val = 0;
#endif
		}
}
