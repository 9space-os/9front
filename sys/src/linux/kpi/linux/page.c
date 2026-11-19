// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: pages.  Linux's pages are memory the device can
 * reach by physical address; here they come from a pool of #g segments
 * of pinned pages (sticky: they stay where they are) attached to the
 * file server, 8 MB each, made as needed; vmap of scattered pages makes
 * an alias segment of them.  Each page of a chunk has its
 * struct page, with where the server sees it and where the device does.
 * A run of 2^order pages must be contiguous both ways; pages are found
 * again by address (virt_to_page) and by frame number (pfn_to_page).
 *
 * 9space: the device's DMA mask (dma_set_mask, dma.c), once the driver
 * sets one, and GFP_DMA32 bound the pool: pages are given only from
 * chunks the device reaches, made below 4 GB (segment(3)'s dma32) for
 * 32 bits.  Linux bounces or remaps (swiotlb, an IOMMU) where we have
 * neither.  Without a mask there is no bound: Linux's default is 32
 * bits, but memory below 4 GB is scarce here (plan9.ini(8)'s *dma32),
 * and the drivers that need it say so (aux/rtw89: kpi_skb_memlimit).
 */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/bitmap.h>
#include <linux/vmalloc.h>
#include <linux/pagevec.h>
#include <linux/smp.h>
#include <linux/dma-mapping.h>
#include <kpi/p9.h>

#define CHUNK		(8UL << 20)
#define CHUNKPAGES	(CHUNK >> PAGE_SHIFT)
#define NHASH		4096

unsigned long	kpi_shrink(gfp_t gfp, unsigned long want);	/* shrinker.c */

struct chunk {
	struct chunk	*next;
	char	name[32];
	u8	*va;
	struct page	*pages;
	unsigned long	used[BITS_TO_LONGS(CHUNKPAGES)];
	unsigned long	nfree;
	u64	maxpa;		/* 9space: its highest byte's physical address */
	u64	pa0;		/* 9space: its first page's physical address */
	int	contig;		/* 9space: its pages follow each other physically from pa0 */
};

u64 kpi_dma_limit = DMA_BIT_MASK(64);	/* 9space: the device's DMA mask (dma.c) */

static u64 plock[4];		/* QLock */
static struct chunk *chunks;
static int nchunks;
static struct page *hash[NHASH];

static inline unsigned int hashpfn(unsigned long pfn) { return (pfn * 2654435761UL) % NHASH; }

/*
 * 9space: the chunks by address, so that a page is found without a walk:
 * a chunk's run of addresses (physical for a contiguous one, virtual for
 * every one) touches at most two keys of CHUNK's size, and there are few
 * chunks, so a key's list is short. pfn_to_page and virt_to_page walked
 * a hash chain of pages (a pool of N chunks: N/2 pages a list) or every
 * chunk; i915 asks for each page of an object, many times a frame. A
 * scattered chunk's pages stay in hash[]. Lists only grow (chunks are
 * not given back): a new entry is published with a release store, the
 * lookups take no lock.
 */
#define KEYSHIFT	23	/* log2(CHUNK) */

struct cref {
	struct cref	*next;
	struct chunk	*c;
};

static struct cref *bypa[NHASH], *byva[NHASH];

/* 0, -1 if an entry could not be made (then that key's lookup does not find c) */
static int
addref(struct cref **t, u64 a, struct chunk *c)
{
	struct cref *r;
	u64 k;
	int ok = 0;

	for (k = a >> KEYSHIFT; k <= (a + CHUNK - 1) >> KEYSHIFT; k++) {
		r = p9_mallocz(sizeof(*r));
		if (r == NULL) {
			ok = -1;
			continue;
		}
		r->c = c;
		r->next = t[hashpfn(k)];
		__atomic_store_n(&t[hashpfn(k)], r, __ATOMIC_RELEASE);
	}
	return ok;
}

/* the contiguous chunk with the page at physical address pa, nil if none */
static struct chunk *
chunkbypa(u64 pa)
{
	struct cref *r;
	struct chunk *c;

	for (r = __atomic_load_n(&bypa[hashpfn(pa >> KEYSHIFT)], __ATOMIC_ACQUIRE); r; r = r->next) {
		c = r->c;
		if (pa >= c->pa0 && pa < c->pa0 + CHUNK)
			return c;
	}
	return NULL;
}

/* the chunk with virtual address a, nil if none */
static struct chunk *
chunkbyva(const u8 *a)
{
	struct cref *r;
	struct chunk *c;

	for (r = __atomic_load_n(&byva[hashpfn((unsigned long)a >> KEYSHIFT)], __ATOMIC_ACQUIRE); r; r = r->next) {
		c = r->c;
		if (a >= c->va && a < c->va + CHUNK)
			return c;
	}
	return NULL;
}

static struct chunk *
newchunk(u64 limit)
{
	unsigned long long *pa;
	struct chunk *c;
	struct page *p;
	unsigned long i;
	unsigned int h;
	int dma32;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	pa = kmalloc_array(CHUNKPAGES, sizeof(*pa), GFP_KERNEL);
	if (!c || !pa)
		goto err;
	c->pages = kcalloc(CHUNKPAGES, sizeof(struct page), GFP_KERNEL);
	if (!c->pages)
		goto err;
	snprintf(c->name, sizeof(c->name), "kpi.%d.%d", p9_getpid(), nchunks);
	/*
	 * physically contiguous and aligned if the kernel has such a run
	 * free (then every block of an order is naturally aligned, as
	 * Linux's buddy allocator gives them), else scattered pages
	 */
	/*
	 * 9space: below 4 GB if the device addresses no more; a chunk
	 * above a wider mask (more memory than the device reaches) again
	 * so
	 */
	dma32 = limit <= DMA_BIT_MASK(32) ? P9SEGDMA32 : 0;
Again:
	c->va = p9_segalloc(c->name, CHUNK, pa, P9SEGCONTIG | dma32);
	if (!c->va) {
		/* scattered pages; below 4 GB a run, not aligned (segment(3)) */
		snprintf(c->name, sizeof(c->name), "kpi.%d.%ds", p9_getpid(), nchunks);
		c->va = p9_segalloc(c->name, CHUNK, pa, dma32);
	}
	if (!c->va)
		goto err;
	c->maxpa = 0;
	for (i = 0; i < CHUNKPAGES; i++)
		if (pa[i] + PAGE_SIZE - 1 > c->maxpa)
			c->maxpa = pa[i] + PAGE_SIZE - 1;
	if (c->maxpa > limit) {
		p9_segfree(c->name, c->va);
		c->va = NULL;
		if (dma32)
			goto err;
		dma32 = P9SEGDMA32;
		snprintf(c->name, sizeof(c->name), "kpi.%d.%dl", p9_getpid(), nchunks);
		goto Again;
	}
	/* 9space: contiguous (the usual case): found by its range, not page by page */
	c->pa0 = pa[0];
	c->contig = 1;
	for (i = 1; i < CHUNKPAGES; i++)
		if (pa[i] != pa[0] + i * PAGE_SIZE) {
			c->contig = 0;
			break;
		}
	if (c->contig && addref(bypa, c->pa0, c) < 0)
		c->contig = 0;	/* no memory for its entry: page by page then */
	for (i = 0; i < CHUNKPAGES; i++) {
		p = &c->pages[i];
		p->virtual = c->va + i * PAGE_SIZE;
		p->phys_addr = pa[i];
		p->kpi_chunk = c;
		INIT_LIST_HEAD(&p->lru);
		if (!c->contig) {
			h = hashpfn(pa[i] >> PAGE_SHIFT);
			p->kpi_hnext = hash[h];
			__atomic_store_n(&hash[h], p, __ATOMIC_RELEASE);
		}
	}
	addref(byva, (u64)(uintptr_t)c->va, c);	/* (failed: virt_to_page walks the chunks) */
	c->nfree = CHUNKPAGES;
	kfree(pa);
	c->next = chunks;
	chunks = c;
	nchunks++;
	return c;
err:
	if (c)
		kfree(c->pages);
	kfree(pa);
	kfree(c);
	return NULL;
}

/*
 * a free run of n pages in c, physically contiguous and aligned to its
 * size, as Linux's alloc_pages(order) gives
 */
static long
findrun(struct chunk *c, unsigned long n)
{
	unsigned long i, j, k, mis;

	/*
	 * 9space: from free page to free page, past a used one in the way,
	 * to the next aligned page of a contiguous chunk: the bitmap's words,
	 * not every index (that took most of aux/i915's time, page by page
	 * through the used part of each chunk for every allocation)
	 */
	i = 0;
	while (i + n <= CHUNKPAGES) {
		i = _find_next_zero_bit(c->used, CHUNKPAGES, i);
		if (i + n > CHUNKPAGES)
			break;
		mis = c->pages[i].phys_addr & (n * PAGE_SIZE - 1);
		if (mis) {
			if (c->contig)
				i += (n * PAGE_SIZE - mis) >> PAGE_SHIFT;
			else
				i++;
			continue;
		}
		j = _find_next_bit(c->used, i + n, i);
		if (j < i + n) {
			i = j + 1;
			continue;
		}
		for (k = 1; k < n; k++)
			if (c->pages[i + k].phys_addr != c->pages[i].phys_addr + k * PAGE_SIZE)
				break;
		if (k == n)
			return i;
		i++;
	}
	return -1;
}

struct page *
alloc_pages(gfp_t gfp, unsigned int order)
{
	unsigned long n = 1UL << order, k;
	struct chunk *c;
	struct page *p;
	long i = -1;
	int shrunk = 0;
	u64 limit;

	if (n > CHUNKPAGES)
		return NULL;
	limit = kpi_dma_limit;
	if ((gfp & __GFP_DMA32) && limit > DMA_BIT_MASK(32))
		limit = DMA_BIT_MASK(32);
again:
	p9_qlock(plock);
	for (c = chunks; c; c = c->next)
		if (c->maxpa <= limit && c->nfree >= n && (i = findrun(c, n)) >= 0)
			break;
	if (!c) {
		/* a new chunk; a higher order may still fail there, then the caller tries lower */
		c = newchunk(limit);
		if (!c && !(gfp & __GFP_NORETRY) && !shrunk) {
			/* no more memory: what the shrinkers give back, then again */
			p9_qunlock(plock);
			shrunk = 1;
			kpi_shrink(gfp, 2 * n);
			goto again;
		}
		if (!c || (i = findrun(c, n)) < 0) {
			p9_qunlock(plock);
			return NULL;
		}
	}
	bitmap_set(c->used, i, n);
	c->nfree -= n;
	p9_qunlock(plock);
	p = &c->pages[i];
	for (k = 0; k < n; k++) {
		p[k].flags = 0;
		p[k].private = 0;
		p[k].mapping = NULL;
		p[k].index = 0;
		p[k].order = 0;
		p[k].kpi_head = k ? p : NULL;
		atomic_set(&p[k]._refcount, k ? 0 : 1);
		atomic_set(&p[k]._mapcount, -1);
	}
	p->order = order;
	if (gfp & __GFP_ZERO)
		memset(p->virtual, 0, n * PAGE_SIZE);
	return p;
}

struct folio *folio_alloc(gfp_t gfp, unsigned int order) { return (struct folio *)alloc_pages(gfp | __GFP_COMP, order); }

static void
freerun(struct page *p)
{
	struct chunk *c = p->kpi_chunk;
	unsigned long n = 1UL << p->order;

	p9_qlock(plock);
	bitmap_clear(c->used, p - c->pages, n);
	c->nfree += n;
	p9_qunlock(plock);
}

void
put_page(struct page *p)
{
	if (!p->kpi_chunk)
		return;		/* a page of the heap or a frame's (heappage, kpi_pfn_to_page): not the pool's */
	p = compound_head(p);
	if (atomic_dec_and_test(&p->_refcount))
		freerun(p);
}

void __free_pages(struct page *p, unsigned int order) { put_page(p); }

/*
 * 9space: the pool's state, a status line (gpu(4)'s status, through
 * kmsout.h): its chunks, how many are physically contiguous and how many
 * scattered, its free pages, the chunks wholly free, and the chunks given
 * back to the system and the bookkeeping waiting for readers to leave
 * (both 0 until the pool gives chunks back: i915-pool.md (a))
 */
static unsigned long nreleased, ndeferred;

int
kpi_pool_status(char *buf, int n)
{
	struct chunk *c;
	unsigned long nc = 0, ncontig = 0, nfree = 0, nwhole = 0;

	p9_qlock(plock);
	for (c = chunks; c; c = c->next) {
		nc++;
		ncontig += c->contig != 0;
		nfree += c->nfree;
		nwhole += c->nfree == CHUNKPAGES;
	}
	p9_qunlock(plock);
	return snprintf(buf, n, "pool chunks %lu (contiguous %lu, scattered %lu), %lu MB, pages free %lu, chunks wholly free %lu, released %lu, deferred %lu\n",
		nc, ncontig, nc - ncontig, nc * (CHUNK >> 20), nfree, nwhole, nreleased, ndeferred);
}

/*
 * 9front: a run of 2^order pages into as many pages of their own (TTM's
 * pool before it frees them one by one): each its reference, order 0
 */
void
split_page(struct page *p, unsigned int order)
{
	unsigned long n = 1UL << order, k;

	for (k = 0; k < n; k++) {
		p[k].order = 0;
		p[k].kpi_head = NULL;
		if (k)
			atomic_set(&p[k]._refcount, 1);
	}
}

unsigned long
__get_free_pages(gfp_t gfp, unsigned int order)
{
	struct page *p = alloc_pages(gfp, order);

	return p ? (unsigned long)p->virtual : 0;
}

unsigned long get_zeroed_page(gfp_t gfp) { return __get_free_pages(gfp | __GFP_ZERO, 0); }

void
free_pages(unsigned long addr, unsigned int order)
{
	if (addr)
		__free_pages(virt_to_page((void *)addr), order);
}

void
__folio_batch_release(struct folio_batch *fb)
{
	unsigned int i;

	for (i = 0; i < fb->nr; i++)
		folio_put(fb->folios[i]);
	fb->nr = 0;
}

/*
 * Linux gives every kernel address a struct page, kmalloc's too (the
 * error state puts kmalloc'd text in a scatter list).  A page of the
 * heap gets one when first asked for: its address for the processor,
 * no physical address (not for the device), never given back to the
 * pool; found again by its address
 */
static struct page *heappages[NHASH];

static struct page *
heappage(const u8 *a)
{
	unsigned long va = (unsigned long)a & PAGE_MASK;
	unsigned int h = hashpfn(va >> PAGE_SHIFT);
	struct page *p;

	p9_qlock(plock);
	for (p = heappages[h]; p; p = p->kpi_hnext)
		if ((unsigned long)p->virtual == va)
			break;
	if (!p) {
		p = p9_mallocz(sizeof(*p));
		if (p) {
			p->virtual = (void *)va;
			INIT_LIST_HEAD(&p->lru);
			atomic_set(&p->_refcount, 1);
			atomic_set(&p->_mapcount, -1);
			p->kpi_hnext = heappages[h];
			heappages[h] = p;
		}
	}
	p9_qunlock(plock);
	return p;
}

struct page *
kpi_virt_to_page(const void *v)
{
	const u8 *a = v;
	struct chunk *c;

	if ((c = chunkbyva(a)) != NULL)
		return &c->pages[(a - c->va) >> PAGE_SHIFT];
	for (c = chunks; c; c = c->next)	/* (a key without its entry: no memory then) */
		if (a >= c->va && a < c->va + CHUNK)
			return &c->pages[(a - c->va) >> PAGE_SHIFT];
	return heappage(a);
}

void *kpi_page_address(const struct page *p) { return p->virtual; }

/*
 * Linux has a struct page for every page frame (the memmap): a frame
 * that is no page of the pool gets one when first asked for, its
 * physical address only (no address for the processor), never the
 * pool's (put_page leaves it); found again by its number
 */
static struct page *framepages[NHASH];

/*
 * a frame of RAM: what we know to be is the pool's (other frames, of the
 * machine's memory or of a device, are not told apart here)
 */
int
page_is_ram(unsigned long pfn)
{
	struct page *p;

	if (chunkbypa((u64)pfn << PAGE_SHIFT) != NULL)
		return 1;
	for (p = hash[hashpfn(pfn)]; p; p = p->kpi_hnext)
		if ((p->phys_addr >> PAGE_SHIFT) == pfn)
			return 1;
	return 0;
}

struct page *
kpi_pfn_to_page(unsigned long pfn)
{
	unsigned int h = hashpfn(pfn);
	struct chunk *c;
	struct page *p;
	u64 pa = (u64)pfn << PAGE_SHIFT;

	if ((c = chunkbypa(pa)) != NULL)
		return &c->pages[(pa - c->pa0) >> PAGE_SHIFT];
	for (p = hash[h]; p; p = p->kpi_hnext)
		if ((p->phys_addr >> PAGE_SHIFT) == pfn)
			return p;
	p9_qlock(plock);
	for (p = framepages[h]; p; p = p->kpi_hnext)
		if ((p->phys_addr >> PAGE_SHIFT) == pfn)
			break;
	if (!p) {
		p = p9_mallocz(sizeof(*p));
		if (p) {
			p->phys_addr = (phys_addr_t)pfn << PAGE_SHIFT;
			INIT_LIST_HEAD(&p->lru);
			atomic_set(&p->_refcount, 1);
			atomic_set(&p->_mapcount, -1);
			p->kpi_hnext = framepages[h];
			framepages[h] = p;
		}
	}
	p9_qunlock(plock);
	return p;
}

/*
 * nth_page: the page n frames after p; p + n while that is in p's chunk
 * and the frame that follows (a chunk's frames may be scattered), else
 * found by its frame number (another chunk's, which lies right after it
 * in memory: page + n would run past the end of p's chunk's array)
 */
struct page *
kpi_nth_page(struct page *p, unsigned long n)
{
	struct chunk *c = p->kpi_chunk;
	unsigned long i;

	if (n == 0)
		return p;
	if (c) {
		i = p - c->pages;
		if (i + n < CHUNKPAGES && c->pages[i + n].phys_addr == p->phys_addr + ((phys_addr_t)n << PAGE_SHIFT))
			return p + n;
	}
	return kpi_pfn_to_page((p->phys_addr >> PAGE_SHIFT) + n);
}

/*
 * vmap: pages that lie one after the other in a chunk are mapped
 * already; others get an alias segment (segment(3)) made of them
 */
struct vmapping {
	struct list_head	node;
	void	*va;
	char	name[32];	/* the alias segment; "": the pages lie one after the other */
	struct page	**pages;
	unsigned int	count;
	unsigned long	flags;	/* VM_MAP_PUT_PAGES: vfree puts the pages, frees pages */
};
static LIST_HEAD(vmappings);
static int nvmaps;

/*
 * an alias segment (segment(3)) named name of pages of the pool, page i
 * at i*PAGE_SIZE: its address; attached here if attach, else only made
 * (an object for a client: the file server attaches none of them)
 */
void *
kpi_alias_pages(const char *name, struct page **pages, unsigned int count, bool attach)
{
	unsigned long long *off;
	const char **src;
	struct chunk *c;
	unsigned int i;
	void *va = NULL;

	src = kmalloc_array(count, sizeof(*src), GFP_KERNEL);
	off = kmalloc_array(count, sizeof(*off), GFP_KERNEL);
	if (!src || !off)
		goto out;
	for (i = 0; i < count; i++) {
		c = pages[i]->kpi_chunk;
		if (!c) {
			pr_warn("alias %s: page %u not of the pool\n", name, i);
			goto out;
		}
		src[i] = c->name;
		off[i] = (u8 *)pages[i]->virtual - c->va;
		/* the kernel takes whole pages of the chunk only (segment(3)) */
		if (off[i] & (PAGE_SIZE - 1) || off[i] >= CHUNK) {
			pr_warn("alias %s: page %u (%p) at %p: not a page of chunk %s (%p)\n",
			    name, i, pages[i], pages[i]->virtual, c->name, c->va);
			goto out;
		}
	}
	va = p9_segalias(name, count, src, off, attach);
out:
	kfree(src);
	kfree(off);
	return va;
}

/*
 * vmap: pages that lie one after the other in a chunk are mapped
 * already, others get an alias segment (segment(3)) made of them.  Each
 * mapping is recorded, so that vunmap and vfree find it, as Linux's
 * vmap areas: with VM_MAP_PUT_PAGES the mapping owns the pages and the
 * array, and vfree gives them back
 */
void *
vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{
	struct vmapping *m;
	unsigned int i;

	if (!count)
		return NULL;
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return NULL;
	m->pages = pages;
	m->count = count;
	m->flags = flags;
	for (i = 1; i < count; i++)
		if ((u8 *)pages[i]->virtual != (u8 *)pages[0]->virtual + i * PAGE_SIZE)
			break;
	if (i == count)
		m->va = pages[0]->virtual;
	else {
		p9_qlock(plock);
		snprintf(m->name, sizeof(m->name), "kpi.%d.v%d", p9_getpid(), nvmaps++);
		p9_qunlock(plock);
		m->va = kpi_alias_pages(m->name, pages, count, true);
		if (!m->va) {
			pr_warn("vmap: %u pages: no alias segment\n", count);
			kfree(m);
			return NULL;
		}
	}
	p9_qlock(plock);
	list_add(&m->node, &vmappings);
	p9_qunlock(plock);
	return m->va;
}

/*
 * the mapping va is in, as Linux finds the vmap area of an address: a
 * caller may free a pointer into it (i915's selftest_lrc frees its
 * mapping of a context image past the first page); the mapping that
 * begins there first (two vmaps of the same pages begin at one place)
 */
static struct vmapping *
takevmap(const void *va)
{
	struct vmapping *m, *found = NULL;
	const u8 *p = va;

	p9_qlock(plock);
	list_for_each_entry(m, &vmappings, node)
		if (m->va == va) {
			found = m;
			break;
		}
	if (!found)
		list_for_each_entry(m, &vmappings, node)
			if (p > (u8 *)m->va && p < (u8 *)m->va + (size_t)m->count * PAGE_SIZE) {
				found = m;
				break;
			}
	if (found)
		list_del(&found->node);
	p9_qunlock(plock);
	return found;
}

static void
unmapvmap(struct vmapping *m)
{
	/* the alias segment is revoked in every proc that had it (segment(3)) */
	if (m->name[0])
		p9_segfree(m->name, m->va);
}

void
vunmap(const void *va)
{
	struct vmapping *m = takevmap(va);

	if (m) {
		unmapvmap(m);
		kfree(m);
	}
}

/* vmalloc'd memory (the heap), or a vmap mapping (Linux's vfree takes both) */
void
vfree(const void *va)
{
	struct vmapping *m;
	unsigned int i;

	if (!va)
		return;
	m = takevmap(va);
	if (!m) {
		kfree(va);
		return;
	}
	unmapvmap(m);
	if (m->flags & VM_MAP_PUT_PAGES) {
		for (i = 0; i < m->count; i++)
			put_page(m->pages[i]);
		kvfree(m->pages);
	}
	kfree(m);
}

static unsigned long long memtotal, memfree;

void
si_meminfo(struct sysinfo *i)
{
	if (p9_meminfo(&memtotal, &memfree) < 0) {
		memtotal = 1ULL << 32;
		memfree = memtotal / 2;
	}
	i->totalram = memtotal >> PAGE_SHIFT;
	i->freeram = memfree >> PAGE_SHIFT;
	i->totalhigh = 0;
	i->mem_unit = PAGE_SIZE;
}


int kpi_wbinvd(void) { return p9_wbinvd(); }

/* 9front: the one memory node (NODE_DATA) */
#include <linux/mmzone.h>
pg_data_t kpi_node0;

/*
 * unmap_mapping_range: the CPU's mappings of the device's mmap offsets
 * [start, start+len) go (TTM, before it moves an object); the driver
 * shows objects to clients as pager segments (segment(3)) and takes
 * their pages back here
 */
void	(*kpi_unmap_range)(unsigned long long start, unsigned long long len);

void
unmap_mapping_range(struct address_space *m, loff_t start, loff_t len, int even_cows)
{
	(void)m;
	(void)even_cows;
	if(kpi_unmap_range != NULL)
		kpi_unmap_range(start, len);
}
