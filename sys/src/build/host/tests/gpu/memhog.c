/*
 * SPDX-License-Identifier: MIT
 *
 * memhog [-s] [-m max] [-d ms] kb: takes memory without bound in children of
 * kb KB each (forked one after another, ms apart, default 10; each
 * touches its pages, then sleeps),
 * until the pager kills one (devswap.c killbig: the biggest process
 * that may be killed).  Each child is small on purpose: a GPU driver
 * bigger than one of them is the pager's choice unless it is noswap
 * (gpu(4)).  When a child is gone, the rest are killed and memhog
 * prints "memhog: N children of K KB, child P gone" (sys/src/build/host/tests/gpu
 * drvnoswap.rc reads the kernel's message for who it was).  It gives up
 * after max children (default 4096) with "memhog: no child killed".
 * -s: in this process itself, kb KB at a time, ms apart, until the pager
 * kills it (a window's program the pager kills: sys/src/build/host/tests/gpu wsyssup.rc).
 */
#include <u.h>
#include <libc.h>

enum { Max = 4096 };

static int pids[Max];
static int n;

static int
gone(int pid)
{
	char path[32];

	snprint(path, sizeof path, "/proc/%d/status", pid);
	return access(path, AEXIST) < 0;
}

static void
killall(void)
{
	char path[32];
	int i, fd;

	for(i = 0; i < n; i++){
		snprint(path, sizeof path, "/proc/%d/ctl", pids[i]);
		if((fd = open(path, OWRITE)) >= 0){
			write(fd, "kill", 4);
			close(fd);
		}
	}
	while(waitpid() > 0)
		;
}

void
main(int argc, char **argv)
{
	long kb;
	int i, max, pid, ms, self;
	char *b;

	max = Max;
	ms = 10;
	self = 0;
	ARGBEGIN{
	case 's':
		self = 1;
		break;
	case 'd':
		ms = atoi(EARGF(sysfatal("usage")));
		break;
	case 'm':
		max = atoi(EARGF(sysfatal("usage")));
		if(max < 1 || max > Max)
			sysfatal("usage");
		break;
	}ARGEND
	if(argc != 1 || (kb = atol(argv[0])) <= 0)
		sysfatal("usage: memhog [-s] [-m max] [-d ms] kb");
	if(self){
		for(;;){
			if((b = malloc(kb*1024)) == nil)
				exits("malloc");
			memset(b, 1, kb*1024);	/* the pages taken now */
			sleep(ms);
		}
	}
	for(n = 0; n < max;){
		switch(pid = rfork(RFPROC|RFFDG)){
		case -1:
			sysfatal("rfork: %r");
		case 0:
			if((b = malloc(kb*1024)) == nil)
				exits("malloc");
			memset(b, 1, kb*1024);	/* the pages taken now */
			for(;;)
				sleep(100000);
		}
		pids[n++] = pid;
		sleep(ms);
		for(i = 0; i < n; i++)
			if(gone(pids[i])){
				print("memhog: %d children of %ld KB, child %d gone\n", n, kb, pids[i]);
				killall();
				exits(nil);
			}
	}
	print("memhog: no child killed (%d children of %ld KB)\n", n, kb);
	killall();
	exits("none killed");
}
