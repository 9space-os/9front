/*
 * GCC port: notify(fn) is the system call (scripts/gen-9syscall.sh names
 * the stub for sys.h's NOTIFY _notify): fn is up->notify, per process, as
 * in 9front.
 *
 * GCC port: sys/src/9/pc64/trap.c's notify() enters the handler both ways:
 * 6c's (the Ureg* in %rbp and at 8(%rsp), the message's address at
 * 16(%rsp), 0(%rsp) a fake return pc) and System V's (%rdi, %rsi, the
 * stack aligned as after a call).  Before, the kernel did only the first
 * and libc installed a trampoline that called the handler kept in one
 * variable: the processes of a program that share memory (libthread's,
 * rio's) shared it, one's notify(nil) and the others' next note jumped to
 * pc 0, the trampoline did not align the stack either.
 */

.text
.globl	notify
notify:
	jmp	_notify
