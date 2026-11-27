#include "lib.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "sys9.h"

int
close(int d)
{
	int n;
	Fdinfo *f;

	n = -1;
	f = &_fdinfo[d];
	if(d<0 || d>=OPEN_MAX || !(f->flags&FD_ISOPEN))
		errno = EBADF;
	else{
		if(f->flags&(FD_BUFFERED|FD_BUFFEREDX)) {
			if(f->flags&FD_BUFFERED)
				_closebuf(d);
			f->flags &= ~FD_BUFFERED;
		}
		_dupclose(d);	/* 9front-ng: out of its group of dups (gnu/buf.c) */
		_fdinfo[d].gen++;
		_fdinfo[d].flags = 0;
		_fdinfo[d].oflags = 0;
		if(_fdinfo[d].name){
			free(_fdinfo[d].name);
			_fdinfo[d].name = 0;
		}
		n = _CLOSE(d);
		if(n < 0)
			_syserrno();
	}
	return n;
}

/*
 * 9space: descriptor d's generation: it changes when d is closed (also
 * by dup2 onto it), so a library that knows what d was (Mesa's
 * drm9.c: a GPU context) can tell it is still that without a stat
 */
unsigned long
__fdgen(int d)
{
	if(d < 0 || d >= OPEN_MAX)
		return 0;
	return _fdinfo[d].gen;
}
