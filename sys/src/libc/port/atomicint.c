/*
 * GCC port: Aint, the 32-bit counterpart of Along (a long has 64 bits
 * under gcc, 32 under kencc): the word of a semacquire(2) semaphore, which
 * the kernel changes with a 32-bit compare-and-swap, so that a program can
 * work on the same word with these. gcc's __atomic builtins, sequentially
 * consistent, as atomic(2) says of all of them. Under kencc a long has
 * 32 bits: Aint is Along there, and these are atomic(2)'s functions.
 */
#include <u.h>
#include <libc.h>

#ifdef __GNUC__

int
ageti(Aint *a)
{
	return __atomic_load_n(&a->v, __ATOMIC_SEQ_CST);
}

int
aswapi(Aint *a, int v)
{
	return __atomic_exchange_n(&a->v, v, __ATOMIC_SEQ_CST);
}

int
ainci(Aint *a, int n)
{
	return __atomic_add_fetch(&a->v, n, __ATOMIC_SEQ_CST);
}

int
acasi(Aint *a, int old, int new)
{
	return __atomic_compare_exchange_n(&a->v, &old, new, 0,
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#else
int
ageti(Aint *a)
{
	return agetl((Along*)a);
}

int
aswapi(Aint *a, int v)
{
	return aswapl((Along*)a, v);
}

int
ainci(Aint *a, int n)
{
	return aincl((Along*)a, n);
}

int
acasi(Aint *a, int old, int new)
{
	return acasl((Along*)a, old, new);
}
#endif
