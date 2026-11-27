#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#ifdef __GNUC_MINOR__
/* GCC port: in a native program with APE in it (Rust's std): its other procs
 * go, this one ends with a trap and stays Broken (its stack for acid) */
extern _Noreturn void __plan9_abort(void) __attribute__((weak));
/* 9space: a C program's other pthreads go too (gnu/pthread.c, if linked;
 * gcc's only: kencc's programs have none) */
extern void __pthread_killothers(void) __attribute__((weak));
#endif

_Noreturn void
abort(void)
{
#ifdef __GNUC_MINOR__
	if(__plan9_abort)
		__plan9_abort();
	if(__pthread_killothers)
		__pthread_killothers();
#endif
	kill(getpid(), SIGABRT);
}
