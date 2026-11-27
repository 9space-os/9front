/*
 * pANS stdio -- feof
 */
#include "iolib.h"
static int feof_l(FILE *f){
	return f->state==END;
}

/* 9space: under the stream's lock (iolib.h) */
int
feof(FILE *f)
{
	int r;

	_IO_lock(f);
	r = feof_l(f);
	_IO_unlock(f);
	return r;
}
