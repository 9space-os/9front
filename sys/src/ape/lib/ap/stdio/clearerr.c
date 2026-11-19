/*
 * pANS stdio -- clearerr
 */
#include "iolib.h"
static void clearerr_l(FILE *f){
	switch(f->state){
	case ERR:
		f->state=f->buf?RDWR:OPEN;
		break;
	case END:
		f->state=RDWR;
		break;
	}
}

/* 9space: under the stream's lock (iolib.h) */
void
clearerr(FILE *f)
{
	_IO_lock(f);
	clearerr_l(f);
	_IO_unlock(f);
}
