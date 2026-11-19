/*
 * pANS stdio -- ferror
 */
#include "iolib.h"
static int ferror_l(FILE *f){
	return f->state==ERR;
}

/* 9space: under the stream's lock (iolib.h) */
int
ferror(FILE *f)
{
	int r;

	_IO_lock(f);
	r = ferror_l(f);
	_IO_unlock(f);
	return r;
}
