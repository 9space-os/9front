/* SPDX-License-Identifier: MIT */
/*
 * LinuxKPI for 9front: the ARP header (RFC 826) as the drivers build it
 * (rtw89's ARP response offload).
 */
#ifndef _KPI_LINUX_IF_ARP_H
#define _KPI_LINUX_IF_ARP_H

#include <linux/types.h>
#include <linux/netdevice.h>

#define	ARPHRD_ETHER	1	/* hardware: Ethernet */
#define	ARPOP_REQUEST	1
#define	ARPOP_REPLY	2

struct arphdr {
	__be16	ar_hrd;		/* hardware address space */
	__be16	ar_pro;		/* protocol address space */
	u8	ar_hln;		/* their lengths */
	u8	ar_pln;
	__be16	ar_op;
};

#endif
