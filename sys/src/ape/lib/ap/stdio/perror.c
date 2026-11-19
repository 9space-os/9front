/*
 * pANS stdio -- perror
 */
#include "iolib.h"
#include <errno.h>

static void perror_l(const char *s){
	if(s!=NULL && *s != '\0') fputs(s, stderr), fputs(": ", stderr);
	fputs(strerror(errno), stderr);
	putc_unlocked('\n', stderr);
	fflush(stderr);
}

/* 9space: under the stream's lock (iolib.h) */
void
perror(const char *s)
{
	_IO_lock(stderr);
	perror_l(s);
	_IO_unlock(stderr);
}
