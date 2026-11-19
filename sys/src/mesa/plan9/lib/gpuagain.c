/*
 * SPDX-License-Identifier: MIT
 *
 * gpuagain(n): a GPU's files again after its driver was started anew
 * (gpu(4)), as libdraw's wsysagain is a window system's (graphics(2)).
 * A driver posts /srv/gpu.n and mounts itself after /dev in the name space
 * it was started in; a program whose name space still has the mount of a
 * driver that died (wsys, and every window it started since) finds
 * /dev/gpu/n dead.  If /dev/gpu/n/status cannot be opened but /srv/gpu.n
 * can, the driver posted again: its files are mounted before /dev here,
 * over the dead ones.  Returns 1 when it mounted them, 0 when /dev/gpu/n
 * was alive, -1 when there is no driver to mount.  Called before a device
 * is looked for (plan9/drm/drm9.c, Mesa's venus renderer for 9front).
 */
#include <u.h>
#include <libc.h>

#include "gpuagain.h"

int
gpuagain(int n)
{
	char path[64];
	int fd;

	snprint(path, sizeof path, "/dev/gpu/%d/status", n);
	if((fd = open(path, OREAD)) >= 0){
		close(fd);
		return 0;
	}
	snprint(path, sizeof path, "/srv/gpu.%d", n);
	if((fd = open(path, ORDWR)) < 0)
		return -1;
	/* a mount that works closes fd (bind(2)) */
	if(mount(fd, -1, "/dev", MBEFORE, "") < 0){
		close(fd);
		return -1;
	}
	return 1;
}
