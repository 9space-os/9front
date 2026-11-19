/*
 * pANS stdio -- ftello
 */
#include "iolib.h"
static off_t ftello_l(FILE *f){
	off_t seekp=lseek(f->fd, 0L, 1);
	if(seekp<0) return -1;		/* enter error state? */
	switch(f->state){
	default:
		return seekp;
	case RD:
		return seekp-(f->wp-f->rp);
	case WR:
		return (f->flags&LINEBUF?f->lp:f->wp)-f->buf+seekp;
	}
}

/* 9space: under the stream's lock (iolib.h) */
off_t
ftello(FILE *f)
{
	off_t r;

	_IO_lock(f);
	r = ftello_l(f);
	_IO_unlock(f);
	return r;
}
