/*
 * pANS stdio -- fputc
 */
#include "iolib.h"
static int fputc_l(int c, FILE *f){
	return putc_unlocked(c, f);	/* This can be made more fair to _IOLBF-mode streams */
}

/* 9space: under the stream's lock (iolib.h) */
int
fputc(int c, FILE *f)
{
	int r;

	_IO_lock(f);
	r = fputc_l(c, f);
	_IO_unlock(f);
	return r;
}
