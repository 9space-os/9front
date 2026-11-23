#ifndef __GNUC_MINOR__	/* GCC port: gcc's APE has them in ../gnu */
/*
 * GCC port: the hooks this directory calls into ../gnu (eventfd and
 * timerfd: epoll.c; MSG_NOSIGNAL: nosigpipe.c; SIGCHLD: sigchld.c;
 * memfd: mman.c), which only gcc builds (../mkfile's DIRS has no gnu).
 * Under kencc there are none of these descriptors, no SIGCHLD watcher and
 * no MSG_NOSIGNAL: each hook says so, and the caller goes on as 9front's
 * APE does (as _buf.c stands for ../gnu/buf.c).
 */
#include "lib.h"
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include "sys9.h"

int
__eventfdpeer(int fd)
{
	return -1;
}

void
__timerfdtake(int fd, uint64_t *v)
{
}

int
__nosigpipe(void)
{
	return 0;
}

int
__nosigpipe_begin(void)
{
	return -1;
}

long
__nosigpipe_end(int slot, long n)
{
	return n;
}

void
__sigchld_mask(long mask)
{
}

void
__sigchld_exec(void)
{
}

void
__sigchld_forked(void)
{
}

int
__sigchld_watching(void)
{
	return 0;
}

Waitmsg*
__sigchld_take(int pid, int nohang)
{
	return 0;
}

int
__shmseals(int fd, int cmd, int arg)
{
	errno = EINVAL;
	return -1;
}

int
__shmtruncate(int fd, off_t length)
{
	return -2;	/* not a shared memory object */
}
#endif
