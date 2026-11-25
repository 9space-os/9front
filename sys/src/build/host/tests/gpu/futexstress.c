/* SPDX-License-Identifier: MIT */
/*
 * futexstress: Mesa's futex for 9front (sys/src/mesa/vendor/src/util/futex.c,
 * compiled in by build-vktest.sh: the tree's file, not a copy) under the
 * patterns Mesa's simple_mtx and util_queue_fence use.
 *
 *  wake1     a waiter on A, two on B, A and B in one bucket of futex.c's
 *            hash: each round A is changed and woken with futex_wake(A, 1)
 *            while B's waiters sleep on; the wake must reach A's waiter.
 *            futex.c before the waiter queues released one count on the
 *            bucket's semaphore, which a waiter of B could take (it saw B
 *            unchanged and slept again) while A's waiter slept on: the
 *            hang of browser/NOTES.md (2026-11-21).
 *  timeout   a wait with a deadline and no wake ends, ETIMEDOUT, not before
 *            the deadline; a wake afterwards still reaches a new waiter.
 *  mutex     a futex mutex (Drepper's, as simple_mtx), 8 threads, the count
 *            right and no hang.
 *
 * A round that does not finish in 2 s is a lost wakeup: FAIL, the rest
 * skipped (the threads stay asleep; exit ends them).
 * Output: "ok name" / "FAIL name: detail", then "futexstress: all passed".
 */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "util/futex.h"

enum { Rounds = 3000, Mthreads = 8, Mloops = 20000 };

static uint32_t words[4096];
static uint32_t *A, *B;
static _Atomic uint32_t ack;
static int failed;

static void
fail(const char *name, const char *detail)
{
	printf("FAIL %s: %s\n", name, detail);
	failed = 1;
}

/* futex.c's bucket, so that A and B share one; the test holds for any
 * hashing, the pair only makes the old defect certain */
static unsigned
bucket(uint32_t *addr)
{
	return (uint32_t)((uintptr_t)addr >> 2) * 2654435761u >> 24;
}

static int64_t
nowms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void
napms(long ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000 };

	nanosleep(&t, NULL);
}

static void *
waitA(void *v)
{
	uint32_t seen = 0, x;

	(void)v;
	for (;;) {
		x = atomic_load((_Atomic uint32_t *)A);
		if (x == seen) {
			futex_wait(A, (int32_t)x, NULL);
			continue;
		}
		seen = x;
		atomic_store(&ack, x);
		if (x == Rounds)
			return NULL;
	}
}

static void *
waitB(void *v)
{
	(void)v;
	while (atomic_load((_Atomic uint32_t *)B) == 0)
		futex_wait(B, 0, NULL);
	return NULL;
}

static int
wake1(void)
{
	pthread_t ta, tb[2];
	uint32_t r;
	int64_t t;
	char buf[96];
	int i;

	A = &words[0];
	for (i = 1; i < 4096 && bucket(&words[i]) != bucket(A); i++)
		;
	if (i == 4096) {
		fail("wake1", "no second address in A's bucket");
		return -1;
	}
	B = &words[i];
	pthread_create(&tb[0], NULL, waitB, NULL);
	pthread_create(&tb[1], NULL, waitB, NULL);
	pthread_create(&ta, NULL, waitA, NULL);
	for (r = 1; r <= Rounds; r++) {
		napms(1);	/* A's waiter asleep again, most rounds */
		atomic_store((_Atomic uint32_t *)A, r);
		futex_wake(A, 1);
		t = nowms();
		while (atomic_load(&ack) != r) {
			if (nowms() - t > 2000) {
				snprintf(buf, sizeof buf, "round %u of %d: A's waiter not woken in 2 s (lost wakeup)", r, Rounds);
				fail("wake1", buf);
				return -1;
			}
			napms(1);
		}
	}
	pthread_join(ta, NULL);
	atomic_store((_Atomic uint32_t *)B, 1);
	futex_wake(B, INT_MAX);
	pthread_join(tb[0], NULL);
	pthread_join(tb[1], NULL);
	printf("ok wake1 %d rounds, A and B in bucket %u\n", Rounds, bucket(A));
	return 0;
}

static uint32_t tw;
static _Atomic int tdone;

static void *
waitT(void *v)
{
	(void)v;
	while (atomic_load((_Atomic uint32_t *)&tw) == 0)
		futex_wait(&tw, 0, NULL);
	atomic_store(&tdone, 1);
	return NULL;
}

static int
timeout(void)
{
	struct timespec dl;
	pthread_t t;
	int64_t t0, el;
	int r, e;
	char buf[96];

	clock_gettime(CLOCK_MONOTONIC, &dl);
	dl.tv_nsec += 50 * 1000000;
	if (dl.tv_nsec >= 1000000000) {
		dl.tv_sec++;
		dl.tv_nsec -= 1000000000;
	}
	t0 = nowms();
	r = futex_wait(&tw, 0, &dl);
	e = errno;
	el = nowms() - t0;
	if (r != -1 || e != ETIMEDOUT || el < 45) {
		snprintf(buf, sizeof buf, "returned %d errno %d after %lld ms (want -1 ETIMEDOUT, >= 50 ms)", r, e, (long long)el);
		fail("timeout", buf);
		return -1;
	}
	pthread_create(&t, NULL, waitT, NULL);
	napms(20);
	atomic_store((_Atomic uint32_t *)&tw, 1);
	futex_wake(&tw, 1);
	t0 = nowms();
	while (atomic_load(&tdone) == 0) {
		if (nowms() - t0 > 2000) {
			fail("timeout", "the waiter after the timed-out wait not woken in 2 s");
			return -1;
		}
		napms(1);
	}
	pthread_join(t, NULL);
	printf("ok timeout after %lld ms, a later wake reaches a new waiter\n", (long long)el);
	return 0;
}

/* Drepper's futex mutex: 0 free, 1 locked, 2 locked with waiters (simple_mtx) */
static _Atomic uint32_t mtx;
static long count;
static _Atomic int mdone;

static void
mlock(void)
{
	uint32_t c = 0;

	if (atomic_compare_exchange_strong(&mtx, &c, 1))
		return;
	if (c != 2)
		c = atomic_exchange(&mtx, 2);
	while (c != 0) {
		futex_wait((uint32_t *)&mtx, 2, NULL);
		c = atomic_exchange(&mtx, 2);
	}
}

static void
munlock(void)
{
	if (atomic_fetch_sub(&mtx, 1) != 1) {
		atomic_store(&mtx, 0);
		futex_wake((uint32_t *)&mtx, 1);
	}
}

static void *
mthread(void *v)
{
	int i;

	(void)v;
	for (i = 0; i < Mloops; i++) {
		mlock();
		count++;
		munlock();
	}
	atomic_fetch_add(&mdone, 1);
	return NULL;
}

static int
mutex(void)
{
	pthread_t t[Mthreads];
	int64_t t0;
	char buf[96];
	int i;

	for (i = 0; i < Mthreads; i++)
		pthread_create(&t[i], NULL, mthread, NULL);
	t0 = nowms();
	while (atomic_load(&mdone) != Mthreads) {
		if (nowms() - t0 > 60000) {
			snprintf(buf, sizeof buf, "%d of %d threads done after 60 s (a lost wakeup)", atomic_load(&mdone), Mthreads);
			fail("mutex", buf);
			return -1;
		}
		napms(10);
	}
	for (i = 0; i < Mthreads; i++)
		pthread_join(t[i], NULL);
	if (count != (long)Mthreads * Mloops) {
		snprintf(buf, sizeof buf, "count %ld, want %ld", count, (long)Mthreads * Mloops);
		fail("mutex", buf);
		return -1;
	}
	printf("ok mutex %d threads x %d in %lld ms\n", Mthreads, Mloops, (long long)(nowms() - t0));
	return 0;
}

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (wake1() < 0 || timeout() < 0 || mutex() < 0) {
		printf("futexstress: failed\n");
		exit(1);
	}
	printf("futexstress: all passed\n");
	return 0;
}
