/*
 * clock_gettime, clock_getres (POSIX), timespec_get (C11), usleep,
 * sched_yield, which APE lacks.
 *
 * The clocks are 9front's /dev/bintime (cons(3), port/tod.c): big-endian
 * nanoseconds since the epoch, fastticks, their frequency, and the
 * nanoseconds since boot, which never go back (CLOCK_MONOTONIC; the
 * epoch time moves when the time is set; never slewed, so it is
 * CLOCK_MONOTONIC_RAW as well). The file stays open, shared by
 * all procs of the program; pread, so they need no lock.
 */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>

extern unsigned long __fdgen(int);	/* plan9/close.c: one more at each close of the descriptor */

/*
 * the descriptor and its generation: one the program closed (daemons
 * close all) and got again for another file of 32 bytes or more gave
 * that file's bytes as the time; now it is opened anew then
 */
static uint64_t bintime = ~(uint64_t)0;

#define BTFD(v)	((int)(uint32_t)(v))
#define BTGEN(v)	((uint32_t)((v) >> 32))

static int
openbintime(uint64_t old)
{
	int fd;
	uint64_t v;

	fd = open("/dev/bintime", O_RDONLY);
	if(fd < 0)
		return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	v = (uint64_t)(uint32_t)__fdgen(fd) << 32 | (uint32_t)fd;
	if(!__atomic_compare_exchange_n(&bintime, &old, v, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)){
		close(fd);	/* another proc was first */
		if(BTGEN(old) == (uint32_t)__fdgen(BTFD(old)))
			return BTFD(old);
		return -1;
	}
	return fd;
}

/* v[0] epoch ns, v[1] ticks, v[2] ticks per second, v[3] ns since boot */
static int
readbintime(uint64_t v[4])
{
	unsigned char b[32];
	int fd, i, j, try;

	uint64_t bt;

	for(try = 0; try < 2; try++){
		bt = __atomic_load_n(&bintime, __ATOMIC_SEQ_CST);
		fd = BTFD(bt);
		if(bt == ~(uint64_t)0 || BTGEN(bt) != (uint32_t)__fdgen(fd))
			fd = openbintime(bt);
		if(fd < 0)
			return -1;
		if(pread(fd, b, sizeof b, 0) == sizeof b){
			for(i = 0; i < 4; i++){
				v[i] = 0;
				for(j = 0; j < 8; j++)
					v[i] = v[i] << 8 | b[8*i + j];
			}
			return 0;
		}
		/* not readable as it was: once more, opened anew */
		__atomic_compare_exchange_n(&bintime, &bt, ~(uint64_t)0, 0,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	}
	return -1;
}

static void
nstots(uint64_t ns, struct timespec *ts)
{
	ts->tv_sec = ns / 1000000000;
	ts->tv_nsec = ns % 1000000000;
}

int
clock_gettime(clockid_t id, struct timespec *ts)
{
	uint64_t v[4];
	struct tms t;
	long hz;

	switch(id){
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		if(readbintime(v) < 0)
			return -1;
		nstots(id == CLOCK_REALTIME ? v[0] : v[3], ts);
		return 0;
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		/* this proc's (a thread is a proc) user and system time */
		times(&t);
		hz = sysconf(_SC_CLK_TCK);
		nstots((uint64_t)(t.tms_utime + t.tms_stime) * (1000000000 / hz), ts);
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int
clock_getres(clockid_t id, struct timespec *ts)
{
	uint64_t v[4];

	switch(id){
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		if(ts == NULL)
			return 0;
		/* a tick of the counter they come from, at least 1ns */
		if(readbintime(v) < 0)
			return -1;
		nstots(v[2] >= 1000000000 || v[2] == 0 ? 1 : (1000000000 + v[2] - 1) / v[2], ts);
		return 0;
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		if(ts != NULL)
			nstots(1000000000 / sysconf(_SC_CLK_TCK), ts);
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int
timespec_get(struct timespec *ts, int base)
{
	if(base != TIME_UTC || clock_gettime(CLOCK_REALTIME, ts) < 0)
		return 0;
	return base;
}

int
usleep(useconds_t us)
{
	struct timespec ts;

	ts.tv_sec = us / 1000000;
	ts.tv_nsec = us % 1000000 * 1000;
	return nanosleep(&ts, NULL);
}

/* sleep(0): the kernel reschedules */
int
sched_yield(void)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);
	return 0;
}
