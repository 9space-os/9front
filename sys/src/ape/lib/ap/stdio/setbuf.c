/*
 * pANS stdio -- setbuf
 */
#include "iolib.h"
static void setbuf_l(FILE *f, char *buf){
	if(f->state==OPEN){
		if(buf)
			f->bufl=BUFSIZ;
		else{
			buf=f->unbuf;
			f->bufl=0;
		}
		f->rp=f->wp=f->lp=f->buf=buf;
		f->state=RDWR;
	}
	/* else error, but there's no way to report it */
}

/* 9space: under the stream's lock (iolib.h) */
void
setbuf(FILE *f, char *buf)
{
	_IO_lock(f);
	setbuf_l(f, buf);
	_IO_unlock(f);
}
