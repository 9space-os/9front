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
		/* 9space: the child's threads and stdio's locks anew (gnu/pthread.c, stdio/flockfile.c; pthreads are gcc's only) */
#ifdef __GNUC_MINOR__
		extern void __pthread_forkchild(void) __attribute__((weak));
#endif
		extern void _IO_forkchild(void);
		extern void _winchfork(void);

		_detachbuf();
		_sessleader = 0;
#ifdef __GNUC_MINOR__
		if(__pthread_forkchild)
			__pthread_forkchild();
#endif
		_IO_forkchild();
		_winchfork();	/* 9space: its own SIGWINCH registration (signal.c) */
	}
	if(n > 0){	/* GCC port: SIGCHLD (gnu/sigchld.c) */
		extern void __sigchld_forked(void);

		__sigchld_forked();
	}
	return n;
}
