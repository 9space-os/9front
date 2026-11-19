/*
 * pANS stdio -- fgetpos
 */
#include "iolib.h"
static int fgetpos_l(FILE *f, fpos_t *pos){
	*pos=ftell(f);
	return *pos==-1?-1:0;
}

/* 9space: under the stream's lock (iolib.h) */
int
fgetpos(FILE *f, fpos_t *pos)
{
	int r;

	_IO_lock(f);
	r = fgetpos_l(f, pos);
	_IO_unlock(f);
	return r;
}
