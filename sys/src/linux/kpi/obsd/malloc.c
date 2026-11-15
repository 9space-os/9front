/* SPDX-License-Identifier: MIT */
/*
 * LinuxKPI for 9front: OpenBSD's kpi_malloc(9), pool(9) and km_alloc(9) on
 * the file server's heap.
 *
 * A pool keeps the items given back on a free list of its own and never
 * returns them to the heap: an item stays an item of its pool, as the
 * Linux code expects of SLAB_TYPESAFE_BY_RCU caches (i915's requests are
 * read under RCU after being freed and reused).
 */
#include "obsd.h"
#include <sys/malloc.h>
#include <sys/pool.h>
#include <uvm/uvm_extern.h>

void *
kpi_malloc(size_t size, int type, int flags)
{
	void *p;

	size_t align;

	/*
	 * 9front: aligned as Linux's kmalloc gives a block, to its size
	 * rounded up to a power of two, here at most ARCH_KMALLOC_MINALIGN
	 * (64): a type declared __aligned(64) has a size that is a multiple
	 * of it, and gcc's code relies on the alignment (16-byte aligned
	 * stores: struct sk_buff's trapped in rtw89).  Plan 9's malloc
	 * aligns to 8 only.
	 */
	if (size == 0)
		size = 1;
	for (align = 8; align < size && align < 64; align <<= 1)
		;
	if (align > 8) {
		p = p9_mallocalign(size, align);
		if (p != NULL && (flags & M_ZERO))
			memset(p, 0, size);
	} else if (flags & M_ZERO)
		p = p9_mallocz(size);
	else
		p = p9_malloc(size);
	if (p == NULL && !(flags & (M_NOWAIT | M_CANFAIL)))
		panic("malloc: out of memory (%zu bytes)", size);
	/*
	 * the caller's pc as the block's tag, not ours: kmalloc and kzalloc
	 * are inline, so a pool panic's dump names the driver's code that
	 * allocated the block
	 */
	if (p != NULL)
		p9_setmalloctag(p, (unsigned long long)(uintptr_t)__builtin_return_address(0));
	return p;
}

void *
mallocarray(size_t n, size_t size, int type, int flags)
{
	if (size != 0 && n > SIZE_MAX / size) {
		if (flags & M_CANFAIL)
			return NULL;
		panic("mallocarray: %zu * %zu overflows", n, size);
	}
	return kpi_malloc(n * size, type, flags);
}

int	kpi_slab_owned(const void *);

void
kpi_free(void *p, int type, size_t size)
{
	if (p != NULL && !kpi_slab_owned(p))
		p9_free(p);
}

struct poolitem {
	struct poolitem	*next;
};

void
pool_init(struct pool *pp, size_t size, unsigned int align, int ipl, int flags,
    const char *wchan, void *palloc)
{
	memset(pp, 0, sizeof(*pp));
	if (size < sizeof(struct poolitem))
		size = sizeof(struct poolitem);
	pp->pr_size = size;
	pp->pr_align = align;
	pp->pr_ipl = ipl;
	pp->pr_wchan = wchan;
	mtx_init(&pp->pr_mtx, ipl);
}

void
pool_setipl(struct pool *pp, int ipl)
{
	pp->pr_ipl = ipl;
}

void
pool_destroy(struct pool *pp)
{
	struct poolitem *it;

	mtx_enter(&pp->pr_mtx);
	while ((it = pp->pr_free) != NULL) {
		pp->pr_free = it->next;
		p9_free(it);
	}
	mtx_leave(&pp->pr_mtx);
}

void *
pool_get(struct pool *pp, int flags)
{
	struct poolitem *it;
	void *p;

	mtx_enter(&pp->pr_mtx);
	it = pp->pr_free;
	if (it != NULL)
		pp->pr_free = it->next;
	mtx_leave(&pp->pr_mtx);
	if (it != NULL)
		p = it;
	else if (pp->pr_align > 16)
		p = p9_mallocalign(pp->pr_size, pp->pr_align);
	else
		p = p9_malloc(pp->pr_size);
	if (p == NULL) {
		if (flags & (PR_NOWAIT | PR_LIMITFAIL))
			return NULL;
		panic("pool_get: %s: out of memory", pp->pr_wchan);
	}
	if (flags & PR_ZERO)
		memset(p, 0, pp->pr_size);
	atomic_inc_int(&pp->pr_nout);
	return p;
}

void
pool_put(struct pool *pp, void *p)
{
	struct poolitem *it = p;

	if (p == NULL)
		return;
	mtx_enter(&pp->pr_mtx);
	it->next = pp->pr_free;
	pp->pr_free = it;
	mtx_leave(&pp->pr_mtx);
	atomic_dec_int(&pp->pr_nout);
}

/* km_alloc(9): page aligned memory of the heap (its pages are found by uvm_atopg) */
const struct kmem_va_mode kv_any = { 1 }, kv_page = { 1 };
const struct kmem_pa_mode kp_none = { 0 }, kp_zero = { 1 }, kp_dirty = { 0 };
const struct kmem_dyn_mode kd_waitok = { 1 }, kd_nowait = { 0 };

void *
km_alloc(size_t size, const struct kmem_va_mode *kv, const struct kmem_pa_mode *kp,
    const struct kmem_dyn_mode *kd)
{
	void *p;

	p = p9_mallocalign(round_page(size), PAGE_SIZE);
	if (p == NULL) {
		if (!kd->kd_waitok)
			return NULL;
		panic("km_alloc: out of memory");
	}
	if (kp->kp_zero)
		memset(p, 0, round_page(size));
	return p;
}

void
km_free(void *p, size_t size, const struct kmem_va_mode *kv, const struct kmem_pa_mode *kp)
{
	p9_free(p);
}
