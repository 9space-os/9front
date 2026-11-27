/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020-2021 The FreeBSD Foundation
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
 * LinuxKPI for 9front: FreeBSD's linux/ip.h (freebsd/, 584f44221f45),
 * changed where marked 9front: no FreeBSD headers; the header's checksum computed here.
 */
#ifndef	_LINUXKPI_LINUX_IP_H
#define	_LINUXKPI_LINUX_IP_H



#include <linux/skbuff.h>

/* 9front: the protocol numbers of netinet/in.h the drivers test */
#define	IPPROTO_ICMP	1
#define	IPPROTO_TCP	6
#define	IPPROTO_UDP	17

/* (u) unconfirmed structure field names; using FreeBSD's meanwhile. */
struct iphdr {
	uint8_t		ip_hl:4, ip_ver:4;	/* (u) */
	uint8_t		ip_tos;			/* (u) */
	uint16_t	ip_len;			/* (u) */
	uint16_t	id;
	uint16_t	ip_off;			/* (u) */
	uint8_t		ip_ttl;			/* (u) */
	uint8_t		protocol;
	uint16_t	check;
	uint32_t	saddr;
	uint32_t	daddr;
};

static __inline struct iphdr *
ip_hdr(struct sk_buff *skb)
{

	return (struct iphdr *)skb_network_header(skb);
}

/* 9front: the one's complement sum of the header's 16-bit words */
static __inline uint16_t
kpi_ip_cksum(const struct iphdr *iph)
{
	const uint16_t *w = (const uint16_t *)iph;
	uint32_t sum = 0;
	int i;

	for (i = 0; i < iph->ip_hl * 2; i++)
		sum += w[i];
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (~sum);
}

static __inline void
ip_send_check(struct iphdr *iph)
{

	/* Clear the checksum before computing! */
	iph->check = 0;
	/* An IPv4 header is the same everywhere even if names differ. */
	iph->check = kpi_ip_cksum(iph);	/* 9front */
}

#endif	/* _LINUXKPI_LINUX_IP_H */
