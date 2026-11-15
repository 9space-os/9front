// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: a small mac80211 of our own, written from what
 * the drivers call (rtw89) and the interface of FreeBSD's net/mac80211.h
 * and net/cfg80211.h (their linuxkpi_ functions are these).  Not Linux's
 * mac80211: 9front's own 802.11 code (port/wifi.c, behind wlan(3))
 * scans, authenticates, associates and ciphers; this layer only drives
 * the hardware for it, one station interface with one access point:
 *
 *	up		start, add_interface, the receive filter, the
 *			queues' parameters, the channel (a channel context
 *			the interface keeps; a driver without them: the
 *			hardware's by config), a software scan
 *	tune		the channel (the context changed)
 *	assoc		the scan ends; the access point's BSS and station
 *			(none, auth, assoc, authorized), vif_cfg assoc
 *	disassoc	back, and scanning again
 *	tx		a frame wifi.c made, given to the driver's tx
 *	rx		the driver's frames to wlan.c (kpi_wlan_rx)
 *
 * No keys reach the driver (set_key is never called: wifi.c ciphers),
 * no aggregation, no power save, no hardware scan; the driver's rate
 * control chooses the rates.  Transmit status is dropped.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include "kpi80211.h"

int linuxkpi_debug_80211;
const unsigned char rfc1042_header[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };

static struct lhw *thehw;

/* the access category of a TID (802.1D user priority, 802.11 Table 10-1) */
static int
ac_from_tid(int tid)
{
	static const u8 ac[8] = {
		IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK, IEEE80211_AC_BE,
		IEEE80211_AC_VI, IEEE80211_AC_VI, IEEE80211_AC_VO, IEEE80211_AC_VO,
	};

	return ac[tid & 7];
}

struct lhw *
kpi80211_hw(void)
{
	return thehw;
}

/* -------------------------------------------------------------------------- */
/* the wiphy and its works */

static void
wwk_run(struct work_struct *w)
{
	struct lhw *l = container_of(w, struct lhw, wwk_work);
	struct wiphy *wiphy = l->hw.wiphy;
	struct wiphy_work *wk;

	wiphy_lock(wiphy);
	for (;;) {
		spin_lock(&l->wwk_lock);
		if (list_empty(&l->wwk_list)) {
			spin_unlock(&l->wwk_lock);
			break;
		}
		wk = list_first_entry(&l->wwk_list, struct wiphy_work, entry);
		list_del_init(&wk->entry);
		spin_unlock(&l->wwk_lock);
		wk->fn(wiphy, wk);
	}
	wiphy_unlock(wiphy);
}

void
linuxkpi_wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct lhw *l = hw_to_lhw(wiphy_to_ieee80211_hw(wiphy));

	spin_lock(&l->wwk_lock);
	if (list_empty(&wk->entry))
		list_add_tail(&wk->entry, &l->wwk_list);
	spin_unlock(&l->wwk_lock);
	queue_work(l->wwk_wq, &l->wwk_work);
}

/* with the wiphy's lock held: a work not run yet is not run */
void
linuxkpi_wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct lhw *l = hw_to_lhw(wiphy_to_ieee80211_hw(wiphy));

	spin_lock(&l->wwk_lock);
	list_del_init(&wk->entry);
	spin_unlock(&l->wwk_lock);
}

/* with the wiphy's lock held: a work queued runs now */
void
linuxkpi_wiphy_work_flush(struct wiphy *wiphy, struct wiphy_work *wk)
{
	struct lhw *l = hw_to_lhw(wiphy_to_ieee80211_hw(wiphy));
	bool queued;

	if (wk == NULL)
		return;
	spin_lock(&l->wwk_lock);
	queued = !list_empty(&wk->entry);
	list_del_init(&wk->entry);
	spin_unlock(&l->wwk_lock);
	if (queued)
		wk->fn(wiphy, wk);
}

void
lkpi_wiphy_delayed_work_timer(struct timer_list *t)
{
	struct wiphy_delayed_work *dw = container_of(t, struct wiphy_delayed_work, timer);

	linuxkpi_wiphy_work_queue(dw->wiphy, &dw->work);
}

void
linuxkpi_wiphy_delayed_work_queue(struct wiphy *wiphy, struct wiphy_delayed_work *dw,
    unsigned long delay)
{
	dw->wiphy = wiphy;
	if (delay == 0) {
		timer_delete(&dw->timer);
		linuxkpi_wiphy_work_queue(wiphy, &dw->work);
		return;
	}
	mod_timer(&dw->timer, jiffies + delay);
}

void
linuxkpi_wiphy_delayed_work_cancel(struct wiphy *wiphy, struct wiphy_delayed_work *dw)
{
	timer_delete_sync(&dw->timer);
	linuxkpi_wiphy_work_cancel(wiphy, &dw->work);
}

void
linuxkpi_wiphy_delayed_work_flush(struct wiphy *wiphy, struct wiphy_delayed_work *dw)
{
	timer_delete_sync(&dw->timer);
	linuxkpi_wiphy_work_flush(wiphy, &dw->work);
}

int
linuxkpi_80211_wiphy_register(struct wiphy *wiphy)
{
	return 0;
}

void
linuxkpi_wiphy_free(struct wiphy *wiphy)
{
	kfree(wiphy);
}

struct ieee80211_hw *
linuxkpi_wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	return &((struct lhw *)wiphy->priv)->hw;
}

struct linuxkpi_ieee80211_regdomain *
lkpi_get_linuxkpi_ieee80211_regdomain(size_t nrules)
{
	return kzalloc(sizeof(struct linuxkpi_ieee80211_regdomain) +
	    nrules * sizeof(struct ieee80211_reg_rule), GFP_KERNEL);
}

/* -------------------------------------------------------------------------- */
/* the hardware */

static void	restart_work(struct work_struct *w);
static void	ba_work(struct work_struct *w);
static void	bw_work(struct work_struct *w);
static void	chanctxdown(struct lhw *l);

/*
 * the wiphy with the hardware in its private area (ieee80211_free_hw
 * frees the wiphy last), the driver's area at the end
 */
struct ieee80211_hw *
linuxkpi_ieee80211_alloc_hw(size_t priv_len, const struct ieee80211_ops *ops)
{
	struct wiphy *wiphy;
	struct lhw *l;

	wiphy = kzalloc(sizeof(*wiphy) + sizeof(*l) + priv_len, GFP_KERNEL);
	if (wiphy == NULL)
		return NULL;
	mutex_init(&wiphy->mtx);
	INIT_LIST_HEAD(&wiphy->wdev_list);
	wiphy->rts_threshold = (uint32_t)-1;
	wiphy->frag_threshold = (uint32_t)-1;
	l = (struct lhw *)wiphy->priv;
	l->ops = ops;
	l->hw.wiphy = wiphy;
	l->hw.priv = l->priv;
	l->hw.queues = IEEE80211_NUM_ACS;
	l->hw.conf.listen_interval = 1;
	l->datafd = -1;
	spin_lock_init(&l->wwk_lock);
	INIT_LIST_HEAD(&l->wwk_list);
	INIT_WORK(&l->wwk_work, wwk_run);
	INIT_WORK(&l->restart_work, restart_work);
	INIT_WORK(&l->ba_work, ba_work);
	INIT_WORK(&l->bw_work, bw_work);
	spin_lock_init(&l->ba_lock);
	INIT_LIST_HEAD(&l->ba_list);
	l->wq = alloc_ordered_workqueue("mac80211", 0);
	l->wwk_wq = alloc_ordered_workqueue("wiphy", 0);
	if (l->wq == NULL || l->wwk_wq == NULL) {
		if (l->wq)
			destroy_workqueue(l->wq);
		if (l->wwk_wq)
			destroy_workqueue(l->wwk_wq);
		kfree(wiphy);
		return NULL;
	}
	thehw = l;
	return &l->hw;
}

void
linuxkpi_set_ieee80211_dev(struct ieee80211_hw *hw)
{
}

int
linuxkpi_ieee80211_ifattach(struct ieee80211_hw *hw)
{
	struct wiphy *wiphy = hw->wiphy;
	struct regulatory_request req;

	hw_to_lhw(hw)->registered = true;
	pr_info("mac80211: %pM registered, %d queues\n", wiphy->perm_addr, hw->queues);
	/*
	 * the regulatory domain, as cfg80211's wiphy_register tells a new
	 * wiphy the last request: the core's world domain ("00") here, there
	 * being no user or country to ask (rtw89 sets its domain from it;
	 * FreeBSD, whose LinuxKPI does not call it, sets it in rtw89 itself)
	 */
	if (wiphy->reg_notifier != NULL && !(wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED)) {
		memset(&req, 0, sizeof(req));
		req.alpha2[0] = '0';
		req.alpha2[1] = '0';
		req.initiator = NL80211_REGDOM_SET_BY_CORE;
		wiphy->reg_notifier(wiphy, &req);
	}
	return 0;
}

void
linuxkpi_ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
	struct lhw *l = hw_to_lhw(hw);

	wiphy_lock(hw->wiphy);
	if (l->assoc)
		kpi80211_disassoc(l);
	if (l->vif != NULL)
		chanctxdown(l);
	if (l->vif != NULL && l->ops->remove_interface)
		l->ops->remove_interface(hw, l->vif);
	if (l->started && l->ops->stop)
		l->ops->stop(hw, false);
	l->started = false;
	l->registered = false;
	wiphy_unlock(hw->wiphy);
}

static void
freetxq(struct ieee80211_txq *q)
{
	kfree(q);
}

void
linuxkpi_ieee80211_iffree(struct ieee80211_hw *hw)
{
	struct lhw *l = hw_to_lhw(hw);

	flush_workqueue(l->wwk_wq);
	flush_workqueue(l->wq);
	destroy_workqueue(l->wwk_wq);
	destroy_workqueue(l->wq);
	if (l->vif != NULL) {
		freetxq(l->vif->txq);
		kfree(l->vif);
		l->vif = NULL;
	}
	if (thehw == l)
		thehw = NULL;
}

/* SER, the driver's recovery: the hardware set up again as it was */
static void
restart_work(struct work_struct *w)
{
	struct lhw *l = container_of(w, struct lhw, restart_work);
	u8 bssid[ETH_ALEN];
	u16 aid;
	bool assoc;

	wiphy_lock(l->hw.wiphy);
	pr_info("mac80211: restarting the hardware\n");
	assoc = l->assoc;
	memcpy(bssid, l->bssid, ETH_ALEN);
	aid = l->aid;
	/* the driver's state is gone: ours goes too, without calling it */
	if (l->sta != NULL) {
		int i;

		for (i = 0; i < nitems(l->sta->txq); i++)
			freetxq(l->sta->txq[i]);
		kfree(l->sta);
		l->sta = NULL;
	}
	kfree(l->ctx);
	l->ctx = NULL;
	l->assoc = false;
	l->started = false;
	l->scanning = false;
	if (kpi80211_up(l) == 0) {
		if (assoc)
			kpi80211_assoc(l, bssid, aid);
		if (l->ops->reconfig_complete)
			l->ops->reconfig_complete(&l->hw, IEEE80211_RECONFIG_TYPE_RESTART);
	}
	wiphy_unlock(l->hw.wiphy);
}

void
linuxkpi_ieee80211_restart_hw(struct ieee80211_hw *hw)
{
	queue_work(hw_to_lhw(hw)->wq, &hw_to_lhw(hw)->restart_work);
}

void
linuxkpi_ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *w)
{
	queue_work(hw_to_lhw(hw)->wq, w);
}

void
linuxkpi_ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *w, int delay)
{
	queue_delayed_work(hw_to_lhw(hw)->wq, w, delay);
}

/* -------------------------------------------------------------------------- */
/* interfaces, stations, keys: the one of each there is */

void
linuxkpi_ieee80211_iterate_interfaces(struct ieee80211_hw *hw, enum ieee80211_iface_iter flags,
    void (*fn)(void *, uint8_t *, struct ieee80211_vif *), void *arg)
{
	struct lhw *l = hw_to_lhw(hw);

	if (l->vif != NULL && l->started)
		fn(arg, l->vif->addr, l->vif);
}

void
linuxkpi_ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
    void (*fn)(void *, struct ieee80211_sta *), void *arg)
{
	struct lhw *l = hw_to_lhw(hw);

	if (l->sta != NULL)
		fn(arg, l->sta);
}

void
linuxkpi_ieee80211_iterate_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*fn)(struct ieee80211_hw *, struct ieee80211_vif *, struct ieee80211_sta *,
	struct ieee80211_key_conf *, void *), void *arg, bool rcu)
{
	/* no keys: wifi.c ciphers */
}

struct ieee80211_sta *
linuxkpi_ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
	struct lhw *l = thehw;

	if (l == NULL || l->vif != vif || l->sta == NULL)
		return NULL;
	return ether_addr_equal(l->sta->addr, addr) ? l->sta : NULL;
}

void
linuxkpi_ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	pr_info("mac80211: beacon loss\n");
}

void
linuxkpi_ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	pr_info("mac80211: connection loss\n");
}

/* a hardware scan's end: not used (wifi.c scans) */
void
linuxkpi_ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{
}

int
linuxkpi_ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, uint8_t tid, int timeout)
{
	return -EOPNOTSUPP;	/* no aggregation */
}

/* -------------------------------------------------------------------------- */
/* the queues: frames go to the driver's tx, none wait in txqs */

struct sk_buff *
linuxkpi_ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	return NULL;
}

void
linuxkpi_ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frames, unsigned long *bytes)
{
	if (frames)
		*frames = 0;
	if (bytes)
		*bytes = 0;
}

void
linuxkpi_ieee80211_txq_schedule_start(struct ieee80211_hw *hw, uint8_t ac)
{
}

struct ieee80211_txq *
linuxkpi_ieee80211_next_txq(struct ieee80211_hw *hw, uint8_t ac)
{
	return NULL;
}

void
linuxkpi_ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq, bool withoutpkts)
{
}

void
linuxkpi_ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	WRITE_ONCE(hw_to_lhw(hw)->stopped, true);
}

void
linuxkpi_ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	WRITE_ONCE(hw_to_lhw(hw)->stopped, false);
}

void
linuxkpi_ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

void
linuxkpi_ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb, int status)
{
	dev_kfree_skb_any(skb);
}

/* -------------------------------------------------------------------------- */
/* frames the driver has the firmware send (power save, scan offload) */

static struct sk_buff *
mkframe(struct ieee80211_hw *hw, size_t len, size_t tailroom)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(hw->extra_tx_headroom + len + tailroom);
	if (skb == NULL)
		return NULL;
	skb_reserve(skb, hw->extra_tx_headroom);
	memset(skb_put(skb, len), 0, len);
	return skb;
}

struct sk_buff *
linuxkpi_ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    int link_id, bool qos_ok)
{
	struct lhw *l = hw_to_lhw(hw);
	struct ieee80211_hdr_3addr *h;
	struct sk_buff *skb;

	if (!l->assoc)
		return NULL;
	skb = mkframe(hw, sizeof(*h), 0);
	if (skb == NULL)
		return NULL;
	h = (struct ieee80211_hdr_3addr *)skb->data;
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_FC0_SUBTYPE_NODATA |
	    IEEE80211_FCTL_TODS);
	memcpy(h->addr1, l->bssid, ETH_ALEN);
	memcpy(h->addr2, vif->addr, ETH_ALEN);
	memcpy(h->addr3, l->bssid, ETH_ALEN);
	return skb;
}

/* PS-Poll: frame control, the association id (top bits set), BSSID, sender */
struct sk_buff *
linuxkpi_ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct lhw *l = hw_to_lhw(hw);
	struct sk_buff *skb;
	u8 *p;

	if (!l->assoc)
		return NULL;
	skb = mkframe(hw, 16, 0);
	if (skb == NULL)
		return NULL;
	p = skb->data;
	put_unaligned_le16(IEEE80211_FTYPE_CTL | IEEE80211_FC0_SUBTYPE_PS_POLL, p);
	put_unaligned_le16(l->aid | 0xc000, p + 2);
	memcpy(p + 4, l->bssid, ETH_ALEN);
	memcpy(p + 10, vif->addr, ETH_ALEN);
	return skb;
}

/* a broadcast probe request with the SSID element, room for more after it */
struct sk_buff *
linuxkpi_ieee80211_probereq_get(struct ieee80211_hw *hw, const uint8_t *addr,
    const uint8_t *ssid, size_t ssid_len, size_t tailroom)
{
	struct ieee80211_hdr_3addr *h;
	struct sk_buff *skb;
	u8 *p;

	skb = mkframe(hw, sizeof(*h), 2 + ssid_len + tailroom);
	if (skb == NULL)
		return NULL;
	h = (struct ieee80211_hdr_3addr *)skb->data;
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(h->addr1);
	memcpy(h->addr2, addr, ETH_ALEN);
	eth_broadcast_addr(h->addr3);
	p = skb_put(skb, 2 + ssid_len);
	p[0] = WLAN_EID_SSID;
	p[1] = ssid_len;
	if (ssid_len)
		memcpy(p + 2, ssid, ssid_len);
	return skb;
}

uint8_t
linuxkpi_ieee80211_get_tid(struct ieee80211_hdr *h, bool nonqos_ok)
{
	if (!ieee80211_is_data_qos(h->frame_control))
		return nonqos_ok ? IEEE80211_NUM_TIDS : 0;	/* the non-QoS TID */
	return *ieee80211_get_qos_ctl(h) & IEEE80211_QOS_CTL_TID_MASK;
}

/* -------------------------------------------------------------------------- */
/* channels and rates */

uint32_t
linuxkpi_ieee80211_channel_to_frequency(uint32_t c, enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ:
		if (c == 14)
			return 2484;
		return c >= 1 && c < 14 ? 2407 + 5 * c : 0;
	case NL80211_BAND_5GHZ:
		return c >= 1 && c <= 196 ? 5000 + 5 * c : 0;
	case NL80211_BAND_6GHZ:
		if (c == 2)
			return 5935;
		return c >= 1 && c <= 233 ? 5950 + 5 * c : 0;
	default:
		return 0;
	}
}

uint32_t
linuxkpi_ieee80211_frequency_to_channel(uint32_t freq, uint32_t flags)
{
	if (freq == 2484)
		return 14;
	if (freq > 2407 && freq < 2484)
		return (freq - 2407) / 5;
	if (freq == 5935)
		return 2;
	if (freq > 5950 && freq <= 7115)
		return (freq - 5950) / 5;
	if (freq >= 5000 && freq <= 5980)
		return (freq - 5000) / 5;
	return 0;
}

struct linuxkpi_ieee80211_channel *
linuxkpi_ieee80211_get_channel(struct wiphy *wiphy, uint32_t freq)
{
	struct ieee80211_supported_band *sb;
	int b, i;

	for (b = 0; b < NUM_NL80211_BANDS; b++) {
		if ((sb = wiphy->bands[b]) == NULL)
			continue;
		for (i = 0; i < sb->n_channels; i++)
			if (sb->channels[i].center_freq == freq)
				return &sb->channels[i];
	}
	return NULL;
}

/*
 * a rate in 100 kb/s: legacy as it is; HT, VHT and HE from the data
 * subcarriers of the width, the bits per subcarrier and the coding rate
 * of the MCS, the symbol time with its guard interval, per stream
 */
uint32_t
linuxkpi_cfg80211_calculate_bitrate(struct rate_info *ri)
{
	/* bits per subcarrier times the coding rate, times 12 (MCS 0..13) */
	static const u16 bits12[] = { 6, 12, 18, 24, 36, 48, 54, 60, 72, 80, 90, 100, 108, 120 };
	static const u16 sc_ht[] = { 52, 108, 234, 468 };	/* 20, 40, 80, 160 MHz */
	static const u16 sc_he[] = { 234, 468, 980, 1960 };
	unsigned int w, sc, mcs, nss, sym;
	u64 r;

	if (!(ri->flags & (RATE_INFO_FLAGS_MCS | RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_HE_MCS)))
		return ri->legacy;
	switch (ri->bw) {
	case RATE_INFO_BW_40: w = 1; break;
	case RATE_INFO_BW_80: w = 2; break;
	case RATE_INFO_BW_160: w = 3; break;
	default: w = 0; break;
	}
	nss = ri->nss ? ri->nss : 1;
	mcs = ri->mcs;
	if (ri->flags & RATE_INFO_FLAGS_MCS) {
		nss = mcs / 8 + 1;	/* HT: the MCS counts the streams */
		mcs %= 8;
	}
	if (mcs >= nitems(bits12))
		return 0;
	if (ri->flags & RATE_INFO_FLAGS_HE_MCS) {
		sc = sc_he[w];
		sym = 136;	/* 12.8 us + 0.8 us guard, in 100 ns */
	} else {
		sc = sc_ht[w];
		sym = (ri->flags & RATE_INFO_FLAGS_SHORT_GI) ? 36 : 40;	/* 3.6 or 4 us */
	}
	/* bits per symbol / symbol time: bits/100ns = Mb/s * 10 */
	r = (u64)sc * bits12[mcs] * nss * 100;
	return div_u64(r, (u64)12 * sym);
}

/*
 * the channel contexts of a driver without them: one channel, the
 * hardware's (config)
 */
static void
emulate_set(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
	hw->conf.chandef = ctx->def;
	hw_to_lhw(hw)->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}

int
ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
	emulate_set(hw, ctx);
	return 0;
}

void
ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
}

void
ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx, uint32_t changed)
{
	emulate_set(hw, ctx);
}

int
ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif_chanctx_switch *sw,
    int n, enum ieee80211_chanctx_switch_mode mode)
{
	if (n > 0)
		emulate_set(hw, sw[0].new_ctx);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* the station */

static struct ieee80211_txq *
newtxq(struct lhw *l, struct ieee80211_vif *vif, struct ieee80211_sta *sta, int tid)
{
	struct ieee80211_txq *q;

	q = kzalloc(sizeof(*q) + l->hw.txq_data_size, GFP_KERNEL);
	if (q == NULL)
		return NULL;
	q->vif = vif;
	q->sta = sta;
	q->tid = tid;
	q->ac = tid < 0 || tid >= IEEE80211_NUM_TIDS ? IEEE80211_AC_BE :
	    ac_from_tid(tid);
	return q;
}

/* the default EDCA parameters of 802.11 (Table 9-155), by access category */
static void
conftx(struct lhw *l)
{
	static const struct { u8 aifs; u16 cwmin, cwmax, txop; } edca[IEEE80211_NUM_ACS] = {
		[IEEE80211_AC_VO] = { 2, 3, 7, 47 },
		[IEEE80211_AC_VI] = { 2, 7, 15, 94 },
		[IEEE80211_AC_BE] = { 3, 15, 1023, 0 },
		[IEEE80211_AC_BK] = { 7, 15, 1023, 0 },
	};
	struct ieee80211_tx_queue_params p;
	int ac;

	if (l->ops->conf_tx == NULL)
		return;
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		memset(&p, 0, sizeof(p));
		p.aifs = edca[ac].aifs;
		p.cw_min = edca[ac].cwmin;
		p.cw_max = edca[ac].cwmax;
		p.txop = edca[ac].txop;
		l->ops->conf_tx(&l->hw, l->vif, 0, ac, &p);
	}
}

static void
filter(struct lhw *l)
{
	unsigned int total;

	if (l->ops->configure_filter == NULL)
		return;
	total = FIF_ALLMULTI;
	if (!l->assoc)
		total |= FIF_BCN_PRBRESP_PROMISC;	/* every BSS's beacons for wifi.c's scan */
	l->ops->configure_filter(&l->hw, FIF_FLAGS_MASK, &total, 0);
}

static void
scan(struct lhw *l, bool on)
{
	if (l->scanning == on)
		return;
	if (on && l->ops->sw_scan_start)
		l->ops->sw_scan_start(&l->hw, l->vif, l->vif->addr);
	if (!on && l->ops->sw_scan_complete)
		l->ops->sw_scan_complete(&l->hw, l->vif);
	l->scanning = on;
}

/* the first 2.4 GHz channel the hardware has (else any) */
static struct linuxkpi_ieee80211_channel *
firstchan(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sb;
	int b, i;

	for (b = 0; b < NUM_NL80211_BANDS; b++) {
		if ((sb = wiphy->bands[b]) == NULL)
			continue;
		for (i = 0; i < sb->n_channels; i++)
			if (!(sb->channels[i].flags & IEEE80211_CHAN_DISABLED))
				return &sb->channels[i];
	}
	return NULL;
}

/*
 * the hardware's channel (conf.chandef): a channel context the interface
 * keeps from the start (one, whether scanning or associated: tune
 * changes it), else (a driver without them) the hardware's by config
 */
static int
chanctx(struct lhw *l)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_chanctx_conf *ctx;
	int r;

	if (l->ops->add_chanctx == NULL || l->ops->assign_vif_chanctx == NULL)
		return l->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
	ctx = kzalloc(sizeof(*ctx) + hw->chanctx_data_size, GFP_KERNEL);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->def = hw->conf.chandef;
	ctx->min_def = hw->conf.chandef;
	ctx->rx_chains_static = 1;
	ctx->rx_chains_dynamic = 1;
	if ((r = l->ops->add_chanctx(hw, ctx)) != 0) {
		pr_err("mac80211: add_chanctx: error %d\n", r);
		kfree(ctx);
		return r;
	}
	l->vif->bss_conf.chanreq.oper = ctx->def;
	if ((r = l->ops->assign_vif_chanctx(hw, l->vif, &l->vif->bss_conf, ctx)) != 0) {
		pr_err("mac80211: assign_vif_chanctx: error %d\n", r);
		l->ops->remove_chanctx(hw, ctx);
		kfree(ctx);
		return r;
	}
	l->ctx = ctx;
	return 0;
}

static void
chanctxdown(struct lhw *l)
{
	if (l->ctx == NULL)
		return;
	if (l->ops->unassign_vif_chanctx)
		l->ops->unassign_vif_chanctx(&l->hw, l->vif, &l->vif->bss_conf, l->ctx);
	l->ops->remove_chanctx(&l->hw, l->ctx);
	kfree(l->ctx);
	l->ctx = NULL;
}

/* the driver started with one station interface on the hardware's address */
int
kpi80211_up(struct lhw *l)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_vif *vif;
	struct linuxkpi_ieee80211_channel *c;
	int r, ac;

	lockdep_assert_wiphy(hw->wiphy);
	if (!l->registered)
		return -ENODEV;
	if (l->vif == NULL) {
		vif = kzalloc(sizeof(*vif) + hw->vif_data_size, GFP_KERNEL);
		if (vif == NULL)
			return -ENOMEM;
		vif->type = NL80211_IFTYPE_STATION;
		memcpy(vif->addr, hw->wiphy->perm_addr, ETH_ALEN);
		vif->bss_conf.vif = vif;
		vif->bss_conf.bssid = l->bssid;	/* zero while not associated */
		memcpy(vif->bss_conf.addr, vif->addr, ETH_ALEN);
		vif->bss_conf.beacon_int = 100;
		vif->bss_conf.dtim_period = 1;
		vif->link_conf[0] = &vif->bss_conf;
		vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			vif->hw_queue[ac] = ac;
		vif->txq = newtxq(l, vif, NULL, IEEE80211_NUM_TIDS);
		if (vif->txq == NULL) {
			kfree(vif);
			return -ENOMEM;
		}
		l->vif = vif;
	}
	if (!l->started) {
		if ((r = l->ops->start(hw)) != 0) {
			pr_err("mac80211: start: error %d\n", r);
			return r;
		}
		l->started = true;
		if ((r = l->ops->add_interface(hw, l->vif)) != 0) {
			pr_err("mac80211: add_interface: error %d\n", r);
			l->ops->stop(hw, false);
			l->started = false;
			return r;
		}
	}
	conftx(l);
	filter(l);
	if ((c = hw->conf.chandef.chan) == NULL && (c = firstchan(hw->wiphy)) == NULL)
		return -ENODEV;
	cfg80211_chandef_create(&hw->conf.chandef, c, NL80211_CHAN_NO_HT);
	if ((r = chanctx(l)) != 0)
		return r;
	scan(l, true);
	return 0;
}

/* on channel c, 20 MHz */
int
kpi80211_tune(struct lhw *l, struct linuxkpi_ieee80211_channel *c)
{
	struct ieee80211_hw *hw = &l->hw;

	lockdep_assert_wiphy(hw->wiphy);
	if (c == NULL || (c->flags & IEEE80211_CHAN_DISABLED))
		return -EINVAL;
	if (hw->conf.chandef.chan == c)
		return 0;
	cfg80211_chandef_create(&hw->conf.chandef, c, NL80211_CHAN_NO_HT);
	if (l->ctx != NULL) {
		l->ctx->def = hw->conf.chandef;
		l->ctx->min_def = hw->conf.chandef;
		l->vif->bss_conf.chanreq.oper = hw->conf.chandef;
		if (l->ops->change_chanctx)
			l->ops->change_chanctx(hw, l->ctx, IEEE80211_CHANCTX_CHANGE_WIDTH |
			    IEEE80211_CHANCTX_CHANGE_CHANNEL);
		return 0;
	}
	return l->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}

static int
stastate(struct lhw *l, enum ieee80211_sta_state from, enum ieee80211_sta_state to)
{
	int r;

	if (l->ops->sta_state == NULL)
		return 0;
	r = l->ops->sta_state(&l->hw, l->vif, l->sta, from, to);
	if (r)
		pr_err("mac80211: sta_state %d to %d: error %d\n", from, to, r);
	return r;
}

/* the access point as the driver's station: its legacy rates (wifi.c's), 20 MHz */
static struct ieee80211_sta *
newsta(struct lhw *l, const u8 *bssid, u16 aid)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_supported_band *sb;
	struct ieee80211_sta *sta;
	int i, b;

	sta = kzalloc(sizeof(*sta) + hw->sta_data_size, GFP_KERNEL);
	if (sta == NULL)
		return NULL;
	memcpy(sta->addr, bssid, ETH_ALEN);
	sta->aid = aid;
	sta->deflink.sta = sta;
	memcpy(sta->deflink.addr, bssid, ETH_ALEN);
	sta->deflink.rx_nss = 1;
	sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
	sta->deflink.smps_mode = IEEE80211_SMPS_OFF;
	sta->deflink.agg.max_amsdu_len = IEEE80211_MAX_MPDU_LEN_HT_3839;
	sta->link[0] = &sta->deflink;
	for (b = 0; b < NUM_NL80211_BANDS; b++)
		if ((sb = hw->wiphy->bands[b]) != NULL)
			sta->deflink.supp_rates[b] = sb->n_bitrates >= 32 ? ~0U : (1U << sb->n_bitrates) - 1;
	for (i = 0; i < nitems(sta->txq); i++) {
		sta->txq[i] = newtxq(l, l->vif, sta, i);
		if (sta->txq[i] == NULL) {
			while (--i >= 0)
				freetxq(sta->txq[i]);
			kfree(sta);
			return NULL;
		}
	}
	return sta;
}

static void
freesta(struct lhw *l)
{
	int i;

	if (l->sta == NULL)
		return;
	for (i = 0; i < nitems(l->sta->txq); i++)
		freetxq(l->sta->txq[i]);
	kfree(l->sta);
	l->sta = NULL;
}

/* the basic rates of the band: the lowest of each modulation (1, 6 Mb/s) */
static u32
basicrates(struct lhw *l)
{
	struct linuxkpi_ieee80211_channel *c = l->hw.conf.chandef.chan;
	struct ieee80211_supported_band *sb;
	u32 m = 0;
	int i;

	if (c == NULL || (sb = l->hw.wiphy->bands[c->band]) == NULL)
		return 1;
	for (i = 0; i < sb->n_bitrates && i < 32; i++)
		if (sb->bitrates[i].bitrate == 10 || sb->bitrates[i].bitrate == 60)
			m |= 1U << i;
	return m ? m : 1;
}

/*
 * the access point's HT, as mac80211 makes a station's from the element
 * (ieee80211_ht_cap_ie_to_sta_ht_cap): its capabilities with ours, 40 MHz
 * where both have it and its HT operation names a secondary channel
 * (wide40), the MCS it receives that we send; rtw89's rate control then
 * uses HT
 */
/*
 * 40 MHz with the access point: both have it, its HT operation names the
 * secondary channel (above or below), the hardware has that channel;
 * then def is the 40 MHz chandef
 */
static bool
wide40(struct lhw *l, struct linuxkpi_ieee80211_channel *c, struct ieee80211_supported_band *sb,
    struct cfg80211_chan_def *def)
{
	struct linuxkpi_ieee80211_channel *s;
	int f;

	if (!l->apht || (l->apsec != 1 && l->apsec != 3) ||
	    !(sb->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ||
	    !(le16_to_cpu(l->apcap.cap_info) & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
		return false;
	f = c->center_freq + (l->apsec == 1 ? 20 : -20);
	if ((s = linuxkpi_ieee80211_get_channel(l->hw.wiphy, f)) == NULL || (s->flags & IEEE80211_CHAN_DISABLED))
		return false;
	if (def != NULL)
		cfg80211_chandef_create(def, c, l->apsec == 1 ? NL80211_CHAN_HT40PLUS : NL80211_CHAN_HT40MINUS);
	return true;
}

/* the channel context to def's width (40 MHz with the access point, 20 again without) */
static void
setwidth(struct lhw *l, struct cfg80211_chan_def *def)
{
	struct ieee80211_hw *hw = &l->hw;

	hw->conf.chandef = *def;
	if (l->ctx != NULL) {
		l->ctx->def = *def;
		l->ctx->min_def = *def;
		l->vif->bss_conf.chanreq.oper = *def;
		if (l->ops->change_chanctx)
			l->ops->change_chanctx(hw, l->ctx, IEEE80211_CHANCTX_CHANGE_WIDTH);
	} else
		l->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}

static void
staht(struct lhw *l, struct ieee80211_sta *sta)
{
	struct linuxkpi_ieee80211_channel *c = l->hw.conf.chandef.chan;
	struct ieee80211_supported_band *sb;
	struct ieee80211_sta_ht_cap *h = &sta->deflink.ht_cap;
	int i, nss;

	if (!l->apht || c == NULL || (sb = l->hw.wiphy->bands[c->band]) == NULL || !sb->ht_cap.ht_supported)
		return;
	memset(h, 0, sizeof(*h));
	h->ht_supported = true;
	h->cap = le16_to_cpu(l->apcap.cap_info) & sb->ht_cap.cap & ~IEEE80211_HT_CAP_DSSSCCK40;
	if (!wide40(l, c, sb, NULL))
		h->cap &= ~(IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_SGI_40);
	h->ampdu_factor = l->apcap.ampdu_params_info & 3;
	h->ampdu_density = l->apcap.ampdu_params_info >> 2 & 7;
	nss = 0;
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		h->mcs.rx_mask[i] = l->apcap.mcs.rx_mask[i] & sb->ht_cap.mcs.rx_mask[i];
		if (i < 4 && h->mcs.rx_mask[i])
			nss = i + 1;
	}
	h->mcs.rx_highest = l->apcap.mcs.rx_highest;
	h->mcs.tx_params = l->apcap.mcs.tx_params;
	sta->deflink.rx_nss = nss > 0 ? nss : 1;
}

/*
 * the access point's VHT, as mac80211's ieee80211_vht_cap_ie_to_sta_vht_cap:
 * its capabilities with ours (no 160 MHz), per spatial stream the MCS it
 * receives that we send and it sends that we receive (3: none)
 */
static void
stavht(struct lhw *l, struct ieee80211_sta *sta)
{
	struct linuxkpi_ieee80211_channel *c = l->hw.conf.chandef.chan;
	struct ieee80211_supported_band *sb;
	struct ieee80211_sta_vht_cap *v = &sta->deflink.vht_cap;
	u16 aprx, aptx, ourrx, ourtx, rx, tx;
	int i, a, b, nss;

	if (!l->apvht || c == NULL || c->band != NL80211_BAND_5GHZ ||
	    (sb = l->hw.wiphy->bands[c->band]) == NULL || !sb->vht_cap.vht_supported ||
	    !sta->deflink.ht_cap.ht_supported)
		return;
	memset(v, 0, sizeof(*v));
	v->vht_supported = true;
	v->cap = le32_to_cpu(l->apvcap.vht_cap_info) & sb->vht_cap.cap &
	    ~(IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK | IEEE80211_VHT_CAP_SHORT_GI_160);
	aprx = le16_to_cpu(l->apvcap.supp_mcs.rx_mcs_map);
	aptx = le16_to_cpu(l->apvcap.supp_mcs.tx_mcs_map);
	ourrx = le16_to_cpu(sb->vht_cap.vht_mcs.rx_mcs_map);
	ourtx = le16_to_cpu(sb->vht_cap.vht_mcs.tx_mcs_map);
	rx = tx = 0;
	nss = 0;
	for (i = 0; i < 8; i++) {
		a = aprx >> 2*i & 3;
		b = ourtx >> 2*i & 3;
		rx |= (a == 3 || b == 3 ? 3 : min(a, b)) << 2*i;
		if (a != 3 && b != 3)
			nss = i + 1;
		a = aptx >> 2*i & 3;
		b = ourrx >> 2*i & 3;
		tx |= (a == 3 || b == 3 ? 3 : min(a, b)) << 2*i;
	}
	v->vht_mcs.rx_mcs_map = cpu_to_le16(rx);
	v->vht_mcs.tx_mcs_map = cpu_to_le16(tx);
	v->vht_mcs.rx_highest = l->apvcap.supp_mcs.rx_highest;
	v->vht_mcs.tx_highest = l->apvcap.supp_mcs.tx_highest;
	if (nss > sta->deflink.rx_nss)
		sta->deflink.rx_nss = nss;
}

/*
 * the width with the access point: 80 MHz where both are VHT stations
 * and its VHT operation says 80 around a centre whose four channels the
 * hardware has, else 40 (wide40), else 20; def its chandef
 */
static enum ieee80211_sta_rx_bandwidth
apwidth(struct lhw *l, struct linuxkpi_ieee80211_channel *c, struct cfg80211_chan_def *def)
{
	struct ieee80211_supported_band *sb = l->hw.wiphy->bands[c->band];
	struct linuxkpi_ieee80211_channel *s;
	int f, k;

	if (sb == NULL || !wide40(l, c, sb, def)) {
		cfg80211_chandef_create(def, c, l->apht ? NL80211_CHAN_HT20 : NL80211_CHAN_NO_HT);
		return IEEE80211_STA_RX_BW_20;
	}
	if (l->sta == NULL || !l->sta->deflink.vht_cap.vht_supported || l->apvwidth < 1 ||
	    c->band != NL80211_BAND_5GHZ)
		return IEEE80211_STA_RX_BW_40;
	f = linuxkpi_ieee80211_channel_to_frequency(l->apvseg0, NL80211_BAND_5GHZ);
	if (f == 0 || c->center_freq < f - 30 || c->center_freq > f + 30)
		return IEEE80211_STA_RX_BW_40;
	for (k = -30; k <= 30; k += 20)
		if ((s = linuxkpi_ieee80211_get_channel(l->hw.wiphy, f + k)) == NULL ||
		    (s->flags & IEEE80211_CHAN_DISABLED))
			return IEEE80211_STA_RX_BW_40;
	def->width = NL80211_CHAN_WIDTH_80;
	def->center_freq1 = f;
	return IEEE80211_STA_RX_BW_80;
}

/* wifi.c associated with bssid as aid */
int
kpi80211_assoc(struct lhw *l, const u8 *bssid, u16 aid)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_vif *vif = l->vif;
	struct ieee80211_bss_conf *bss = &vif->bss_conf;
	int r;

	lockdep_assert_wiphy(hw->wiphy);
	if (l->assoc)
		kpi80211_disassoc(l);
	scan(l, false);
	memcpy(l->bssid, bssid, ETH_ALEN);
	l->aid = aid;

	/* the BSS, on the channel the context has */
	bss->bssid = l->bssid;
	memcpy(vif->cfg.ap_addr, l->bssid, ETH_ALEN);
	bss->basic_rates = basicrates(l);
	bss->use_short_slot = l->hw.conf.chandef.chan != NULL &&
	    l->hw.conf.chandef.chan->band != NL80211_BAND_2GHZ;
	if (l->ops->link_info_changed)
		l->ops->link_info_changed(hw, vif, bss, BSS_CHANGED_BSSID |
		    BSS_CHANGED_BEACON_INT | BSS_CHANGED_BASIC_RATES | BSS_CHANGED_ERP_SLOT);

	/* the access point's station, through the states */
	if ((l->sta = newsta(l, l->bssid, aid)) == NULL)
		return -ENOMEM;
	kpi_wlan_apinfo(l, l->bssid);
	staht(l, l->sta);
	stavht(l, l->sta);
	if (l->sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) {
		struct cfg80211_chan_def def;
		enum ieee80211_sta_rx_bandwidth bw;

		bw = apwidth(l, hw->conf.chandef.chan, &def);
		if (bw != IEEE80211_STA_RX_BW_20) {
			setwidth(l, &def);
			l->sta->deflink.bandwidth = bw;
		}
	}
	l->sta->wme = l->apwmm;
	bss->qos = l->apwmm;
	if (l->ops->link_info_changed)
		l->ops->link_info_changed(hw, vif, bss, BSS_CHANGED_QOS | BSS_CHANGED_HT);
	if ((r = stastate(l, IEEE80211_STA_NOTEXIST, IEEE80211_STA_NONE)) != 0) {
		freesta(l);
		return r;
	}
	stastate(l, IEEE80211_STA_NONE, IEEE80211_STA_AUTH);
	stastate(l, IEEE80211_STA_AUTH, IEEE80211_STA_ASSOC);

	vif->cfg.assoc = true;
	vif->cfg.aid = aid;
	l->assoc = true;
	if (l->ops->vif_cfg_changed)
		l->ops->vif_cfg_changed(hw, vif, BSS_CHANGED_ASSOC);
	/* wifi.c's (and aux/wpa's) keys are not the driver's: the port is open */
	stastate(l, IEEE80211_STA_ASSOC, IEEE80211_STA_AUTHORIZED);
	filter(l);
	pr_info("mac80211: associated with %pM, aid %d%s%s%s%s\n", l->bssid, aid,
	    l->sta->deflink.ht_cap.ht_supported ? ", HT" : "",
	    l->sta->deflink.vht_cap.vht_supported ? ", VHT" : "",
	    l->sta->deflink.bandwidth == IEEE80211_STA_RX_BW_80 ? " 80 MHz" :
	    l->sta->deflink.bandwidth == IEEE80211_STA_RX_BW_40 ? " 40 MHz" : "", l->apwmm ? ", WMM" : "");
	return 0;
}

void
kpi80211_disassoc(struct lhw *l)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_vif *vif = l->vif;
	struct ieee80211_bss_conf *bss = &vif->bss_conf;

	lockdep_assert_wiphy(hw->wiphy);
	if (!l->assoc)
		return;
	if (l->ops->flush)
		l->ops->flush(hw, vif, (1U << hw->queues) - 1, false);
	stastate(l, IEEE80211_STA_AUTHORIZED, IEEE80211_STA_ASSOC);
	vif->cfg.assoc = false;
	vif->cfg.aid = 0;
	if (l->ops->vif_cfg_changed)
		l->ops->vif_cfg_changed(hw, vif, BSS_CHANGED_ASSOC);
	stastate(l, IEEE80211_STA_ASSOC, IEEE80211_STA_AUTH);
	stastate(l, IEEE80211_STA_AUTH, IEEE80211_STA_NONE);
	stastate(l, IEEE80211_STA_NONE, IEEE80211_STA_NOTEXIST);
	freesta(l);
	l->txagg = 0;
	/* 20 MHz again for the scan */
	if (hw->conf.chandef.width != NL80211_CHAN_WIDTH_20_NOHT && hw->conf.chandef.chan != NULL) {
		struct cfg80211_chan_def def;

		cfg80211_chandef_create(&def, hw->conf.chandef.chan, NL80211_CHAN_NO_HT);
		setwidth(l, &def);
	}
	eth_zero_addr(l->bssid);	/* bss->bssid's */
	eth_zero_addr(vif->cfg.ap_addr);
	if (l->ops->link_info_changed)
		l->ops->link_info_changed(hw, vif, bss, BSS_CHANGED_BSSID);
	l->assoc = false;
	l->aid = 0;
	filter(l);
	scan(l, true);
	pr_info("mac80211: disassociated\n");
}

/*
 * the block ack agreements: wifi.c (the kernel's 802.11) makes them, the
 * driver is told from the action frames passing by (802.11-2020 9.6.4),
 * as mac80211 tells it after its own: our ADDBA response agreeing (the
 * access point aggregates: RX_START, its request's starting number), the
 * access point's response to ours (we aggregate: TX_START, then
 * TX_OPERATIONAL), a DELBA either way (the STOP).  In a work, under the
 * wiphy's lock: rtw89's ampdu_action sends H2C commands and sleeps.
 */
struct baop {
	struct list_head	node;
	enum ieee80211_ampdu_mlme_action	action;
	u8	tid;
	u16	ssn;
	u16	size;
};

static void
baqueue(struct lhw *l, enum ieee80211_ampdu_mlme_action a, int tid, int ssn, int size)
{
	struct baop *op;
	unsigned long flags;

	if ((op = kzalloc(sizeof(*op), GFP_ATOMIC)) == NULL)
		return;
	op->action = a;
	op->tid = tid;
	op->ssn = ssn;
	op->size = size;
	spin_lock_irqsave(&l->ba_lock, flags);
	list_add_tail(&op->node, &l->ba_list);
	spin_unlock_irqrestore(&l->ba_lock, flags);
	queue_work(l->wq, &l->ba_work);
}

static void
ba_work(struct work_struct *w)
{
	struct lhw *l = container_of(w, struct lhw, ba_work);
	struct ieee80211_ampdu_params p;
	struct baop *op;
	unsigned long flags;
	int r;

	for (;;) {
		spin_lock_irqsave(&l->ba_lock, flags);
		op = list_first_entry_or_null(&l->ba_list, struct baop, node);
		if (op)
			list_del(&op->node);
		spin_unlock_irqrestore(&l->ba_lock, flags);
		if (op == NULL)
			return;
		wiphy_lock(l->hw.wiphy);
		if (l->sta != NULL && l->ops->ampdu_action != NULL) {
			memset(&p, 0, sizeof(p));
			p.sta = l->sta;
			p.action = op->action;
			p.tid = op->tid;
			p.ssn = op->ssn;
			p.buf_size = op->size;
			p.amsdu = false;
			r = l->ops->ampdu_action(&l->hw, l->vif, &p);
			if (op->action == IEEE80211_AMPDU_TX_START && r == IEEE80211_AMPDU_TX_START_IMMEDIATE) {
				p.action = IEEE80211_AMPDU_TX_OPERATIONAL;
				r = l->ops->ampdu_action(&l->hw, l->vif, &p);
				if (r == 0)
					l->txagg |= 1U << op->tid;
			} else if (op->action == IEEE80211_AMPDU_TX_STOP_CONT || op->action == IEEE80211_AMPDU_TX_STOP_FLUSH)
				l->txagg &= ~(1U << op->tid);
			pr_info("mac80211: block ack tid %d: %s%s\n", op->tid,
			    op->action == IEEE80211_AMPDU_RX_START ? "receiving" :
			    op->action == IEEE80211_AMPDU_RX_STOP ? "not receiving" :
			    op->action == IEEE80211_AMPDU_TX_START ? "sending" : "not sending",
			    r < 0 ? " (refused)" : "");
		}
		wiphy_unlock(l->hw.wiphy);
		kfree(op);
	}
}

/*
 * the access point's width while associated, as mac80211 follows its
 * beacons (ieee80211_config_bw): its HT operation names a secondary
 * channel or no longer does (20/40 MHz coexistence on 2.4 GHz: a
 * neighbour makes it go to 20 MHz); the context and the station follow,
 * the driver's rate control told (link_sta_rc_update, BW_CHANGED)
 */
void
kpi80211_beacon(struct lhw *l, const u8 *f, int len)
{
	const u8 *p, *e;
	int sec, vw, vs;

	if (!l->assoc || len < 24 + 12 || f[0] != 0x80 || !ether_addr_equal(f + 16, l->bssid))
		return;
	sec = -1;
	vw = l->apvwidth;
	vs = l->apvseg0;
	e = f + len;
	for (p = f + 24 + 12; p + 2 <= e && p + 2 + p[1] <= e; p += 2 + p[1]) {
		if (p[0] == 61 && p[1] >= 2)
			sec = (p[3] & 4) ? p[3] & 3 : 0;
		if (p[0] == 192 && p[1] >= 3) {
			vw = p[2];
			vs = p[3];
		}
	}
	if (sec < 0 || (sec == l->apsec && vw == l->apvwidth && vs == l->apvseg0))
		return;
	l->apsec = sec;
	l->apvwidth = vw;
	l->apvseg0 = vs;
	queue_work(l->wq, &l->bw_work);
}

static void
bw_work(struct work_struct *w)
{
	struct lhw *l = container_of(w, struct lhw, bw_work);
	struct ieee80211_hw *hw = &l->hw;
	struct linuxkpi_ieee80211_channel *c;
	struct cfg80211_chan_def def;
	enum ieee80211_sta_rx_bandwidth bw, old;

	wiphy_lock(hw->wiphy);
	c = hw->conf.chandef.chan;
	if (l->sta == NULL || c == NULL || hw->wiphy->bands[c->band] == NULL ||
	    !(l->sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)) {
		wiphy_unlock(hw->wiphy);
		return;
	}
	bw = apwidth(l, c, &def);
	old = l->sta->deflink.bandwidth;
	if (bw != old) {
		/* narrower: the station first; wider: the channel first */
		if (bw < old)
			l->sta->deflink.bandwidth = bw;
		setwidth(l, &def);
		if (bw > old)
			l->sta->deflink.bandwidth = bw;
		if (l->ops->link_sta_rc_update)
			l->ops->link_sta_rc_update(hw, l->vif, &l->sta->deflink, IEEE80211_RC_BW_CHANGED);
		else if (l->ops->sta_rc_update)
			l->ops->sta_rc_update(hw, l->vif, l->sta, IEEE80211_RC_BW_CHANGED);
		pr_info("mac80211: the access point's width now %d MHz\n",
		    bw == IEEE80211_STA_RX_BW_80 ? 80 : bw == IEEE80211_STA_RX_BW_40 ? 40 : 20);
	}
	wiphy_unlock(hw->wiphy);
}

void
kpi80211_ba(struct lhw *l, const u8 *f, int len, bool tx)
{
	const u8 *d = f + 24;
	int n = len - 24, param, tid, size;

	if (len < 24 || f[0] != 0xd0 || n < 2 || d[0] != 3)
		return;
	switch (d[1]) {
	case 0:	/* ADDBA request: the access point's, its starting number */
		if (tx || n < 9)
			return;
		param = get_unaligned_le16(d + 3);
		l->bassn[param >> 2 & 15] = get_unaligned_le16(d + 7) >> 4;
		break;
	case 1:	/* ADDBA response */
		if (n < 9 || get_unaligned_le16(d + 3) != 0)
			return;
		param = get_unaligned_le16(d + 5);
		tid = param >> 2 & 15;
		size = param >> 6;
		if (tx)
			baqueue(l, IEEE80211_AMPDU_RX_START, tid, l->bassn[tid], size);
		else
			baqueue(l, IEEE80211_AMPDU_TX_START, tid, 0, size);
		break;
	case 2:	/* DELBA: bit 11 says whose the agreement is */
		if (n < 6)
			return;
		param = get_unaligned_le16(d + 2);
		tid = param >> 12 & 15;
		if (((param & (1 << 11)) != 0) == tx)
			baqueue(l, IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0);
		else
			baqueue(l, IEEE80211_AMPDU_RX_STOP, tid, 0, 0);
		break;
	}
}

/* a frame of wifi.c's (sequence numbers its own, ciphered as it chose) to the driver */
void
kpi80211_tx(struct lhw *l, struct sk_buff *skb)
{
	struct ieee80211_hw *hw = &l->hw;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *h = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_control control;
	struct linuxkpi_ieee80211_channel *c = hw->conf.chandef.chan;
	int ac;

	memset(info, 0, sizeof(*info));
	memset(&control, 0, sizeof(control));
	info->band = c != NULL ? c->band : NL80211_BAND_2GHZ;
	info->flags = IEEE80211_TX_CTL_FIRST_FRAGMENT;
	if (is_multicast_ether_addr(h->addr1))
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->control.vif = l->vif;
	skb->priority = 0;
	ac = IEEE80211_AC_BE;
	if (ieee80211_is_data_qos(h->frame_control)) {
		skb->priority = *ieee80211_get_qos_ctl(h) & IEEE80211_QOS_CTL_TID_MASK;
		ac = ac_from_tid(skb->priority);
		/* in an aggregate where we have the agreement */
		if (l->txagg & (1U << skb->priority))
			info->flags |= IEEE80211_TX_CTL_AMPDU;
	} else if (!ieee80211_is_data(h->frame_control))
		ac = IEEE80211_AC_VO;	/* management frames go first, as on Linux */
	skb_set_queue_mapping(skb, ac);
	info->hw_queue = l->vif->hw_queue[ac];
	/* the access point's station for data and its management frames */
	if (l->sta != NULL && ether_addr_equal(h->addr1, l->sta->addr))
		control.sta = l->sta;
	kpi80211_ba(l, skb->data, skb->len, true);
	l->ntx++;
	rcu_read_lock();
	l->ops->tx(hw, &control, skb);
	rcu_read_unlock();
}

void
linuxkpi_ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb, struct ieee80211_sta *sta,
    struct napi_struct *napi, struct list_head *list)
{
	kpi_wlan_rx(hw_to_lhw(hw), skb);
}
