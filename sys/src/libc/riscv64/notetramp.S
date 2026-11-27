/*
 * 9space: notify(fn) is the system call (sys/src/build/step/gen-9syscall.sh
 * names the stub for sys.h's NOTIFY _notify): fn is up->notify, per
 * process, as in 9front.
 *
 * sys/src/9/riscv64/trap.c's notify() enters the handler both ways: jc's
 * (nureg in R8 (s0) and at 8(sp), the message's address at 16(sp), 0(sp)
 * a fake return pc) and the psABI's (a0, a1, the stack 16-byte aligned).
 * Before, libc installed a trampoline that called the handler kept in one
 * variable, shared by the processes of a program that share memory
 * (libthread's): one's notify(nil) and the others' next note went to pc 0.
 */

.text
.globl	notify
notify:
	tail	_notify
