/* SPDX-License-Identifier: MIT */
/* LinuxKPI for 9front: linux/devcoredump.h (ours, not Linux's) */
#ifndef _KPI_LINUX_DEVCOREDUMP_H
#define _KPI_LINUX_DEVCOREDUMP_H

#include <linux/device.h>
#include <linux/vmalloc.h>

/* 9front: no devcoredump: the data is dropped (rtw89 gives it to vfree) */
static inline void
dev_coredumpv(struct device *dev, void *data, size_t datalen, gfp_t gfp)
{
	vfree(data);
}



#endif
