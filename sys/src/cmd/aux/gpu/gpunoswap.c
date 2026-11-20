/*
 * SPDX-License-Identifier: MIT
 *
 * gpunoswap: a GPU driver is never the pager's choice when memory runs
 * out.  The pager kills the biggest process (devswap.c killbig), and a
 * driver is big: it holds the GPU's page pool and the pages behind its
 * clients' segments.  Killing it takes the screen and every client's
 * GPU with it, and frees little the pool's watcher would not give back
 * anyway (LinuxKPI, linux/page.c kpi_poolcheck).  proc(3)'s noswap
 * exempts a process of the host owner; it is inherited by fork and
 * cleared by exec, so each driver sets it itself, first thing in main,
 * before any rfork or proccreate: all its procs inherit it, however it
 * was started (gpustart, a restart, by hand).  Included by each
 * driver's main file.
 */

static void
gpunoswap(char *name)
{
	char path[32];
	int fd;

	snprint(path, sizeof path, "/proc/%d/ctl", getpid());
	if((fd = open(path, OWRITE)) < 0){
		fprint(2, "%s: noswap: %r\n", name);
		return;
	}
	if(write(fd, "noswap", 6) != 6)
		fprint(2, "%s: noswap: %r\n", name);
	close(fd);
}
