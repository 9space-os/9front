/*
 * 9front-ng: open_memstream (POSIX.1-2008): a string stream (sopenw)
 * whose buffer grows as it is written; fflush and fclose put it, with a
 * null after its contents, in *bufp and its length in *sizep.  After
 * fclose the buffer is the caller's (free).
 */
#include "iolib.h"
#include <string.h>
#define _PLAN9_SOURCE
#include <lock.h>

/* where each memstream's buffer and length go, by stream (FILEs are kept:
 * _IO_newfile; one past FOPEN_MAX comes from the heap) */
typedef struct Ms Ms;
struct Ms {
	Ms	*next;
	FILE	*f;
	char	**bufp;
	size_t	*sizep;
};
static Ms *mslist;
static Lock mslock;

/* f's, made when make is set; kept for the next memstream on f */
static Ms *msof(FILE *f, int make){
	Ms *m;

	lock(&mslock);
	for(m=mslist; m!=NULL; m=m->next)
		if(m->f==f)
			break;
	if(m==NULL && make && (m=malloc(sizeof *m))!=NULL){
		m->f=f;
		m->next=mslist;
		mslist=m;
	}
	unlock(&mslock);
	return m;
}

FILE *open_memstream(char **bufp, size_t *sizep){
	FILE *f;
	Ms *m;

	if(bufp==NULL || sizep==NULL)
		return NULL;
	if((f=_IO_sopenw())==NULL)
		return NULL;
	if((m=msof(f, 1))==NULL){
		f->flags=0;
		f->state=CLOSED;	/* nothing written yet: no buffer */
		return NULL;
	}
	f->flags|=MEMSTR;
	m->bufp=bufp;
	m->sizep=sizep;
	*bufp=NULL;
	*sizep=0;
	return f;
}

int _IO_memsync(FILE *f){
	char *t;
	size_t n;
	Ms *m;

	if(f->state==OPEN && _IO_setvbuf(f)!=0)
		return EOF;
	if(f->state==ERR || f->buf==NULL)
		return EOF;
	n=f->wp-f->buf;
	if(n>=f->bufl){	/* room for the null */
		if((t=realloc(f->buf, f->bufl+BUFSIZ))==NULL){
			f->state=ERR;
			return EOF;
		}
		f->buf=t;
		f->wp=t+n;
		f->bufl+=BUFSIZ;
		f->rp=t+f->bufl;
	}
	f->buf[n]='\0';
	if((m=msof(f, 0))==NULL)
		return EOF;
	*m->bufp=f->buf;
	*m->sizep=n;
	return 0;
}
