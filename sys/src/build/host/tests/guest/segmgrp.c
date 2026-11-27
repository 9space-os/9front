/*
 * segmgrp: the processes of one memory image (rfork RFMEM) share the
 * segments attached and detached after the fork (9front-ng, segattach(2)):
 * a segment the child attaches is there in the parent, at the same
 * address; a segment the parent detaches is gone in the child (it can
 * attach another at that address); a third process forked from the child
 * sees the segments both attached before and after; enough segments that
 * the tables grow; a process forked without RFMEM keeps its own image
 * (what the others attach later is not in it); one member dying of a
 * trap ends the others (it stays Broken), a process outside is left alone.
 * Prints "ok ..." / "FAIL ..." lines and a summary.
 */
#include <u.h>
#include <libc.h>

static int nfail;

static void
ok(char *fmt, ...)
{
	va_list arg;
	char buf[256];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("ok %s\n", buf);
}

static void
fail(char *fmt, ...)
{
	va_list arg;
	char buf[256];

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("FAIL %s\n", buf);
	nfail++;
}

enum {
	Nmany = 40,		/* more than a table's first NSEG slots */
	Len = 2*4096,
};

/* the shared state lives in the data segment, which RFMEM shares */
static uintptr childva;
static uintptr many[Nmany];
static uintptr thirdsaw;
static int childok;

static void
child(void)
{
	uintptr va;

	/* 1: a segment attached after the fork */
	va = (uintptr)segattach(0, "shared", nil, Len);
	if(va == (uintptr)-1){
		childva = 0;
		rendezvous(&childva, 0);
		_exits("segattach");
	}
	*(ulong*)va = 0x5e6a7712;
	childva = va;
	rendezvous(&childva, 0);

	/* 2: the parent detached it: its address is free here too */
	rendezvous(&childok, 0);
	va = (uintptr)segattach(0, "shared", (void*)childva, Len);
	childok = va == childva;
	if(va != (uintptr)-1)
		segdetach((void*)va);
	rendezvous(&childok, 0);
	_exits(nil);
}

static void
third(void)
{
	int i;

	/* sees what the parent attached before it was made */
	thirdsaw = 0;
	for(i = 0; i < Nmany; i++)
		if(many[i] != 0 && *(ulong*)many[i] == i)
			thirdsaw++;
	rendezvous(&thirdsaw, 0);
	/* and what the parent attaches after */
	rendezvous(&thirdsaw, 0);
	if(*(ulong*)childva == 0x7e57)
		thirdsaw = 1;
	else
		thirdsaw = 0;
	rendezvous(&thirdsaw, 0);
	_exits(nil);
}

/* a proc's status file: its state ("Broken", ...), or nil when it is gone */
static char*
procstate(int pid, char *buf, int n)
{
	char name[32];
	int fd, m;

	snprint(name, sizeof name, "/proc/%d/status", pid);
	if((fd = open(name, OREAD)) < 0)
		return nil;
	m = read(fd, buf, n-1);
	close(fd);
	if(m < 0)
		return nil;
	buf[m] = 0;
	return buf;
}

static void
procctl(int pid, char *msg)
{
	char name[32];
	int fd;

	snprint(name, sizeof name, "/proc/%d/ctl", pid);
	if((fd = open(name, OWRITE)) >= 0){
		write(fd, msg, strlen(msg));
		close(fd);
	}
}

/* the alarm ends a wait that would hang (the old kernel), not us */
static int
alarmed(void*, char *msg)
{
	return strcmp(msg, "alarm") == 0;
}

/*
 * a helper outside our memory image makes one: a member that sleeps and
 * one that faults; the fault ends the helper and the sleeper, the
 * faulting one stays Broken
 */
static void
trapends(void)
{
	int p[2], pids[2], h, i;
	char buf[256], *st;
	Waitmsg *w;

	if(pipe(p) < 0){
		fail("pipe: %r");
		return;
	}
	switch(h = rfork(RFPROC|RFFDG)){
	case -1:
		fail("rfork: %r");
		return;
	case 0:
		close(p[0]);
		switch(pids[0] = rfork(RFPROC|RFMEM)){
		case 0:
			for(;;)
				sleep(1000);
		}
		switch(pids[1] = rfork(RFPROC|RFMEM)){
		case 0:
			sleep(200);
			*(volatile int*)0 = 0;	/* the trap */
			_exits("no trap");
		}
		write(p[1], pids, sizeof pids);
		for(;;)
			sleep(1000);
	}
	close(p[1]);
	if(readn(p[0], pids, sizeof pids) != sizeof pids){
		fail("the helper's pids: %r");
		close(p[0]);
		return;
	}
	close(p[0]);
	atnotify(alarmed, 1);
	alarm(20000);
	while((w = wait()) != nil && w->pid != h)
		free(w);
	alarm(0);
	atnotify(alarmed, 0);
	if(w != nil && strstr(w->msg, "Killed") != nil)
		ok("a member's trap ended the others of its image (%s)", w->msg);
	else
		fail("the helper after its member's trap: %s", w != nil ? w->msg : "no wait (still running)");
	free(w);
	for(i = 0; i < 40 && procstate(pids[0], buf, sizeof buf) != nil; i++)
		sleep(50);
	if((st = procstate(pids[0], buf, sizeof buf)) == nil)
		ok("the sleeping member is gone");
	else
		fail("the sleeping member is still there: %.40s", st);
	if((st = procstate(pids[1], buf, sizeof buf)) != nil && strstr(st, "Broken") != nil)
		ok("the member with the trap stays Broken for the debugger");
	else
		fail("the member with the trap: %.40s", st != nil ? st : "gone");
	procctl(h, "kill");		/* if it was not ended */
	procctl(pids[0], "kill");
	procctl(pids[1], "kill");	/* Broken: gone now */
}

void
main(void)
{
	int i, n, pid;
	uintptr va;
	Waitmsg *w;

	/* child attaches, parent sees it */
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		child();
	}
	rendezvous(&childva, 0);
	if(childva == 0)
		fail("child's segattach: failed");
	else if(*(ulong*)childva == 0x5e6a7712)
		ok("a segment the child attached is in the parent at %#p", childva);
	else
		fail("child's segment at %#p reads %#lux", childva, *(ulong*)childva);

	/* parent detaches, it is gone in the child */
	if(childva != 0 && segdetach((void*)childva) < 0)
		fail("segdetach: %r");
	rendezvous(&childok, 0);
	rendezvous(&childok, 0);
	if(childok)
		ok("a segment the parent detached is gone in the child");
	else
		fail("the child could not attach at the address the parent freed");
	while((w = wait()) != nil && w->pid != pid)
		free(w);
	if(w != nil && w->msg[0] != 0)
		fail("child: %s", w->msg);
	free(w);

	/* many segments: the tables grow alike */
	n = 0;
	for(i = 0; i < Nmany; i++){
		va = (uintptr)segattach(0, "shared", nil, 4096);
		if(va == (uintptr)-1){
			fail("segattach %d: %r", i);
			break;
		}
		many[i] = va;
		*(ulong*)va = i;
		n++;
	}
	if(n == Nmany)
		ok("%d segments attached", n);

	/* a third process sees them, and one attached after it was made */
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		third();
	}
	rendezvous(&thirdsaw, 0);
	if(thirdsaw == Nmany)
		ok("a process forked later sees all %d", Nmany);
	else
		fail("a process forked later sees %lud of %d", (ulong)thirdsaw, Nmany);
	va = (uintptr)segattach(0, "shared", nil, Len);
	if(va == (uintptr)-1)
		fail("segattach: %r");
	else
		*(ulong*)va = 0x7e57;
	childva = va;
	rendezvous(&thirdsaw, 0);
	rendezvous(&thirdsaw, 0);
	if(thirdsaw == 1)
		ok("a segment attached after the fork is in the other process");
	else
		fail("the other process does not see a segment attached after the fork");
	while((w = wait()) != nil && w->pid != pid)
		free(w);
	free(w);

	/* without RFMEM: an image of its own */
	segdetach((void*)childva);
	switch(pid = rfork(RFPROC|RFFDG)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		/* the parent's later segment is not here: its address is free */
		rendezvous(&nfail, 0);
		va = (uintptr)segattach(0, "shared", (void*)childva, Len);
		if(va == childva)
			_exits(nil);
		_exits("address taken");
	}
	va = (uintptr)segattach(0, "shared", (void*)childva, Len);	/* again, after the fork */
	rendezvous(&nfail, 0);
	while((w = wait()) != nil && w->pid != pid)
		free(w);
	if(w != nil && w->msg[0] == 0 && va == childva)
		ok("a process forked without RFMEM keeps its own image");
	else
		fail("forked without RFMEM: %s", w != nil ? w->msg : "no wait");
	free(w);

	for(i = 0; i < Nmany; i++)
		if(many[i] != 0)
			segdetach((void*)many[i]);
	if(va != (uintptr)-1)
		segdetach((void*)va);

	trapends();
	print("segmgrp: %d failed\n", nfail);
	exits(nfail ? "failed" : nil);
}
