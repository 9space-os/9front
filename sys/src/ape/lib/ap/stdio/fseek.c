/*
 * pANS stdio -- fseek
 */
#include "iolib.h"
static int fseek_l(FILE *f, long offs, int type){
	switch(f->state){
	case ERR:
	case CLOSED:
		return -1;
	case WR:
		fflush(f);
		break;
	case RD:
		if(type==1 && f->buf!=f->unbuf)
			offs-=f->wp-f->rp;
		break;
	}
	if(f->flags&STRING || lseek(f->fd, offs, type)==-1)
		return -1;
	if(f->state==RD) f->rp=f->wp=f->buf;
	if(f->state!=OPEN)
		f->state=RDWR;
	return 0;
}

/* 9space: under the stream's lock (iolib.h) */
int
fseek(FILE *f, long offs, int type)
{
	int r;

	_IO_lock(f);
	r = fseek_l(f, offs, type);
	_IO_unlock(f);
	return r;
}
