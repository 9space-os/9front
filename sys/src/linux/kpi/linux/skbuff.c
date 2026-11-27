/*-
 * Copyright (c) 2020-2025 The FreeBSD Foundation
 * Copyright (c) 2021-2022 Bjoern A. Zeeb
 *
 * This software was developed by Björn Zeeb under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * LinuxKPI for 9front: FreeBSD's linux_skbuff.c (freebsd/, 584f44221f45),
 * changed where marked 9front: an skb's data is a run of the page pool's
 * pages (page.c), which the device reaches (dma_map_single: rtw89's
 * receive buffers and frames to send); no UMA zone, no mbufs, no sysctls:
 * FreeBSD's compat.linuxkpi.skb.mem_limit is kpi_skb_memlimit, set by
 * the program (aux/rtw89).
 */

#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mm.h>

/*
 * FreeBSD's linuxkpi_skb_memlimit: Realtek's drivers (rtw88, rtw89) do
 * not work reliably with more than 4 GB of memory unless the data of the
 * skbs is below (FreeBSD's rtw89(4), BUGS): 0 no limit, 1 32 bits, 2 36
 * bits (the device's mask bounds the pool to that anyway, page.c)
 */
int kpi_skb_memlimit;

/* 9front: the data, contiguous for the device too */
static void *
skb_data_alloc(size_t len, gfp_t gfp)
{
	struct page *p;

	if (kpi_skb_memlimit != 0 && kpi_skb_memlimit != 2)
		gfp |= __GFP_DMA32;
	p = alloc_pages(gfp | __GFP_ZERO, get_order(len));
	return p != NULL ? page_address(p) : NULL;
}

static void
skb_data_free(void *data, size_t len)
{
	if (data != NULL)
		__free_pages(virt_to_page(data), get_order(len));
}

struct sk_buff *
linuxkpi_alloc_skb(size_t size, gfp_t gfp)
{
	struct sk_buff *skb;
	void *p;

	skb = kzalloc(sizeof(*skb) + sizeof(struct skb_shared_info), gfp);	/* 9front */
	if (skb == NULL)
		return (NULL);
	skb->prev = skb->next = skb;
	skb->truesize = size;
	skb->shinfo = (struct skb_shared_info *)(skb + 1);
	refcount_set(&skb->refcnt, 1);

	if (size == 0)
		return (skb);

	p = skb_data_alloc(size, gfp);	/* 9front */
	if (p == NULL) {
		kfree(skb);
		return (NULL);
	}

	skb->head = skb->data = (uint8_t *)p;
	skb_reset_tail_pointer(skb);
	skb->end = skb->head + size;

	SKB_TRACE_FMT(skb, "data %p size %zu", (skb) ? skb->data : NULL, size);
	return (skb);
}

struct sk_buff *
linuxkpi_dev_alloc_skb(size_t size, gfp_t gfp)
{
	struct sk_buff *skb;
	size_t len;

	len = size + NET_SKB_PAD;
	skb = linuxkpi_alloc_skb(len, gfp);

	if (skb != NULL)
		skb_reserve(skb, NET_SKB_PAD);

	SKB_TRACE_FMT(skb, "data %p size %zu len %zu",
	    (skb) ? skb->data : NULL, size, len);
	return (skb);
}

struct sk_buff *
linuxkpi_build_skb(void *data, size_t fragsz)
{
	struct sk_buff *skb;

	if (data == NULL || fragsz == 0)
		return (NULL);

	/* Just allocate a skb without data area. */
	skb = linuxkpi_alloc_skb(0, GFP_KERNEL);
	if (skb == NULL)
		return (NULL);

	skb->_flags |= _SKB_FLAGS_SKBEXTFRAG;
	skb->truesize = fragsz;
	skb->head = skb->data = data;
	skb_reset_tail_pointer(skb);
	skb->end = skb->head + fragsz;

	return (skb);
}

struct sk_buff *
linuxkpi_skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
	struct sk_buff *new;
	struct skb_shared_info *shinfo;
	size_t len;
	unsigned int headroom;
	uint16_t fragno, count;

	/* Full buffer size + any fragments. */
	len = skb->end - skb->head + skb->data_len;

	new = linuxkpi_alloc_skb(len, gfp);
	if (new == NULL)
		return (NULL);

	headroom = skb_headroom(skb);
	/* Fixup head and end. */
	skb_reserve(new, headroom);	/* data and tail move headroom forward. */
	skb_put(new, skb->len);		/* tail and len get adjusted */

	/* Copy data. */
	memcpy(new->head, skb->data - headroom, headroom + skb->len);

	/* Deal with fragments. */
	shinfo = skb->shinfo;
	for (count = fragno = 0;
	    count < shinfo->nr_frags && fragno < nitems(shinfo->frags);
	    fragno++) {

		if (shinfo->frags[fragno].page != NULL) {
			skb_put_data(new,
			    skb_frag_address(&shinfo->frags[fragno]),
			    shinfo->frags[fragno].size);
			count++;
		}
	}

	/* Deal with header fields. */
	memcpy(new->cb, skb->cb, sizeof(skb->cb));
	new->protocol = skb->protocol;	/* 9front */
	new->priority = skb->priority;
	new->dev = skb->dev;

	return (new);
}

/* 9front: the head's data, by how it came */
static void
skb_head_free(struct sk_buff *skb)
{
	if ((skb->_flags & _SKB_FLAGS_SKBEXTFRAG) != 0) {
		skb_free_frag(skb->head);
		skb->_flags &= ~_SKB_FLAGS_SKBEXTFRAG;
	} else
		skb_data_free(skb->head, skb->end - skb->head);
	skb->head = NULL;
}

static void
skb_frags_free(struct sk_buff *skb)
{
	struct skb_shared_info *shinfo;
	uint16_t fragno, count;

	shinfo = skb->shinfo;
	for (count = fragno = 0;
	    count < shinfo->nr_frags && fragno < nitems(shinfo->frags);
	    fragno++) {

		if (shinfo->frags[fragno].page != NULL) {
			struct page *p;

			p = shinfo->frags[fragno].page;
			shinfo->frags[fragno].size = 0;
			shinfo->frags[fragno].offset = 0;
			shinfo->frags[fragno].page = NULL;
			__free_page(p);
			count++;
		}
	}
	shinfo->nr_frags = 0;
}

int
lkpi___skb_linearize(struct sk_buff *skb)
{
	struct sk_buff *new;

	SKB_TRACE(skb);

	new = skb_copy(skb, GFP_NOWAIT);
	if (new == NULL)
		return (-ENOMEM);

	/* Now need to swap head, data, tail, ... and free from old (and then new). */
	skb_frags_free(skb);
	skb_head_free(skb);

	skb->head = new->head;
	skb->data = new->data;
	skb->tail = new->tail;
	skb->end = new->end;
	skb->len = new->len;
	skb->data_len = new->data_len;
	skb->truesize = new->truesize;

	kfree(new);	/* 9front: its data is skb's now */
	return (0);
}

void
linuxkpi_kfree_skb(struct sk_buff *skb)
{

	SKB_TRACE(skb);
	if (skb == NULL)
		return;

	if (!refcount_dec_and_test(&skb->refcnt)) {
		SKB_TRACE_FMT(skb, "not freed due to refcnt");
		return;
	}

	skb_frags_free(skb);
	skb_head_free(skb);
	kfree(skb);
}
