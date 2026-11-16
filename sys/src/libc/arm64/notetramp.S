/*
 * GCC port: notify(fn) is the system call (scripts/gen-9syscall.sh names
 * the stub for sys.h's NOTIFY _notify): fn is up->notify, per process, as
 * in 9front.
 *
 * nfront: sys/src/9/arm64/trap.c's notify() enters the handler both ways:
 * 7c's (nureg in R0 and at 8(SP), the message's address at 16(SP), 0(SP)
 * a fake return pc) and AAPCS's (x0, x1, the stack 16-byte aligned).
 * Before, libc installed a trampoline that called the handler kept in one
 * variable, shared by the processes of a program that share memory
 * (libthread's): one's notify(nil) and the others' next note went to pc 0.
 */

.text
.globl	notify
notify:
	b	_notify
