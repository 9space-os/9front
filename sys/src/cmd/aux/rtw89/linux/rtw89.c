// SPDX-License-Identifier: MIT
/*
 * aux/rtw89: the Linux side, where Linux's names are seen: rtw89's PCI
 * driver for the RTL8852CE (rtw8852ce.c's module_pci_driver) registered,
 * which probes the device of #G.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include "../rtw89.h"

int	kpi_init_rtw89_8852ce_driver(void);	/* rtw8852ce.c */
struct pci_dev	*kpi_pci_gpu(const char *gdir);
extern int kpi_loglevel;

void
rtw89_options(int loglevel)
{
	kpi_loglevel = loglevel;
}

int
rtw89_start(void)
{
	struct pci_dev *p;
	int r;

	if ((r = kpi_init_rtw89_8852ce_driver()) < 0)
		return r;
	/* registered without a probe: the device is none of rtw89's */
	p = kpi_pci_gpu(NULL);
	if (p == NULL || p->driver == NULL) {
		pr_err("rtw89: %04x:%04x is no RTL8852CE\n", p ? p->vendor : 0, p ? p->device : 0);
		return -ENODEV;
	}
	return 0;
}
