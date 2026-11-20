// SPDX-License-Identifier: MIT
/*
 * aux/i915: the Linux side, where Linux's names are seen: i915's
 * module_init, and what the DRM core asks of the file server.
 */
#include <linux/kernel.h>
#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_print.h>
#include "i915_params.h"
#include "i915_selftest.h"
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include "../i915.h"
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/delay.h>

struct pci_dev	*kpi_pci_gpu(const char *gdir);

int	kpi_init_i915_init(void);
int	kpi_init_st_init(void);	/* drivers/dma-buf/selftest.c */
extern int kpi_slabdebug;	/* linux/kpi/linux/slab.c */
extern int kpi_loglevel;
void	kpi_constructors(void);
int	kpi_param_set(const char *);

void
i915_options(int nodisplay, unsigned long drmdebug, int loglevel)
{
	/* Linux 7.3: a parameter of the display code (intel_display_params.c), copied at probe */
	kpi_constructors();
	if (nodisplay && kpi_param_set("disable_display=1") < 0)
		pr_err("i915: no parameter disable_display\n");
	__drm_debug = drmdebug;
	kpi_loglevel = loglevel;
}

int
i915_start(void)
{
	return kpi_init_i915_init();
}

/*
 * -T: what the laptop hit first: a page of a pool chunk attached after
 * the workers forked, read by a worker (the kernel attaches it in every
 * proc of the memory image: segattach(2))
 */
struct segtest {
	struct work_struct	work;
	u32	*page;
	u32	seen;
};

static void
segtest_work(struct work_struct *w)
{
	struct segtest *t = container_of(w, struct segtest, work);

	t->seen = READ_ONCE(t->page[100]);
}

/* vmap of pages apart: an alias segment (segment(3)) shows the same memory */
static int
vmap_selftest(void)
{
	struct page *p[3], *v[2];
	struct segtest t;
	u32 *a;
	int ok;

	p[0] = alloc_page(GFP_KERNEL | __GFP_ZERO);
	p[1] = alloc_page(GFP_KERNEL | __GFP_ZERO);
	p[2] = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!p[0] || !p[1] || !p[2])
		return -ENOMEM;
	v[0] = p[2];
	v[1] = p[0];
	a = vmap(v, 2, 0, PAGE_KERNEL);
	if (!a) {
		pr_info("selftest: vmap of 2 pages apart: FAIL (no mapping)\n");
		return -EIO;
	}
	a[1] = 0x1111;
	a[1024 + 2] = 0x2222;
	((u32 *)page_address(p[2]))[3] = 0x3333;
	ok = ((u32 *)page_address(p[2]))[1] == 0x1111 && ((u32 *)page_address(p[0]))[2] == 0x2222 && a[3] == 0x3333;
	/* a worker forked before the alias existed reads through it */
	t.page = a + 1024;
	t.page[100] = 0x915916;
	t.seen = 0;
	INIT_WORK(&t.work, segtest_work);
	queue_work(system_unbound_wq, &t.work);
	flush_work(&t.work);
	ok = ok && t.seen == 0x915916;
	pr_info("selftest: vmap of 2 pages apart (alias segment at %p): %s\n", a, ok ? "ok" : "FAIL");
	vunmap(a);
	put_page(p[0]);
	put_page(p[1]);
	put_page(p[2]);
	return ok ? 0 : -EIO;
}

/*
 * nth_page by frame (i915 joins frames that follow each other into one
 * scatterlist entry): within the page's chunk, past its end (8 MB), far
 * beyond; the page n frames on, with an address if it is the pool's
 * (the laptop's aux/i915 died flushing a scanout object's pages: page + n
 * ran past the chunk's array)
 */
static int
nthpage_selftest(void)
{
	static const unsigned long ns[] = { 1, 7, 2047, 2048, 2049, 4096, 65536 };
	struct page *p, *q;
	unsigned long n, pfn;
	int i, ok, bad;

	p = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;
	bad = 0;
	for (i = 0; i < ARRAY_SIZE(ns); i++) {
		n = ns[i];
		pfn = page_to_pfn(p) + n;
		q = nth_page(p, n);
		ok = q != NULL && page_to_pfn(q) == pfn && (!page_is_ram(pfn) || page_address(q) != NULL);
		if (!ok) {
			pr_info("selftest: nth_page(%#lx, %lu): %#lx, not frame %#lx: FAIL\n",
				page_to_pfn(p), n, q ? page_to_pfn(q) : 0, pfn);
			bad++;
		}
	}
	pr_info("selftest: nth_page by frame, past the chunk: %s\n", bad ? "FAIL" : "ok");
	put_page(p);
	return bad ? -EIO : 0;
}

/*
 * -T: what the firmware says about the machine: DMI (SMBIOS) strings,
 * the GPU's ACPI node (by its _ADR); with tables, the node's _ADR must
 * be the device's PCI address
 */
static int
firmware_selftest(void)
{
	struct pci_dev *p = kpi_pci_gpu(NULL);
	char name[128];
	struct acpi_buffer b = { sizeof(name), name };
	unsigned long long adr;
	acpi_handle h;
	const char *v;

	v = dmi_get_system_info(DMI_SYS_VENDOR);
	pr_info("selftest: dmi: sys vendor \"%s\", product \"%s\", bios \"%s\" \"%s\", board \"%s\"\n",
		v ? v : "(none)",
		dmi_get_system_info(DMI_PRODUCT_NAME) ?: "(none)",
		dmi_get_system_info(DMI_BIOS_VENDOR) ?: "(none)",
		dmi_get_system_info(DMI_BIOS_VERSION) ?: "(none)",
		dmi_get_system_info(DMI_BOARD_NAME) ?: "(none)");
	if (acpi_disabled) {
		pr_info("selftest: acpi: no tables\n");
		return 0;
	}
	if (!p || !(h = ACPI_HANDLE(&p->dev))) {
		pr_info("selftest: acpi: no node for the GPU %s: FAIL\n", p ? dev_name(&p->dev) : "(none)");
		return -ENOENT;
	}
	if (acpi_get_name(h, ACPI_FULL_PATHNAME, &b) != AE_OK)
		strscpy(name, "?", sizeof(name));
	if (acpi_evaluate_integer(h, "_ADR", NULL, &adr) != AE_OK) {
		pr_info("selftest: acpi: GPU node %s without _ADR: FAIL\n", name);
		return -EIO;
	}
	pr_info("selftest: acpi: GPU %s is %s, _ADR %#llx: %s\n", dev_name(&p->dev), name, adr,
		adr == ((unsigned long long)PCI_SLOT(p->devfn) << 16 | PCI_FUNC(p->devfn)) ? "ok" : "FAIL");
	if (adr != ((unsigned long long)PCI_SLOT(p->devfn) << 16 | PCI_FUNC(p->devfn)))
		return -EIO;
	/*
	 * tests/gpu's SSDT (nfst.py), where loaded: a store through Index
	 * changes the named buffer (firmware fills its replies so: ATIF)
	 */
	if (acpi_get_handle(NULL, "\\NFST", &h) == AE_OK) {
		struct acpi_buffer r = { ACPI_ALLOCATE_BUFFER, NULL };
		union acpi_object *o;
		bool ok;

		ok = acpi_evaluate_object(h, NULL, NULL, &r) == AE_OK && (o = r.pointer) != NULL &&
			o->type == ACPI_TYPE_BUFFER && o->buffer.length == 4 && o->buffer.pointer[1] == 0x5a;
		pr_info("selftest: acpi: Store to an Index of a named buffer: %s\n", ok ? "ok" : "FAIL");
		kfree(r.pointer);
		if (!ok)
			return -EIO;
	}
	return 0;
}

/* 9space: the LinuxKPI page pool's status line (gpu(3)'s status), as the status file has it */
int	kpi_pool_status(char *buf, int n);	/* page.c */

static int
poolstatus_selftest(void)
{
	char buf[256];
	struct page *p;
	int n;

	p = alloc_page(GFP_KERNEL);	/* the pool has a chunk then */
	n = kpi_pool_status(buf, sizeof(buf));
	if (p)
		put_page(p);
	pr_info("selftest: %s", n > 0 ? buf : "no pool status\n");
	return n > 0 && strncmp(buf, "pool chunks ", 12) == 0 ? 0 : -EIO;
}

/*
 * 9space: the pool gives back the chunks a load made once they are wholly
 * free (page.c's poolrelease, i915-pool.md (a)), while a kthread looks a
 * page up by address and by frame all the time: the page still found,
 * the chunks fewer, their bookkeeping freed once the lookups end
 */
int	kpi_pool_status(char *buf, int n);	/* page.c */
int	kpi_poolcheck(unsigned long f, unsigned long h, unsigned long r);	/* page.c: what its watcher does each second */
int	p9_memfree(unsigned long *free, unsigned long *high, unsigned long *room);

struct pooltest {
	struct page	*held;
	int	stop, done;
	unsigned long	n, bad;
};

static int
pool_reader(void *a)
{
	struct pooltest *t = a;
	struct page *h = t->held;
	unsigned long pfn = page_to_pfn(h);

	while (!READ_ONCE(t->stop)) {
		if (virt_to_page(page_address(h)) != h || pfn_to_page(pfn) != h)
			t->bad++;
		t->n++;
	}
	WRITE_ONCE(t->done, 1);
	return 0;
}

static int
poolstat4(unsigned long *nc, unsigned long *rel, unsigned long *def, unsigned long *whole, unsigned long *shortn)
{
	char buf[256];
	unsigned long c, s, mb, f;

	kpi_pool_status(buf, sizeof(buf));
	return sscanf(buf, "pool chunks %lu (contiguous %lu, scattered %lu), %lu MB, pages free %lu, chunks wholly free %lu, released %lu, deferred %lu, memory short %lu",
		nc, &c, &s, &mb, &f, whole, rel, def, shortn) == 9 ? 0 : -1;
}

static int
poolstat(unsigned long *nc, unsigned long *rel, unsigned long *def)
{
	unsigned long w, sh;

	return poolstat4(nc, rel, def, &w, &sh);
}

/*
 * 9space: memory running short (page.c's watcher, i915-pool.md (b)): the
 * kernel's line (/dev/swap's free: swap(3)) as the watcher reads it, then
 * the watcher's own check with a short value, after a load left chunks
 * wholly free in the reserve: counted, the reserve given back too
 */
static int
poolshort_selftest(void)
{
	static struct page *run[24];
	unsigned long f, h, r, nc, rel0, rel, def, whole, sh0, sh;
	int i, n, ms, okline, ok;

	okline = p9_memfree(&f, &h, &r) == 0 && f > 0 && h > 0 && r > h;
	pr_info("selftest: /dev/swap's free line: %lu free, high water %lu, headroom %lu: %s\n",
		f, h, r, okline ? "ok" : "FAIL");
	if (!okline)
		return -EIO;
	for (n = 0; n < 24; n++)	/* 24 MB: the reserve keeps 2 chunks of it */
		if ((run[n] = alloc_pages(GFP_KERNEL, 8)) == NULL)
			break;
	for (i = 0; i < n; i++)
		__free_pages(run[i], 8);
	msleep(1500);		/* the delayed release: down to the reserve */
	poolstat4(&nc, &rel0, &def, &whole, &sh0);
	i = kpi_poolcheck(r, h, r);	/* free = headroom: short (below twice it) */
	for (ms = 0; ms < 3000; ms += 100) {
		poolstat4(&nc, &rel, &def, &whole, &sh);
		if (def == 0)
			break;
		msleep(100);
	}
	ok = n == 24 && i == 1 && sh == sh0 + 1 && whole == 0 && rel > rel0 && def == 0;
	pr_info("selftest: pool when memory is short: %lu chunks after, wholly free %lu, released %lu more, deferred %lu, short counted %lu: %s\n",
		nc, whole, rel - rel0, def, sh - sh0, ok ? "ok" : "FAIL");
	return ok ? 0 : -EIO;
}

enum { Npool = 1024, Poolorder = 4 };	/* 64 MB in runs of 64 KB: 8 chunks */

static int
pool_selftest(void)
{
	static struct page *run[Npool];
	struct pooltest t = {0};
	unsigned long nc0, rel0, def0, nc1, rel1, def1, nc2, rel2, def2;
	int i, n, ms, ok;

	t.held = alloc_page(GFP_KERNEL);
	if (!t.held || poolstat(&nc0, &rel0, &def0) < 0) {
		pr_info("selftest: pool: no page or no status: FAIL\n");
		return -EIO;
	}
	if (IS_ERR(kthread_run(pool_reader, &t, "pooltest"))) {
		put_page(t.held);
		return -EIO;
	}
	for (n = 0; n < Npool; n++)
		if ((run[n] = alloc_pages(GFP_KERNEL, Poolorder)) == NULL)
			break;
	poolstat(&nc1, &rel1, &def1);
	for (i = 0; i < n; i++)
		__free_pages(run[i], Poolorder);
	/* poolrelease runs a second after the first chunk is wholly free */
	for (ms = 0; ms < 5000; ms += 100) {
		msleep(100);
		poolstat(&nc2, &rel2, &def2);
		if (rel2 > rel0 && nc2 < nc1)
			break;
	}
	WRITE_ONCE(t.stop, 1);
	while (!READ_ONCE(t.done))
		msleep(10);
	/* no lookup under way now: the bookkeeping goes at the next try */
	for (ms = 0; ms < 5000; ms += 100) {
		poolstat(&nc2, &rel2, &def2);
		if (def2 == 0)
			break;
		msleep(100);
	}
	put_page(t.held);
	ok = n == Npool && rel2 > rel0 && nc2 < nc1 && def2 == 0 && t.bad == 0 && t.n > 0;
	pr_info("selftest: pool gives back wholly free chunks: %lu chunks, %lu with the load, %lu after (released %lu, deferred %lu), %lu lookups meanwhile, %lu wrong: %s\n",
		nc0, nc1, nc2, rel2 - rel0, def2, t.n, t.bad, ok ? "ok" : "FAIL");
	return ok ? 0 : -EIO;
}

int
i915_kpitest(void)
{
	struct segtest t;
	struct page *p;
	int r;

	p = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!p)
		return -ENOMEM;
	t.page = page_address(p);
	t.page[100] = 0x915915;
	t.seen = 0;
	INIT_WORK(&t.work, segtest_work);
	queue_work(system_unbound_wq, &t.work);
	flush_work(&t.work);
	pr_info("selftest: a worker read %#x from a page of a later chunk: %s\n", t.seen,
		t.seen == 0x915915 ? "ok" : "FAIL");
	put_page(p);
	if (t.seen != 0x915915)
		return -EIO;
	r = vmap_selftest();
	if (r)
		return r;
	r = nthpage_selftest();
	if (r)
		return r;
	r = firmware_selftest();
	if (r)
		return r;
	r = poolstatus_selftest();
	if (r)
		return r;
	r = pool_selftest();
	if (r)
		return r;
	r = poolshort_selftest();
	if (r)
		return r;
	/* Linux's selftests of fences, chains, arrays, reservation objects; with the slab's checks */
	kpi_slabdebug = 1;
	r = kpi_init_st_init();
	pr_info("selftest: Linux's dma-buf selftests: %s (%d)\n", r ? "FAIL" : "ok", r);
	return r;
}

/*
 * -M: i915's mock selftests (no hardware: a mock device), as Linux runs
 * them with i915.mock_selftests=-1: i915_init runs its init functions,
 * the module's caches first, the selftests among them, then stops
 * there; filter as i915.st_filter (NULL: keep it, as -p set it).  0, or the first
 * failure's error
 */
int
i915_mock(char *filter)
{
	int r;

	i915_selftest.mock = -1;
	if (filter)
		i915_selftest.filter = filter;
	r = kpi_init_i915_init();
	if (i915_selftest.mock == -ENOTTY)
		return 0;	/* all ran and passed */
	if (i915_selftest.mock < 0)
		return i915_selftest.mock;
	return r < 0 ? r : -EIO;
}

/*
 * -L: the live selftests ran in the probe (live_selftests=-1); their
 * outcome, as i915_selftest.live has it: 0 all passed, or the error.
 * Still -1: they did not run (no device, the probe failed before
 * them): the probe's error.
 */
int
i915_live_result(int probe)
{
	if (i915_selftest.live == -ENOTTY)
		return 0;
	if (i915_selftest.live == -1) {
		/* i915_init succeeds without a device, as in Linux */
		if (probe < 0)
			pr_err("i915: live selftests did not run: error %d\n", probe);
		else
			pr_err("i915: live selftests did not run: no device probed\n");
		return probe < 0 ? probe : -ENODEV;
	}
	return i915_selftest.live < 0 ? i915_selftest.live : -EIO;
}

/* drm_sysfs: a connector's state changed (the file server will tell its clients) */
void
kpi_drm_hotplug(struct drm_device *dev, struct drm_connector *connector)
{
	if (connector)
		pr_info("i915: hotplug %s\n", connector->name);
	else
		pr_info("i915: hotplug\n");
}
