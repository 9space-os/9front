/*
 * pANS stdio -- rewind
 */
#include "iolib.h"
static void rewind_l(FILE *f){
	fseek(f, 0L, SEEK_SET);
}

/* 9space: under the stream's lock (iolib.h) */
void
rewind(FILE *f)
{
	_IO_lock(f);
	rewind_l(f);
	_IO_unlock(f);
}
