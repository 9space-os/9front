/* SPDX-License-Identifier: MIT */
/*
 * LinuxKPI for 9front: what mac80211.c (the layer the WLAN drivers call)
 * and wlan.c (the radio of wlan(3) it serves) share.  One hardware, one
 * station interface, one access point.
 */
#ifndef _KPI_LINUX_KPI80211_H
#define _KPI_LINUX_KPI80211_H

#include <net/mac80211.h>

/*
 * a txq and its frames, waiting for the driver to take them
 * (ieee80211_tx_dequeue), as FreeBSD's linuxkpi has its lkpi_txq
 */
struct ltxq {
	struct list_head	sched;		/* on lhw's txqsched, while scheduled */
	unsigned int	gen;		/* the scheduling round it was taken in */
	struct sk_buff_head	q;
	struct ieee80211_txq	txq;	/* last: the driver's part follows */
};

struct lhw {
	struct ieee80211_hw	hw;
	const struct ieee80211_ops	*ops;
	bool	registered;
	bool	started;
	struct workqueue_struct	*wq;		/* ieee80211_queue_work's */

	/* the wiphy works: run in order, under the wiphy's lock */
	spinlock_t	wwk_lock;
	struct list_head	wwk_list;
	struct workqueue_struct	*wwk_wq;
	struct work_struct	wwk_work;

	/* the station: its interface, the access point, the channel */
	struct ieee80211_vif	*vif;
	struct ieee80211_sta	*sta;		/* the access point, when associated */
	struct ieee80211_chanctx_conf	*ctx;	/* assigned while associated */
	bool	scanning;
	bool	assoc;
	u8	bssid[ETH_ALEN];
	u16	aid;
	bool	stopped;			/* stop_queues */
	/* the txqs the driver was told have frames (wake_tx_queue), by access category */
	spinlock_t	txqlock;
	struct list_head	txqsched[IEEE80211_NUM_ACS];
	unsigned int	txqgen[IEEE80211_NUM_ACS];
	struct work_struct	restart_work;

	/* the radio (wlan.c) */
	int	datafd;
	unsigned long	ntx, nrx, ndrop;
	/*
	 * the access point's HT capabilities, WMM and HT operation, from the
	 * frames the radio passes on (seen: per BSS, beacons, probe and
	 * association responses), taken at association (kpi_wlan_apinfo)
	 */
	struct apinfo {
		u8	bssid[ETH_ALEN];
		bool	used, ht, wmm, vht;
		u8	sec;
		u8	vwidth, vseg0;
		unsigned long	when;
		struct ieee80211_ht_cap	cap;
		struct ieee80211_vht_cap	vcap;
	} seen[16];
	bool	apht;
	bool	apwmm;
	u8	apsec;		/* its HT operation: secondary channel above (1), below (3), none (0) */
	bool	apvht;
	u8	apvwidth;	/* its VHT operation: 0 20/40 MHz, 1 80, ...; seg0 the 80's centre channel */
	u8	apvseg0;
	struct ieee80211_vht_cap	apvcap;
	struct work_struct	bw_work;	/* the width its beacons now say */
	struct ieee80211_ht_cap	apcap;
	/*
	 * the block ack agreements wifi.c makes (the ADDBA/DELBA frames
	 * passing by): the access point's ADDBA requests' starting numbers,
	 * the TIDs we aggregate on
	 */
	u16	bassn[16];
	u16	txagg;
	struct work_struct	ba_work;
	spinlock_t	ba_lock;
	struct list_head	ba_list;

	/* the driver's, aligned */
	uint8_t	priv[0] __aligned(CACHE_LINE_SIZE);
};

static inline struct lhw *
hw_to_lhw(struct ieee80211_hw *hw)
{
	return container_of(hw, struct lhw, hw);
}

/* mac80211.c: the station's states, under the wiphy's lock */
int	kpi80211_up(struct lhw *l);
int	kpi80211_tune(struct lhw *l, struct linuxkpi_ieee80211_channel *c);
int	kpi80211_assoc(struct lhw *l, const u8 *bssid, u16 aid);
void	kpi80211_disassoc(struct lhw *l);
void	kpi80211_tx(struct lhw *l, struct sk_buff *skb);
struct lhw	*kpi80211_hw(void);	/* the one ieee80211_alloc_hw made */

/* wlan.c: a frame received, to the radio's file */
void	kpi_wlan_rx(struct lhw *l, struct sk_buff *skb);
/* wlan.c: what the frames said of bssid, to l->apht, apwmm, apsec, apcap */
void	kpi_wlan_apinfo(struct lhw *l, const u8 *bssid);

/* mac80211.c: a block ack action frame passing (tx: ours), its ampdu_action */
void	kpi80211_ba(struct lhw *l, const u8 *f, int len, bool tx);
/* mac80211.c: a beacon of the access point's: the width its HT operation says */
void	kpi80211_beacon(struct lhw *l, const u8 *f, int len);

#endif
