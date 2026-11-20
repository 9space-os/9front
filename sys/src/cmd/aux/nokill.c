/*
 * SPDX-License-Identifier: MIT
 *
 * nokill: a program the system needs is never the pager's choice when
 * memory runs out (proc(3) nokill: the pager kills the biggest process
 * it may, devswap.c killbig), without noswap's claim on the last free
 * pages (it frees no memory for others: it waits for pages as any
 * process).  It is inherited by fork and cleared by exec, so the
 * program sets it itself, first thing in main, before any rfork or
 * proccreate: all its procs inherit it, however it was started (at
 * boot, restarted, by hand); dontkill(1) at boot only reaches what runs
 * then.  The GPU drivers (gpu(4): a driver holds the screen and every
 * client's GPU), aux/rtw89 and aux/wpa (the WLAN, restarted at run
 * time).  Included by each one's main file.
 */

static void
nokill(char *name)
{
	char path[32];
	int fd;

	snprint(path, sizeof path, "/proc/%d/ctl", getpid());
	if((fd = open(path, OWRITE)) < 0){
		fprint(2, "%s: nokill: %r\n", name);
		return;
	}
	if(write(fd, "nokill", 6) != 6)
		fprint(2, "%s: nokill: %r\n", name);
	close(fd);
}
