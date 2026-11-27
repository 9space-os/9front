/*
 * pANS stdio -- freopen
 */
#include "iolib.h"
/*
 * Open the named file with the given mode, using the given FILE
 * Legal modes are given below, `additional characters may follow these sequences':
 * r rb		open to read
 * w wb		open to write, truncating
 * a ab		open to write positioned at eof, creating if non-existant
 * r+ r+b rb+	open to read and write, creating if non-existant
 * w+ w+b wb+	open to read and write, truncating
 * a+ a+b ab+	open to read and write, positioned at eof, creating if non-existant.
 */
static FILE *freopen_l(const char *name, const char *mode, FILE *f){
	if(f->state!=CLOSED){
		fclose(f);
/* premature; fall through and see what happens */
/*		f->state=OPEN; */
	}
	return _IO_reopen(name, mode, f);
}

/*
 * 9front-ng: freopen's opening, for fopen too: f is a closed stream or
 * one _IO_newfile reserved (fclose would give it to another thread);
 * when the open fails f is closed.
 */
FILE *_IO_reopen(const char *name, const char *mode, FILE *f){
	int m;

	f->fd=-1;
	m = *mode++;
	if(m == 0)
		goto Fail;
	if(*mode == 'b')
		mode++;
	switch(m){
	default:
		goto Fail;
	case 'r':
		f->fd=open(name, (*mode == '+'? O_RDWR: O_RDONLY));
		break;
	case 'w':
		f->fd=creat(name, 0666);	/* implicitly O_WRONLY */
		/* for O_RDWR, have to creat, close, open */
		if(*mode == '+' && f->fd >= 0) {
			close(f->fd);
			f->fd=open(name, O_RDWR);
		}
		break;
	case 'a':
		f->fd=open(name, (*mode == '+'? O_RDWR: O_WRONLY));
		if(f->fd<0) {
			f->fd=creat(name, 0666);
			/* for O_RDWR, have to creat, close, open */
			if(*mode == '+' && f->fd >= 0) {
				close(f->fd);
				f->fd=open(name, O_RDWR);
			}
		}
		lseek(f->fd, 0L, 2);
		break;
	}

	if(f->fd==-1){
	Fail:
		f->fd=-1;
		f->flags=0;
		f->state=CLOSED;
		return NULL;
	}
	f->flags=(mode[0]=='a')? APPEND : 0;
	f->state=OPEN;
	f->buf=0;
	f->rp=0;
	f->wp=0;
	f->lp=0;
	return f;
}

/* 9space: under the stream's lock (iolib.h) */
FILE *
freopen(const char *name, const char *mode, FILE *f)
{
	FILE * r;

	_IO_lock(f);
	r = freopen_l(name, mode, f);
	_IO_unlock(f);
	return r;
}
