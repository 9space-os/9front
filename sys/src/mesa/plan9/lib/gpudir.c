/*
 * SPDX-License-Identifier: MIT
 *
 * plan9_gpudir(n): where GPU n's files are for this process (gpu(4)):
 * /shr/gpu.user/n when the hostowner granted the user a share of their
 * own, else /shr/gpu/n (the hostowner's).  The drivers put their files
 * into shr(3)'s shares, so a driver started again is there at once in
 * every name space.  Used by plan9/drm/drm9.c and Mesa's venus renderer
 * for 9front.
 */
#include <u.h>
#include <libc.h>

#include "gpudir.h"

char*
plan9_gpudir(int n, char *buf, int nbuf)
{
	char user[64];
	int fd, m;

	m = 0;
	if((fd = open("/dev/user", OREAD)) >= 0){
		m = read(fd, user, sizeof user - 1);
		close(fd);
	}
	user[m > 0 ? m : 0] = 0;
	snprint(buf, nbuf, "/shr/gpu.%s/%d", user, n);
	if(access(buf, AEXIST) == 0)
		return buf;
	snprint(buf, nbuf, "/shr/gpu/%d", n);
	return buf;
}
