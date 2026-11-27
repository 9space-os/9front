#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "lib.h"
#include "sys9.h"

#include <stdio.h>

ssize_t
read(int d, void *buf, size_t nbytes)
{
	int n, noblock, isbuf;
	Fdinfo *f;

	if(d<0 || d>=OPEN_MAX || !(_fdinfo[d].flags & FD_ISOPEN)){
		errno = EBADF;
		return -1;
	}
	if(nbytes <= 0)
		return 0;
	if((_fdinfo[d].flags&FD_TIMERFD) && nbytes < 8){	/* GCC port: as Linux */
		errno = EINVAL;
		return -1;
	}
	if(buf == 0){
		errno = EFAULT;
		return -1;
	}
	f = &_fdinfo[d];
	noblock = f->oflags&O_NONBLOCK;
	isbuf = f->flags&(FD_BUFFERED|FD_BUFFEREDX);
	if(noblock || isbuf){
		if(f->flags&FD_BUFFEREDX) {
			errno = EIO;
			return -1;
		}
		if(!isbuf) {
			if(_startbuf(d) != 0) {
				errno = EIO;
				return -1;
			}
		}
		n = _readbuf(d, buf, nbytes, noblock);
	}else{
		n = _READ(d, buf, nbytes);
		if(n < 0)
			_syserrno();
	}
	if(n == 8 && (f->flags&FD_TIMERFD)){
		extern void __timerfdtake(int, unsigned long long*);

		__timerfdtake(d, buf);	/* the record: the count of expirations */
	}
	return n;
}

/* GCC port: fd is a timerfd (gnu/epoll.c) */
void
__timerfdmark(int fd)
{
	if(fd >= 0 && fd < OPEN_MAX)
		_fdinfo[fd].flags |= FD_TIMERFD;
}

/*
 * GCC port: n bytes of d that are there or coming, blocking even on a
 * descriptor with O_NONBLOCK (gnu/epoll.c: a timerfd's record)
 */
int
__readblock(int d, void *buf, int n)
{
	Fdinfo *f;

	f = &_fdinfo[d];
	if(f->flags&FD_BUFFERED)
		return _readbuf(d, buf, n, 0);
	n = _READ(d, buf, n);
	if(n < 0)
		_syserrno();
	return n;
}
