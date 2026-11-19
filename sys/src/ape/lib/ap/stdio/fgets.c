/*
 * pANS stdio -- fgets
 */
#include "iolib.h"
static char *fgets_l(char *as, int n, FILE *f){
	int c=0;
	char *s=as;
	while(n>1 && (c=getc_unlocked(f))!=EOF){
		*s++=c;
		--n;
		if(c=='\n') break;
	}
	if(c==EOF && s==as
	|| ferror(f)) return NULL;
	if(n) *s='\0';
	return as;
}

/* 9space: under the stream's lock (iolib.h) */
char *
fgets(char *as, int n, FILE *f)
{
	char * r;

	_IO_lock(f);
	r = fgets_l(as, n, f);
	_IO_unlock(f);
	return r;
}
