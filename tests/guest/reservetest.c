/*
 * proc(3)'s nokill against noswap, run inside 9front (tests/guest/run.sh):
 * a child that eats memory a megabyte at a time, protected first with
 * nokill, then with noswap.  nokill: the pager does not kill it, and it
 * waits for pages at the pager's high water mark like any process (free
 * pages never well below h).  noswap: it may take the pages below h too
 * (9front's meaning, for the servers that free memory).  The second case
 * is what a protected driver or wsys did before nokill.  Before either,
 * every process of the host owner is made noswap, so that the pager
 * kills none of the test's own (all protected: it waits instead, and
 * says so on the console).
 *
 * Output: "ok name" or "FAIL name: detail" per check, then
 * "reservetest: N ok, M failed".
 */
#include <u.h>
#include <libc.h>

enum {
	Margin = 256,		/* pages: what the protected others may take meanwhile */
	Stall = 30,		/* tenths of a second without growth: the child waits */
	Limit = 1200,		/* tenths: at most */
};

static int nok, nfail;

static void
check(char *name, int ok, char *fmt, ...)
{
	char buf[256];
	va_list arg;

	if(ok){
		nok++;
		print("ok %s\n", name);
		return;
	}
	nfail++;
	buf[0] = 0;
	if(fmt != nil){
		va_start(arg, fmt);
		vsnprint(buf, sizeof buf, fmt, arg);
		va_end(arg);
	}
	print("FAIL %s: %s\n", name, buf);
}

static char buf[1024];

/* swap(3)'s free line: f/h/r */
static int
memfree(long *f, long *h)
{
	char *p;
	int fd, n;

	if((fd = open("/dev/swap", OREAD)) < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	if((p = strstr(buf, " free\n")) == nil)
		return -1;
	while(p > buf && p[-1] != '\n')
		p--;
	*f = strtol(p, &p, 10);
	if(*p++ != '/')
		return -1;
	*h = strtol(p, &p, 10);
	return 0;
}

static char prot[16];	/* the 13th field, read by kb */

/* a process's memory in KB (status' 10th field), -1 when it is gone */
static long
kb(int pid)
{
	char path[32], *f[14];
	int fd, n;

	snprint(path, sizeof path, "/proc/%d/status", pid);
	if((fd = open(path, OREAD)) < 0)
		return -1;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return -1;
	buf[n] = 0;
	n = tokenize(buf, f, nelem(f));
	if(n < 10)
		return -1;
	if(n >= 13)
		strecpy(prot, prot+sizeof prot, f[12]);
	return atol(f[9]);
}

static int
ctl(int pid, char *msg)
{
	char path[32];
	int fd, r;

	snprint(path, sizeof path, "/proc/%d/ctl", pid);
	if((fd = open(path, OWRITE)) < 0)
		return -1;
	r = write(fd, msg, strlen(msg));
	close(fd);
	return r;
}

/* every process of the host owner noswap, ours too */
static void
protectall(void)
{
	Dir *d;
	int fd, i, n;

	if((fd = open("/proc", OREAD)) < 0)
		return;
	while((n = dirread(fd, &d)) > 0){
		for(i = 0; i < n; i++)
			if(strcmp(d[i].uid, getuser()) == 0)
				ctl(atoi(d[i].name), "noswap");
		free(d);
	}
	close(fd);
}

/* the child, after exec (noswap gone): protect, then eat */
static void
eat(char *flag)
{
	char *b;

	if(ctl(getpid(), flag) < 0)
		sysfatal("%s: %r", flag);
	for(;;){
		if((b = malloc(1<<20)) == nil)
			sleep(1000);
		else
			memset(b, 1, 1<<20);	/* the pages taken now */
	}
}

/* a child protected by flag eats memory until it waits; the lowest free count */
static void
run(char *flag, long *minfree, long *h, int *alive, long *eaten, char *seen)
{
	int pid, i, still;
	long f, k, last;

	switch(pid = rfork(RFPROC|RFFDG)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		execl("/tests/reservetest", "reservetest", "-eat", flag, nil);
		sysfatal("exec: %r");
	}
	*minfree = -1;
	last = -1;
	still = 0;
	for(i = 0; i < Limit && still < Stall; i++){
		sleep(100);
		if(memfree(&f, h) == 0 && (*minfree < 0 || f < *minfree))
			*minfree = f;
		if((k = kb(pid)) < 0)
			break;
		if(i == 5)
			strecpy(seen, seen+16, prot);	/* half a second in: its own flag set */
		if(k == last)
			still++;
		else
			still = 0;
		last = k;
	}
	*alive = kb(pid) >= 0;
	*eaten = last;
	ctl(pid, "kill");
	while(waitpid() != pid)
		;
	sleep(2000);	/* the pager sees memory back */
}

static int
kmesgsays(char *s)
{
	int fd, n;
	char *k;

	if((fd = open("/dev/kmesg", OREAD)) < 0)
		return 0;
	k = malloc(64*1024);
	n = readn(fd, k, 64*1024-1);
	close(fd);
	if(n < 0)
		n = 0;
	k[n] = 0;
	n = strstr(k, s) != nil;
	free(k);
	return n;
}

void
main(int argc, char **argv)
{
	long minfree, h, eaten;
	int alive;
	char seen[16];

	if(argc == 3 && strcmp(argv[1], "-eat") == 0)
		eat(argv[2]);
	protectall();

	run("nokill", &minfree, &h, &alive, &eaten, seen);
	print("reservetest: nokill: %ld KB taken, free pages at least %ld (high water %ld)\n", eaten, minfree, h);
	check("nokill: its status says nokill", strcmp(seen, "nokill") == 0, "status: '%s'", seen);
	check("nokill: the pager does not kill it", alive, "it was killed");
	check("nokill: it waits at the high water mark, the reserve stays", minfree >= h - Margin,
		"free went down to %ld, h %ld", minfree, h);
	check("nokill: the pager says only protected processes are left",
		kmesgsays("only protected processes left"), "no such line in kmesg");

	run("noswap", &minfree, &h, &alive, &eaten, seen);
	print("reservetest: noswap: %ld KB taken, free pages at least %ld (high water %ld)\n", eaten, minfree, h);
	check("noswap: its status says noswap", strcmp(seen, "noswap") == 0, "status: '%s'", seen);
	check("noswap: the pager does not kill it", alive, "it was killed");
	check("noswap: it takes the pages below the high water mark (9front's meaning)", minfree < h - Margin,
		"free stayed at %ld, h %ld", minfree, h);

	print("reservetest: %d ok, %d failed\n", nok, nfail);
	exits(nfail ? "failed" : nil);
}
