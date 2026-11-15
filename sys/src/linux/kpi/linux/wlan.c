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
	char path[96], ea[32], err[128];
	const u8 *a;
	int fd, r;

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
