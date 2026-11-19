/*
 * pANS stdio -- ungetc
 */
#include "iolib.h"
static int ungetc_l(int c, FILE *f){
	if(c==EOF) return EOF;
	switch(f->state){
	default:	/* WR */
		f->state=ERR;
		return EOF;
	case CLOSED:
	case ERR:
		return EOF;
	case OPEN:
		_IO_setvbuf(f);
	case RDWR:
	case END:
		f->wp=f->buf;
		if(f->bufl==0)
			f->wp += 1;
		else
			f->wp += f->bufl;
		f->rp = f->wp;
		f->state=RD;
	case RD:
		if(f->rp==f->buf) return EOF;
		if(f->flags&STRING)
			f->rp--;
		else
			*--f->rp=c;
		return (char)c;
	}
}

/* 9space: under the stream's lock (iolib.h) */
int
ungetc(int c, FILE *f)
{
	int r;

	_IO_lock(f);
	r = ungetc_l(c, f);
	_IO_unlock(f);
	return r;
}
