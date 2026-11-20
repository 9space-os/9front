/*
 * SPDX-License-Identifier: MIT
 *
 * aux/rtw89: Realtek's rtw89 WLAN driver (FreeBSD's copy, under its
 * BSD-3-Clause choice) on the LinuxKPI (sys/src/linux) for the
 * RTL8852CE, as the radio of a wlan(3) interface.  The card is a device
 * of #G (plan9.ini's *userpci=10ec/c852, gpu(3)); the kernel's 802.11
 * code behind #ω/wlanN (ether type wlan) scans, associates and ciphers,
 * aux/wpa does WPA.  This starts the KPI, rtw89's PCI driver (the
 * efuse, the firmware from /lib/firmware/rtw89), and the radio
 * (kpi/linux/wlan.c): the card's address to #ω/wlanN/ctl, the frames
 * between #ω/wlanN/data and the card.
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <ureg.h>
#include <kpi/linux.h>
#include "rtw89.h"
#include "../nokill.c"

/* the Linux code runs on the procs' stacks, as aux/amdgpu's: Linux gives its code 16 KB */
int	mainstacksize = 256*1024;

/* the first device of #G that is Realtek's (vendor 10ec): its "id" line in ctl */
static char*
finddev(void)
{
	static char dir[32];
	char buf[512], *p;
	int i, fd, n;

	for(i = 0; i < 8; i++){
		snprint(dir, sizeof dir, "#G/%d", i);
		snprint(buf, sizeof buf, "%s/ctl", dir);
		if((fd = open(buf, OREAD)) < 0)
			break;
		n = read(fd, buf, sizeof buf - 1);
		close(fd);
		if(n <= 0)
			continue;
		buf[n] = 0;
		if((p = strstr(buf, "\nid ")) != nil && strncmp(p + 4, "10ec/", 5) == 0)
			return dir;
		if(strncmp(buf, "id 10ec/", 8) == 0)
			return dir;
	}
	return nil;
}

/* the first interface of wlan(3) */
static char*
findwlan(void)
{
	static char dir[64];
	Dir *d;
	int fd, i, n;

	if((fd = open("#ω", OREAD)) < 0)
		return nil;
	n = dirreadall(fd, &d);
	close(fd);
	for(i = 0; i < n; i++)
		if(strncmp(d[i].name, "wlan", 4) == 0){
			snprint(dir, sizeof dir, "#ω/%s", d[i].name);
			free(d);
			return dir;
		}
	free(d);
	return nil;
}

/* a trap in any proc (they inherit the handler): the note and the pc */
static int
trapnote(void *v, char *note)
{
	struct Ureg *u;

	if(strncmp(note, "sys:", 4) != 0)
		return 0;
	u = v;
	fprint(2, "rtw89 %d: %s pc %#llux sp %#llux\n", getpid(), note, (uvlong)u->pc, (uvlong)u->sp);
	return 0;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-l loglevel] [-m memlimit] [-p name=value]... [-g #G/n] [-w #ω/wlanN]\n", argv0);
	threadexitsall("usage");
}

/* the LinuxKPI's skbuff.c: FreeBSD's compat.linuxkpi.skb.mem_limit */
extern int kpi_skb_memlimit;

void
threadmain(int argc, char **argv)
{
	char *gdir, *wdir, *param[32], status[256];
	int r, i, nparam, loglevel;

	gdir = nil;
	wdir = nil;
	nparam = 0;
	loglevel = 6;	/* KERN_INFO */
	/*
	 * the skbs' data below 4 GB: the card does not fetch frames above
	 * (the firmware download never arrived), as FreeBSD's rtw89(4)
	 * says of machines with more than 4 GB (its BUGS)
	 */
	kpi_skb_memlimit = 1;
	nokill("rtw89");	/* before the kpi's procs (nokill.c) */
	ARGBEGIN{
	case 'm':
		kpi_skb_memlimit = atoi(EARGF(usage()));
		break;
	case 'l':
		loglevel = atoi(EARGF(usage()));
		break;
	case 'p':
		if(nparam >= nelem(param))
			sysfatal("too many -p");
		param[nparam++] = EARGF(usage());	/* rtw89's module parameters: debug_mask=0x..., disable_aspm_l1=1 */
		break;
	case 'g':
		gdir = EARGF(usage());
		break;
	case 'w':
		wdir = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	if(gdir == nil && (gdir = finddev()) == nil)
		sysfatal("no Realtek device in #G (plan9.ini: *userpci=10ec/c852)");
	if(wdir == nil && (wdir = findwlan()) == nil)
		sysfatal("no wlan(3) interface in #ω (plan9.ini: etherN=type=wlan)");

	atnotify(trapnote, 1);
	rtw89_options(loglevel);
	if((r = kpi_start(gdir)) < 0)
		sysfatal("kpi: %s: error %d", gdir, -r);
	for(i = 0; i < nparam; i++)
		if((r = kpi_param_set(param[i])) < 0)
			sysfatal("-p %s: error %d", param[i], -r);
	if((r = rtw89_start()) < 0)
		sysfatal("rtw89: error %d", -r);
	if((r = kpi_wlan_start(wdir)) < 0)
		sysfatal("%s: error %d", wdir, -r);
	kpi_wlan_status(status, sizeof status);
	fprint(2, "rtw89: %s", status);
	/* the driver's procs (the radio, workqueues, irq, timers) go on */
	threadexits(nil);
}
