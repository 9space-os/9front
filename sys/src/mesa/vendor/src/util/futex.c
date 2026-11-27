/*
 * Copyright © 2015 Intel
 * Copyright © 2022 Yonggang Luo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/futex.h"

#if UTIL_FUTEX_SUPPORTED

#if defined(HAVE_LINUX_FUTEX_H)

#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

#ifndef SYS_futex
#define SYS_futex SYS_futex_time64
#endif

static inline long sys_futex(void *addr1, int op, int val1, const struct timespec *timeout, void *addr2, int val3)
{
   return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

int futex_wake(uint32_t *addr, int32_t count)
{
   return sys_futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   /* FUTEX_WAIT_BITSET with FUTEX_BITSET_MATCH_ANY is equivalent to
    * FUTEX_WAIT, except that it treats the timeout as absolute. */
   return sys_futex(addr, FUTEX_WAIT_BITSET, value, timeout, NULL,
                    FUTEX_BITSET_MATCH_ANY);
}

#elif defined(__FreeBSD__)

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/umtx.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   assert(count >= 0);
   return _umtx_op(addr, UMTX_OP_WAKE, (uint32_t)count, NULL, NULL) == -1 ? errno : 0;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   void *uaddr = NULL, *uaddr2 = NULL;
   struct _umtx_time tmo = {
      ._flags = UMTX_ABSTIME,
      ._clockid = CLOCK_MONOTONIC
   };

   assert(value == (int)(uint32_t)value); /* Check that bits weren't discarded */

   if (timeout != NULL) {
      tmo._timeout = *timeout;
      uaddr = (void *)(uintptr_t)sizeof(tmo);
      uaddr2 = (void *)&tmo;
   }

   return _umtx_op(addr, UMTX_OP_WAIT_UINT, (uint32_t)value, uaddr, uaddr2) == -1 ? errno : 0;
}

#elif defined(__OpenBSD__)

#include <sys/futex.h>
#include <sys/time.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   return futex(addr, FUTEX_WAKE, count, NULL, NULL);
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   struct timespec tsnow, tsrel;

   if (timeout == NULL)
      return futex(addr, FUTEX_WAIT, value, NULL, NULL);

   clock_gettime(CLOCK_MONOTONIC, &tsnow);
   if (timespeccmp(&tsnow, timeout, <))
      timespecsub(timeout, &tsnow, &tsrel);
   else
      timespecclear(&tsrel);
   return futex(addr, FUTEX_WAIT, value, &tsrel, NULL);
}

#elif defined(_WIN32) && !defined(WINDOWS_NO_FUTEX)

#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   /* All current callers fall into one of these buckets, and we'll get the semantics
    * wrong if someone tries to be more clever.
    */
   assert(count == 1 || count == INT32_MAX);
   if (count == 1)
      WakeByAddressSingle(addr);
   else
      WakeByAddressAll(addr);
   return count;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   DWORD timeout_ms = INFINITE;
   if (timeout != NULL) {
      struct timespec tsnow;
      timespec_get(&tsnow, TIME_MONOTONIC);

      timeout_ms = (timeout->tv_sec - tsnow.tv_sec) * 1000 +
                   (timeout->tv_nsec - tsnow.tv_nsec) / 1000000;
   }

   if (WaitOnAddress(addr, &value, sizeof(value), timeout_ms))
      return 0;
   return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : -1;
}

#elif defined(__plan9__)

/*
 * 9front: a queue of waiters per bucket of addresses, each waiter on a
 * semaphore of its own (semacquire(2)), woken by address, as the plan9
 * futex of Rust's std and the condition variables of APE's pthreads.
 * A waiter looks at the value and queues itself under the bucket's lock,
 * a waker changes the value before it takes the lock: the waker finds
 * the waiter, or the waiter the new value.  A waiter's node is on its
 * stack, which is shared memory (APE's and std's threads run on stacks
 * from malloc or segments every proc has); a woken waiter is off the
 * queue before its semaphore is released, and the waker does not touch
 * it after that.
 * A wake only reaches waiters of its address: with one semaphore per
 * bucket a wake could be taken by a waiter of another address in the
 * bucket, which found its value unchanged and waited again, while the
 * one it was meant for slept on (a lost wakeup).
 */
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>

extern int _SEMACQUIRE(int *, int);
extern int _TSEMACQUIRE(int *, unsigned long);
extern int _SEMRELEASE(int *, int);

struct futex_waiter {
   uint32_t *addr;
   struct futex_waiter *next;
   int sem;
};

static struct {
   int lock;
   struct futex_waiter *head;
} futex_buckets[256];

static unsigned
futex_bucket(uint32_t *addr)
{
   return (uint32_t)((uintptr_t)addr >> 2) * 2654435761u >> 24;
}

static void
futex_lock(int *l)
{
   while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
      sched_yield();
}

static void
futex_unlock(int *l)
{
   __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

/* off the bucket's queue; whether it was there (else a waker took it off
 * and its release is due) */
static int
futex_unqueue(unsigned b, struct futex_waiter *w)
{
   struct futex_waiter **l;

   for (l = &futex_buckets[b].head; *l != NULL; l = &(*l)->next) {
      if (*l == w) {
         *l = w->next;
         return 1;
      }
   }
   return 0;
}

int futex_wake(uint32_t *addr, int32_t count)
{
   unsigned b = futex_bucket(addr);
   struct futex_waiter **l, *w;
   int n = 0;

   futex_lock(&futex_buckets[b].lock);
   for (l = &futex_buckets[b].head; *l != NULL && n < count;) {
      w = *l;
      if (w->addr == addr) {
         *l = w->next;
         _SEMRELEASE(&w->sem, 1);   /* w may be gone after this */
         n++;
      } else
         l = &w->next;
   }
   futex_unlock(&futex_buckets[b].lock);
   return n;
}

static int64_t
futex_ms_left(const struct timespec *timeout)
{
   struct timespec now;

   clock_gettime(CLOCK_MONOTONIC, &now);
   return (int64_t)(timeout->tv_sec - now.tv_sec) * 1000 +
          (timeout->tv_nsec - now.tv_nsec + 999999) / 1000000;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   unsigned b = futex_bucket(addr);
   struct futex_waiter w;
   int64_t ms;
   int r, queued;

   futex_lock(&futex_buckets[b].lock);
   if (__atomic_load_n(addr, __ATOMIC_SEQ_CST) != (uint32_t)value) {
      futex_unlock(&futex_buckets[b].lock);
      errno = EAGAIN;
      return -1;
   }
   w.addr = addr;
   w.sem = 0;
   w.next = futex_buckets[b].head;
   futex_buckets[b].head = &w;
   futex_unlock(&futex_buckets[b].lock);

   if (timeout == NULL)
      r = _SEMACQUIRE(&w.sem, 1);
   else {
      /* the timeout is absolute, on CLOCK_MONOTONIC (as FUTEX_WAIT_BITSET) */
      ms = futex_ms_left(timeout);
      r = ms > 0 ? _TSEMACQUIRE(&w.sem, ms) : 0;
   }
   if (r > 0)
      return 0;
   /* timed out or a note: off the queue, unless a waker took it off */
   futex_lock(&futex_buckets[b].lock);
   queued = futex_unqueue(b, &w);
   futex_unlock(&futex_buckets[b].lock);
   if (!queued) {
      /* woken meanwhile: take the release, the node must stay until then */
      while (_SEMACQUIRE(&w.sem, 1) < 0)
         ;
      return 0;
   }
   errno = r == 0 ? ETIMEDOUT : EINTR;
   return -1;
}

#else
#error UTIL_FUTEX_SUPPORTED is not implemented but the header told it is supported on this platform
#endif

#endif /* UTIL_FUTEX_SUPPORTED */
