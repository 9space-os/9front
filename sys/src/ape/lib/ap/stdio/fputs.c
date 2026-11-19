/*
 * pANS stdio -- fputs
 */
#include "iolib.h"
static int fputs_l(const char *s, FILE *f){
	while(*s) putc_unlocked(*s++, f);
	return ferror(f)?EOF:0;
}

/* 9space: under the stream's lock (iolib.h) */
int
fputs(const char *s, FILE *f)
{
	int r;

	_IO_lock(f);
	r = fputs_l(s, f);
	_IO_unlock(f);
	return r;
}
