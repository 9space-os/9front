// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: starting up.  What Linux does at boot for the
 * drivers (the processor, the system workqueues, the PCI device; DRM's
 * core and helpers in drm.c); a driver's own module_init comes after,
 * from its file server (aux/i915: i915_init).
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <kpi/p9.h>
#include <kpi/linux.h>
#include <uvm/uvm_extern.h>

/* the machine's memory, as OpenBSD's uvm counts it */
static void
kpi_meminit(void)
{
	unsigned long long total, avail;

	if (p9_meminfo(&total, &avail) < 0)
		return;
	physmem = uvmexp.npages = total / PAGE_SIZE;
	uvmexp.free = avail / PAGE_SIZE;
	uvmexp.pagesize = PAGE_SIZE;
}

void	kpi_cpu_init(void);
void	drm_linux_init(void);	/* OpenBSD's: the system workqueues, tasklets, pools */
struct pci_dev	*kpi_pci_gpu(const char *gdir);

void	kpi_constructors(void);

int
kpi_start(const char *gdir)
{
	p9_fpu_kernel();	/* Linux's FP environment, for every proc made from here on */
	p9_segclean();		/* the pool segments of a crashed predecessor */
	(void)current;		/* the starting proc's task */
	kpi_constructors();	/* the module parameters register themselves */
	kpi_cpu_init();
	kpi_meminit();
	drm_linux_init();
	cold = 0;
	if (!kpi_pci_gpu(gdir)) {
		pr_err("kpi: %s: no PCI device\n", gdir);
		return -ENODEV;
	}
	return 0;
}
