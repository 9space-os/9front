/*
 * pANS stdio -- gets
 */
#include "iolib.h"
static char *gets_l(char *as){
#ifdef secure
	stdin->flags|=ERR;
	return NULL;
#else
	char *s=as;
	int c;
	while((c=getc_unlocked(stdin))!='\n' && c!=EOF) *s++=c;
	if(c!=EOF || s!=as) *s='\0';
	else return NULL;
	return as;
#endif
}

/* 9space: under the stream's lock (iolib.h) */
char *
gets(char *as)
{
	char * r;

	_IO_lock(stdin);
	r = gets_l(as);
	_IO_unlock(stdin);
	return r;
}
