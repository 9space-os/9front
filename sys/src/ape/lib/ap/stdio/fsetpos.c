/*
 * pANS stdio -- fsetpos
 */
#include "iolib.h"
static int fsetpos_l(FILE *f, const fpos_t *pos){
	return fseek(f, *pos, SEEK_SET);
}

/* 9space: under the stream's lock (iolib.h) */
int
fsetpos(FILE *f, const fpos_t *pos)
{
	int r;

	_IO_lock(f);
	r = fsetpos_l(f, pos);
	_IO_unlock(f);
	return r;
}
