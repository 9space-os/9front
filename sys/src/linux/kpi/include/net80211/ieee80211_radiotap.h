/* SPDX-License-Identifier: MIT */
/*
 * LinuxKPI for 9front: FreeBSD's net80211 radiotap definitions (BSD-3,
 * imported unchanged in sys/src/linux/freebsd), whose numbers FreeBSD's
 * net/ieee80211_radiotap.h uses.
 */
#ifndef _KPI_NET80211_IEEE80211_RADIOTAP_H
#define _KPI_NET80211_IEEE80211_RADIOTAP_H

#include <linux/types.h>
#include <sys/types.h>
/* as FreeBSD's kernel sees it (its LinuxKPI is built with it) */
#ifndef _KERNEL
#define _KERNEL
#define _KPI_NET80211_KERNEL
#endif
#include "../../../freebsd/freebsd/sys/net80211/ieee80211_radiotap.h"
#ifdef _KPI_NET80211_KERNEL
#undef _KERNEL
#undef _KPI_NET80211_KERNEL
#endif

#endif
