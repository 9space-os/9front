/*
 * pANS stdio -- fopen
 */
#include "iolib.h"

#define _PLAN9_SOURCE
#include <lock.h>
#include <errno.h>

/*
 * 9front-ng: FOPEN_MAX is the number of streams there always are
 * (_IO_stream), not a limit: past them streams come from the heap, Nblock
 * at a time, and are kept for reuse (a FILE is never freed, a pointer to a
 * closed one stays a FILE, as _IO_stream's do). The FILE found is reserved
 * under the lock (open, no descriptor, no buffer): two threads opening at
 * once got the same one.
 */
enum {
	Nblock = 64,
};

typedef struct Fblock Fblock;
struct Fblock {
	Fblock	*next;
	FILE	f[Nblock];
};

static Lock fl;
static Fblock *blocks;	/* newest first */

static void
reserve(FILE *f)
{
	f->fd=-1;
	f->flags=0;
	f->state=OPEN;
	f->buf=0;
	f->rp=0;
	f->wp=0;
	f->lp=0;
	f->bufl=0;
}

FILE *_IO_newfile(void)
{
	static FILE *fx=0;
	Fblock *b;
	FILE *f;
	int i;

	lock(&fl);
	for(i=0; i<FOPEN_MAX; i++){
		if(fx==0 || ++fx >= &_IO_stream[FOPEN_MAX]) fx=_IO_stream;
		if(fx->state==CLOSED){
			f = fx;
			goto Found;
		}
	}
	for(b=blocks; b!=NULL; b=b->next)
		for(f=b->f; f<&b->f[Nblock]; f++)
			if(f->state==CLOSED)
				goto Found;
	if((b=calloc(1, sizeof *b))==NULL){	/* calloc: all CLOSED */
		unlock(&fl);
		errno = EMFILE;
		return NULL;
	}
	b->next=blocks;
	blocks=b;
	f=b->f;
Found:
	reserve(f);
	unlock(&fl);
	return f;
}

/* the stream after f, _IO_stream's then the heap's; the first for NULL, NULL after the last */
FILE *_IO_nextfile(FILE *f)
{
	Fblock *b;

	if(f==NULL)
		return _IO_stream;
	if(f>=_IO_stream && f<&_IO_stream[FOPEN_MAX]){
		if(++f<&_IO_stream[FOPEN_MAX])
			return f;
		lock(&fl);
		b=blocks;
		unlock(&fl);
		return b!=NULL? b->f : NULL;
	}
	lock(&fl);
	for(b=blocks; b!=NULL; b=b->next)
		if(f>=b->f && f<&b->f[Nblock])
			break;
	if(b==NULL)
		f=NULL;		/* not a stream of ours */
	else if(++f>=&b->f[Nblock])
		f=b->next!=NULL? b->next->f : NULL;
	unlock(&fl);
	return f;
}

/*
 * 9space: fork's child (plan9/fork.c): the list's lock free, and the
 * streams' (flockfile.c); their holders, threads of the parent, do not
 * run in the child (POSIX: only the thread that forked)
 */
void _IO_forkchild(void)
{
	extern void _IO_flockforkchild(void);

	fl.val = 0;
	_IO_flockforkchild();
}
