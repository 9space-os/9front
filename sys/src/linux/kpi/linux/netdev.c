// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: what the WLAN drivers use of the network
 * devices: a net_device for its private area (rtw89's NAPI holder) and
 * NAPI.  A NAPI's poll runs as work on a workqueue of its own (the
 * drivers schedule it from their interrupt handlers): polled again
 * while it uses its whole budget, until the driver completes it.  The
 * state bits are those of FreeBSD's linux/netdevice.h: IS_SCHEDULED
 * (queued or running), LOST_RACE (scheduled again while running),
 * DISABLE_PENDING (napi_disable: no new schedules).
 */
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/string.h>

struct net { int dummy; };
struct net kpi_init_net;

static struct workqueue_struct *napi_wq;

static void
napi_work(struct work_struct *w)
{
	struct napi_struct *n = container_of(w, struct napi_struct, napi_work);
	int done;

	if (test_bit(LKPI_NAPI_FLAG_SHUTDOWN, &n->state))
		return;
	clear_bit(LKPI_NAPI_FLAG_LOST_RACE_TRY_AGAIN, &n->state);
	done = n->poll(n, n->budget);
	/*
	 * the whole budget used: the driver did not complete it, there is
	 * more; else it completed it (napi_complete_done) or will be
	 * scheduled again
	 */
	if (done >= n->budget && test_bit(LKPI_NAPI_FLAG_IS_SCHEDULED, &n->state))
		queue_work(napi_wq, &n->napi_work);
}

void
linuxkpi_netif_napi_add(struct net_device *dev, struct napi_struct *n,
    int (*poll)(struct napi_struct *, int))
{
	if (napi_wq == NULL)
		napi_wq = alloc_ordered_workqueue("napi", 0);
	memset(n, 0, sizeof(*n));
	n->dev = dev;
	n->poll = poll;
	n->budget = NAPI_POLL_WEIGHT;
	INIT_LIST_HEAD(&n->rx_list);
	INIT_WORK(&n->napi_work, napi_work);
	/* disabled until napi_enable, as on Linux */
	set_bit(LKPI_NAPI_FLAG_DISABLE_PENDING, &n->state);
	if (dev != NULL) {
		spin_lock(&dev->napi_lock);
		list_add_tail(&n->entry, &dev->napi_list);
		spin_unlock(&dev->napi_lock);
	} else
		INIT_LIST_HEAD(&n->entry);
}

void
linuxkpi_netif_napi_del(struct napi_struct *n)
{
	set_bit(LKPI_NAPI_FLAG_SHUTDOWN, &n->state);
	cancel_work_sync(&n->napi_work);
	if (n->dev != NULL) {
		spin_lock(&n->dev->napi_lock);
		list_del_init(&n->entry);
		spin_unlock(&n->dev->napi_lock);
	}
}

bool
linuxkpi_napi_schedule_prep(struct napi_struct *n)
{
	if (test_bit(LKPI_NAPI_FLAG_DISABLE_PENDING, &n->state))
		return false;
	if (test_and_set_bit(LKPI_NAPI_FLAG_IS_SCHEDULED, &n->state)) {
		set_bit(LKPI_NAPI_FLAG_LOST_RACE_TRY_AGAIN, &n->state);
		return false;
	}
	return true;
}

void
linuxkpi___napi_schedule(struct napi_struct *n)
{
	queue_work(napi_wq, &n->napi_work);
}

bool
linuxkpi_napi_schedule(struct napi_struct *n)
{
	if (!linuxkpi_napi_schedule_prep(n))
		return false;
	linuxkpi___napi_schedule(n);
	return true;
}

void
linuxkpi_napi_reschedule(struct napi_struct *n)
{
	linuxkpi_napi_schedule(n);
}

bool
linuxkpi_napi_complete_done(struct napi_struct *n, int done)
{
	clear_bit(LKPI_NAPI_FLAG_IS_SCHEDULED, &n->state);
	/* scheduled while it ran: once more */
	if (test_and_clear_bit(LKPI_NAPI_FLAG_LOST_RACE_TRY_AGAIN, &n->state))
		linuxkpi_napi_schedule(n);
	return true;
}

bool
linuxkpi_napi_complete(struct napi_struct *n)
{
	return linuxkpi_napi_complete_done(n, 0);
}

void
linuxkpi_napi_disable(struct napi_struct *n)
{
	set_bit(LKPI_NAPI_FLAG_DISABLE_PENDING, &n->state);
	flush_work(&n->napi_work);
	clear_bit(LKPI_NAPI_FLAG_IS_SCHEDULED, &n->state);
}

void
linuxkpi_napi_enable(struct napi_struct *n)
{
	clear_bit(LKPI_NAPI_FLAG_SHUTDOWN, &n->state);
	clear_bit(LKPI_NAPI_FLAG_IS_SCHEDULED, &n->state);
	clear_bit(LKPI_NAPI_FLAG_DISABLE_PENDING, &n->state);
}

void
linuxkpi_napi_synchronize(struct napi_struct *n)
{
	flush_work(&n->napi_work);
}

void
linuxkpi_init_dummy_netdev(struct net_device *dev)
{
	memset(dev, 0, sizeof(*dev));
	spin_lock_init(&dev->napi_lock);
	INIT_LIST_HEAD(&dev->napi_list);
	dev->reg_state = NETREG_DUMMY;
}

struct net_device *
linuxkpi_alloc_netdev(size_t len, const char *name, uint32_t flags,
    void (*setup)(struct net_device *))
{
	struct net_device *dev;

	dev = kzalloc(sizeof(*dev) + len, GFP_KERNEL);
	if (dev == NULL)
		return NULL;
	linuxkpi_init_dummy_netdev(dev);
	strscpy(dev->name, name, sizeof(dev->name));
	if (setup != NULL)
		setup(dev);
	return dev;
}

void
linuxkpi_free_netdev(struct net_device *dev)
{
	struct napi_struct *n, *t;

	if (dev == NULL)
		return;
	list_for_each_entry_safe(n, t, &dev->napi_list, entry)
		linuxkpi_netif_napi_del(n);
	kfree(dev);
}
