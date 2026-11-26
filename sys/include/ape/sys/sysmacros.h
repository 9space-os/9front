/*
 * APE (GCC port): <sys/sysmacros.h> as on Linux: major, minor and makedev
 * of a dev_t (APE's stat: the device's number; there are no device
 * numbers of this kind on 9front).
 */
#ifndef __APE_SYS_SYSMACROS_H
#define __APE_SYS_SYSMACROS_H
/* 9space: as dirtostat.c makes st_dev: (type<<16)|dev */
#define major(d)	((unsigned)(d) >> 16 & 0xFFFF)
#define minor(d)	((unsigned)(d) & 0xFFFF)
#define makedev(x, y)	((dev_t)(((x) & 0xFFFF) << 16 | ((y) & 0xFFFF)))
#endif
