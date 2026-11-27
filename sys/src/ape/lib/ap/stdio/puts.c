/*
 * pANS stdio -- puts
 */
#include "iolib.h"
static int puts_l(const char *s){
	fputs(s, stdout);
	putchar_unlocked('\n');
	return ferror(stdin)?EOF:0;
}

/* 9space: under the stream's lock (iolib.h) */
int
puts(const char *s)
{
	int r;

	_IO_lock(stdout);
	r = puts_l(s);
	_IO_unlock(stdout);
	return r;
}
