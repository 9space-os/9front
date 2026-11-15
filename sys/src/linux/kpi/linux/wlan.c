// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: a WLAN driver as the radio of a wlan(3)
 * interface.  #ω/wlanN/data gives what the kernel's 802.11 code
 * (port/wifi.c) sends, each read one frame or a tune request after a
 * header (channel[2] rate[1] flags[1] aid[2] bssid[6]); it takes the
 * frames received after a header (channel[2] rssi[1] flags[1] tsf[8]).
 * The header's channel is where the hardware is to be; an association
 * id with an access point is the association wifi.c made (aux/wpa's
 * keys stay wifi.c's: it ciphers); a scan's tune request (to the
 * broadcast address), or the access point again without an id, ends it.
 * The station's address, the hardware's, goes to ctl first.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include <kpi/p9.h>
#include <kpi/linux.h>
#include "kpi80211.h"

enum {
	Hdrtx	= 12,	/* channel[2] rate[1] flags[1] aid[2] bssid[6] */
	Hdrrx	= 12,	/* channel[2] rssi[1] flags[1] tsf[8] */
	Ftune	= 1<<0,	/* tx: no frame, only tune */
	Ftsf	= 1<<0,	/* rx: tsf is the frame's timestamp */
	Maxframe	= 4096,	/* wlan.h's */
};

static char wlandir[64];

/* the channel of wifi.c's number: 1-14 2.4 GHz, else 5 GHz (6 GHz's numbers are 5 GHz's too) */
static struct linuxkpi_ieee80211_channel *
chanof(struct lhw *l, int n)
{
	uint32_t f;

	f = linuxkpi_ieee80211_channel_to_frequency(n, n <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);
	return f ? linuxkpi_ieee80211_get_channel(l->hw.wiphy, f) : NULL;
}

/* what the header asks of the hardware: the channel, the association */
static void
follow(struct lhw *l, const u8 *h)
{
	struct linuxkpi_ieee80211_channel *c;
	const u8 *bssid = h + 6;
	int n, aid;

	n = get_unaligned_le16(h);
	aid = get_unaligned_le16(h + 4) & 0x3fff;
	wiphy_lock(l->hw.wiphy);
	if (aid != 0 && !is_zero_ether_addr(bssid) && !is_broadcast_ether_addr(bssid)) {
		if (!l->assoc || !ether_addr_equal(bssid, l->bssid) || aid != l->aid) {
			if (n && (c = chanof(l, n)) != NULL)
				kpi80211_tune(l, c);
			kpi80211_assoc(l, bssid, aid);
		}
	} else if (l->assoc && (ether_addr_equal(bssid, l->bssid) || is_broadcast_ether_addr(bssid)))
		kpi80211_disassoc(l);
	if (n && (c = chanof(l, n)) != NULL && c != l->hw.conf.chandef.chan)
		kpi80211_tune(l, c);
	wiphy_unlock(l->hw.wiphy);
}

static int
radio(void *arg)
{
	struct lhw *l = arg;
	struct sk_buff *skb;
	char err[128];
	u8 *buf;
	long n;
	int i;

	buf = kmalloc(Hdrtx + Maxframe, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	for (;;) {
		n = p9_read(l->datafd, buf, Hdrtx + Maxframe);
		if (n < 0) {
			p9_errstr(err, sizeof(err));
			pr_err("wlan: %s/data: %s\n", wlandir, err);
			break;
		}
		if (n < Hdrtx)
			continue;
		follow(l, buf);
		if ((buf[3] & Ftune) || n == Hdrtx)
			continue;
		/* the driver stopped its queues: it is full or resetting */
		for (i = 0; READ_ONCE(l->stopped) && i < 100; i++)
			msleep(10);
		skb = dev_alloc_skb(l->hw.extra_tx_headroom + n - Hdrtx);
		if (skb == NULL) {
			l->ndrop++;
			continue;
		}
		skb_reserve(skb, l->hw.extra_tx_headroom);
		skb_put_data(skb, buf + Hdrtx, n - Hdrtx);
		kpi80211_tx(l, skb);
	}
	kfree(buf);
	return 0;
}

/*
 * the band the radio's capabilities come from: 5 GHz's where it has
 * one (the same HT on both for rtw89), else 2.4 GHz's
 */
static struct ieee80211_supported_band *
capband(struct lhw *l)
{
	struct ieee80211_supported_band *sb;

	if ((sb = l->hw.wiphy->bands[NL80211_BAND_5GHZ]) != NULL && sb->ht_cap.ht_supported)
		return sb;
	if ((sb = l->hw.wiphy->bands[NL80211_BAND_2GHZ]) != NULL && sb->ht_cap.ht_supported)
		return sb;
	return NULL;
}

/*
 * the radio's capability elements for wlan(3)'s ctl (caps hex), which
 * wifi.c sends in the association request: the HT capabilities element
 * of the driver's band (802.11-2020 9.4.2.55), with 40 MHz where it has
 * it (kpi80211_assoc follows the access point's HT operation).  No VHT
 * or HE yet: a VHT station is one of 80 MHz.
 */
static int
capsline(struct lhw *l, char *buf, int n)
{
	struct ieee80211_supported_band *sb;
	struct ieee80211_ht_cap c;
	u8 e[2 + sizeof(c)];
	int i, k;

	if ((sb = capband(l)) == NULL)
		return 0;
	memset(&c, 0, sizeof(c));
	c.cap_info = cpu_to_le16(sb->ht_cap.cap & ~IEEE80211_HT_CAP_DSSSCCK40);
	c.ampdu_params_info = (sb->ht_cap.ampdu_factor & 3) | (sb->ht_cap.ampdu_density & 7) << 2;
	c.mcs = sb->ht_cap.mcs;
	e[0] = WLAN_EID_HT_CAPABILITY;
	e[1] = sizeof(c);
	memcpy(e + 2, &c, sizeof(c));
	k = snprintf(buf, n, "caps ");
	for (i = 0; i < (int)sizeof(e) && k + 2 < n; i++)
		k += snprintf(buf + k, n - k, "%02x", e[i]);
	return k;
}

/*
 * an association response to us: the access point's HT capabilities
 * and WMM (its parameter element), for the station kpi80211_assoc makes
 */
static void
apcaps(struct lhw *l, const u8 *f, int len)
{
	const u8 *p, *e;

	if (len < 24 + 6 || (f[0] != 0x10 && f[0] != 0x30) ||
	    !ether_addr_equal(f + 4, l->hw.wiphy->perm_addr))
		return;
	l->apht = false;
	l->apwmm = false;
	l->apsec = 0;
	e = f + len;
	for (p = f + 24 + 6; p + 2 <= e && p + 2 + p[1] <= e; p += 2 + p[1]) {
		if (p[0] == WLAN_EID_HT_CAPABILITY && p[1] >= sizeof(l->apcap)) {
			memcpy(&l->apcap, p + 2, sizeof(l->apcap));
			l->apht = true;
		}
		/* HT operation (61): the secondary channel's offset where any width is allowed */
		if (p[0] == 61 && p[1] >= 2 && (p[3] & 4) != 0)
			l->apsec = p[3] & 3;
		if (p[0] == 221 && p[1] >= 7 && p[2] == 0x00 && p[3] == 0x50 && p[4] == 0xf2 &&
		    p[5] == 0x02 && p[6] == 0x01)
			l->apwmm = true;
	}
}

/*
 * a frame of the driver's to wifi.c: without the FCS the hardware
 * keeps, none that failed its checks, none on a channel wifi.c cannot
 * name (6 GHz)
 */
void
kpi_wlan_rx(struct lhw *l, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rs = IEEE80211_SKB_RXCB(skb);
	u8 *h;
	int len, ch;

	if (l->datafd < 0 || (rs->flag & (RX_FLAG_FAILED_FCS_CRC | RX_FLAG_FAILED_PLCP_CRC)) ||
	    rs->band == NL80211_BAND_6GHZ)
		goto drop;
	len = skb->len;
	if (ieee80211_hw_check(&l->hw, RX_INCLUDES_FCS))
		len -= FCS_LEN;
	if (len < 10 || len > Maxframe)
		goto drop;
	ch = linuxkpi_ieee80211_frequency_to_channel(rs->freq, 0);
	apcaps(l, skb->data, len);
	kpi80211_ba(l, skb->data, len, false);
	if (skb_headroom(skb) < Hdrrx) {
		/* the driver's buffers have room before them; else a copy */
		struct sk_buff *n = dev_alloc_skb(Hdrrx + len);

		if (n == NULL)
			goto drop;
		skb_reserve(n, Hdrrx);
		skb_put_data(n, skb->data, len);
		memcpy(n->cb, skb->cb, sizeof(n->cb));
		dev_kfree_skb_any(skb);
		skb = n;
		rs = IEEE80211_SKB_RXCB(skb);
	}
	h = skb_push(skb, Hdrrx);
	put_unaligned_le16(ch, h);
	h[2] = (rs->flag & RX_FLAG_NO_SIGNAL_VAL) ? 0 : (u8)rs->signal;
	h[3] = (rs->flag & (RX_FLAG_MACTIME_START | RX_FLAG_MACTIME_END)) ? Ftsf : 0;
	put_unaligned_le64(rs->mactime, h + 4);
	if (p9_write(l->datafd, h, Hdrrx + len) != Hdrrx + len)
		l->ndrop++;
	else
		l->nrx++;
	dev_kfree_skb_any(skb);
	return;
drop:
	l->ndrop++;
	dev_kfree_skb_any(skb);
}

/* the radio of wlan(3)'s dir (#ω/wlan1): the address, the hardware up, the frames between */
int
kpi_wlan_start(const char *dir)
{
	struct lhw *l = kpi80211_hw();
	struct task_struct *t;
	char path[96], ea[32], err[128], caps[160];
	const u8 *a;
	int fd, r, n;

	if (l == NULL || !l->registered)
		return -ENODEV;
	strscpy(wlandir, dir, sizeof(wlandir));
	a = l->hw.wiphy->perm_addr;
	if (!is_valid_ether_addr(a)) {
		pr_err("wlan: the hardware has no address\n");
		return -EINVAL;
	}
	snprintf(path, sizeof(path), "%s/ctl", dir);
	if ((fd = p9_open(path, P9_OWRITE)) < 0)
		goto err;
	snprintf(ea, sizeof(ea), "ea %02x%02x%02x%02x%02x%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
	r = p9_write(fd, ea, strlen(ea));
	/* the capabilities for the association request (wifi.c sends those the AP has too) */
	if (r >= 0 && (n = capsline(l, caps, sizeof(caps))) > 0)
		r = p9_write(fd, caps, n);
	p9_close(fd);
	if (r < 0)
		goto err;
	snprintf(path, sizeof(path), "%s/data", dir);
	if ((l->datafd = p9_open(path, P9_ORDWR)) < 0)
		goto err;
	wiphy_lock(l->hw.wiphy);
	r = kpi80211_up(l);
	wiphy_unlock(l->hw.wiphy);
	if (r)
		return r;
	t = kthread_run(radio, l, "wlanradio");
	if (IS_ERR(t))
		return PTR_ERR(t);
	return 0;
err:
	p9_errstr(err, sizeof(err));
	pr_err("wlan: %s: %s\n", path, err);
	return -EIO;
}

/* the state, a line into buf; its length */
int
kpi_wlan_status(char *buf, int n)
{
	struct lhw *l = kpi80211_hw();
	struct linuxkpi_ieee80211_channel *c;

	if (l == NULL)
		return scnprintf(buf, n, "no hardware\n");
	c = l->hw.conf.chandef.chan;
	return scnprintf(buf, n, "radio %s addr %pM %s channel %d (%u MHz) %s%s%pM aid %d tx %lu rx %lu drop %lu\n",
	    wlandir, l->hw.wiphy->perm_addr, l->started ? "started" : "stopped",
	    c ? (int)linuxkpi_ieee80211_frequency_to_channel(c->center_freq, 0) : 0,
	    c ? c->center_freq : 0, l->scanning ? "scanning " : "",
	    l->assoc ? "bss " : "no bss ", l->bssid, l->aid, l->ntx, l->nrx, l->ndrop);
}
