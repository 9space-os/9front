/*
 * pANS stdio -- fgetc
 */
#include "iolib.h"
static int fgetc_l(FILE *f){
	return getc_unlocked(f);
}

/* 9space: under the stream's lock (iolib.h) */
int
fgetc(FILE *f)
{
	int r;

	_IO_lock(f);
	r = fgetc_l(f);
	_IO_unlock(f);
	return r;
}
