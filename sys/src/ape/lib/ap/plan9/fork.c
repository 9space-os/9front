#include "lib.h"
#include <errno.h>
#include <unistd.h>
#include "sys9.h"

pid_t
fork(void)
{
	int n;

	n = _RFORK(RFENVG|RFFDG|RFPROC);
	if(n < 0)
		_syserrno();
	if(n == 0) {
		/* 9space: the child's threads and stdio's locks anew (gnu/pthread.c, stdio/flockfile.c) */
		extern void __pthread_forkchild(void) __attribute__((weak));
		extern void _IO_forkchild(void);

		_detachbuf();
		_sessleader = 0;
		if(__pthread_forkchild)
			__pthread_forkchild();
		_IO_forkchild();
	}
	if(n > 0){	/* GCC port: SIGCHLD (gnu/sigchld.c) */
		extern void __sigchld_forked(void);

		__sigchld_forked();
	}
	return n;
}
