/*
 * sfe_cm.c
 *	Shortcut Forwarding Engine v2 Connection Manager.
 *
 * Copyright (c) 2013-2016 The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/route.h>
#include <net/dst.h>
#include <net/neighbour.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>

#include "sfe_cm.h"
#include "exports/sfe_api.h"

/*
 * Exported by the SFE v2 engine through the generic compatibility patch; they
 * are internal to the engine otherwise.
 */
extern void sfe_ipv4_destroy_all_rules_for_dev(struct net_device *dev);
extern void sfe_ipv6_destroy_all_rules_for_dev(struct net_device *dev);

/*
 * Kernel compatibility shims
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
static inline struct rtable *sfe_cm_ip_route_output(struct net *net, __be32 daddr, __be32 saddr, u8 tos, int oif)
{
	struct flowi4 fl4 = {
		.flowi4_oif = oif,
		.flowi4_tos = tos,
		.flowi4_scope = RT_SCOPE_UNIVERSE,
		.daddr = daddr,
		.saddr = saddr,
	};
	return ip_route_output_key(net, &fl4);
}
#else
#define sfe_cm_ip_route_output(net, daddr, saddr, tos, oif) ip_route_output(net, daddr, saddr, tos, oif)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0))
#define SFE_NF_CT_DEFAULT_ZONE (&nf_ct_zone_dflt)
#else
#define SFE_NF_CT_DEFAULT_ZONE NF_CT_DEFAULT_ZONE
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
#define SFE_DEV_EVENT_PTR(ptr) netdev_notifier_info_to_dev(ptr)
#else
#define SFE_DEV_EVENT_PTR(ptr) ((struct net_device *)(ptr))
#endif

static inline struct net_device *sfe_dev_get_master(struct net_device *dev)
{
	struct net_device *master;
	rcu_read_lock();
	master = netdev_master_upper_dev_get_rcu(dev);
	if (master)
		dev_hold(master);
	rcu_read_unlock();
	return master;
}

static inline void sfe_cm_add_acct(struct nf_conn *ct, int dir, u64 pkts, u64 bytes)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
	struct nf_conn_acct *acct = nf_conn_acct_find(ct);
	if (acct) {
		struct nf_conn_counter *counter = acct->counter;
		atomic64_add(pkts, &counter[dir].packets);
		atomic64_add(bytes, &counter[dir].bytes);
	}
#else
	struct nf_conn_counter *counter = nf_conn_acct_find(ct);
	if (counter) {
		atomic64_add(pkts, &counter[dir].packets);
		atomic64_add(bytes, &counter[dir].bytes);
	}
#endif
}

/*
 * Periodic connection synchronisation.
 *
 * SFE v2 never walks its connection table on its own: the connection manager
 * asks for a batch and the engine answers asynchronously through the many-sync
 * callback.  We have to drive that loop, because the fast path never touches
 * conntrack:
 *   1. every accelerated flow keeps its conntrack entry (and therefore its
 *      timeout) alive instead of being kicked back to the slow path halfway
 *      through,
 *   2. the conntrack packet/byte counters stay meaningful for accounting.
 *
 * A round is (re)started every SFE_CM_SYNC_INTERVAL_MS.  Whenever the engine
 * reports connections left to sync (many->next != 0) the next batch is queued
 * SFE_CM_SYNC_CHAIN_DELAY jiffies later, so a full walk of even a maximum sized
 * (4096 entries) table finishes in about a second at a fixed, tiny CPU cost.
 */
#define SFE_CM_SYNC_INTERVAL_MS	1000
#define SFE_CM_SYNC_CHAIN_DELAY	1

#ifdef SFE_SUPPORT_IPV6
#define SFE_CM_SYNC_FAMILIES	2
#else
#define SFE_CM_SYNC_FAMILIES	1
#endif

/*
 * One round driver per address family: a busy IPv4 table must not drag IPv6
 * (or the other way round) through the same chain of wake-ups.
 */
struct sfe_cm_sync {
	struct delayed_work dwork;
	bool is_v4;
};

struct sfe_cm {
	struct notifier_block dev_notifier;
	struct notifier_block inet_notifier;
	struct notifier_block inet6_notifier;
};

static struct sfe_cm __sc;
static struct sfe_cm_sync sfe_cm_sync[SFE_CM_SYNC_FAMILIES];
static struct sfe_ctx_instance *sfe_v4_ctx;
#ifdef SFE_SUPPORT_IPV6
static struct sfe_ctx_instance *sfe_v6_ctx;
#endif

static void sfe_cm_sync_work(struct work_struct *work);

/*
 * Vanilla kernels have a single conntrack event notifier slot.  We take it over
 * and re-claim it from the periodic work in case somebody registers later.
 */
static void sfe_cm_conntrack_notifier_reclaim(void);

/*
 * SFE v2 API bridges
 */
static int sfe_cm_v2_ipv4_create_rule(struct sfe_connection_create *sic)
{
	struct sfe_ipv4_msg msg;

	if (unlikely(!sfe_v4_ctx))
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	sfe_ipv4_msg_init(&msg, 0, SFE_TX_CREATE_RULE_MSG,
			  sizeof(struct sfe_ipv4_rule_create_msg), NULL, NULL);

	msg.msg.rule_create.valid_flags = SFE_RULE_CREATE_CONN_VALID;
	/*
	 * Ask the engine to try the qdisc-bypassing transmit path in both
	 * directions.  The engine still validates every flow once (GSO, VLAN,
	 * XFRM, socket validation) and silently falls back to dev_queue_xmit()
	 * where the fast path is not usable.
	 *
	 * Trade-off, see README.md: traffic control (SQM/cake shaping, egress
	 * filters, qdisc statistics) is bypassed for accelerated flows.
	 */
	msg.msg.rule_create.rule_flags = SFE_RULE_CREATE_FLAG_ROUTED |
					 SFE_RULE_CREATE_FLAG_FLOW_TRANSMIT_FAST |
					 SFE_RULE_CREATE_FLAG_RETURN_TRANSMIT_FAST;

	/*
	 * A PPPoE side consists of two devices: the bottom one (the real
	 * ingress/egress device SFE decapsulates from / encapsulates into) and
	 * the top one (the pppoe-wan netdev).  Tell the engine which sides have
	 * to be matched and transmitted on the bottom interface.
	 */
	if (sic->flow_is_pppoe)
		msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_USE_FLOW_BOTTOM_INTERFACE;

	if (sic->return_is_pppoe)
		msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_USE_RETURN_BOTTOM_INTERFACE;

	msg.msg.rule_create.tuple.protocol = sic->protocol;
	msg.msg.rule_create.tuple.flow_ip = sic->src_ip.ip;
	msg.msg.rule_create.tuple.return_ip = sic->dest_ip.ip;
	msg.msg.rule_create.tuple.flow_ident = sic->src_port;
	msg.msg.rule_create.tuple.return_ident = sic->dest_port;

	msg.msg.rule_create.conn_rule.flow_ip_xlate = sic->src_ip_xlate.ip;
	msg.msg.rule_create.conn_rule.return_ip_xlate = sic->dest_ip_xlate.ip;
	msg.msg.rule_create.conn_rule.flow_ident_xlate = sic->src_port_xlate;
	msg.msg.rule_create.conn_rule.return_ident_xlate = sic->dest_port_xlate;

	msg.msg.rule_create.conn_rule.flow_top_interface_num = sic->flow_top_dev ? sic->flow_top_dev->ifindex : sic->src_dev->ifindex;
	msg.msg.rule_create.conn_rule.return_top_interface_num = sic->return_top_dev ? sic->return_top_dev->ifindex : sic->dest_dev->ifindex;
	msg.msg.rule_create.conn_rule.flow_interface_num = sic->src_dev->ifindex;
	msg.msg.rule_create.conn_rule.return_interface_num = sic->dest_dev->ifindex;

	msg.msg.rule_create.conn_rule.flow_mtu = sic->src_mtu;
	msg.msg.rule_create.conn_rule.return_mtu = sic->dest_mtu;

	memcpy(msg.msg.rule_create.conn_rule.flow_mac, sic->src_mac, ETH_ALEN);
	memcpy(msg.msg.rule_create.conn_rule.return_mac, sic->dest_mac, ETH_ALEN);

	if (sic->flow_is_pppoe) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_PPPOE_DECAP_VALID;
		msg.msg.rule_create.pppoe_rule.flow_pppoe_session_id = sic->flow_pppoe_session_id;
		memcpy(msg.msg.rule_create.pppoe_rule.flow_pppoe_remote_mac, sic->flow_pppoe_remote_mac, ETH_ALEN);
	}

	if (sic->return_is_pppoe) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_PPPOE_ENCAP_VALID;
		msg.msg.rule_create.pppoe_rule.return_pppoe_session_id = sic->return_pppoe_session_id;
		memcpy(msg.msg.rule_create.pppoe_rule.return_pppoe_remote_mac, sic->return_pppoe_remote_mac, ETH_ALEN);
	}

	if (sic->protocol == IPPROTO_TCP) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_TCP_VALID;
		msg.msg.rule_create.tcp_rule.flow_max_window = sic->src_td_max_window;
		msg.msg.rule_create.tcp_rule.return_max_window = sic->dest_td_max_window;
		msg.msg.rule_create.tcp_rule.flow_end = sic->src_td_end;
		msg.msg.rule_create.tcp_rule.return_end = sic->dest_td_end;
		msg.msg.rule_create.tcp_rule.flow_max_end = sic->src_td_max_end;
		msg.msg.rule_create.tcp_rule.return_max_end = sic->dest_td_max_end;
		msg.msg.rule_create.tcp_rule.flow_window_scale = sic->src_td_window_scale;
		msg.msg.rule_create.tcp_rule.return_window_scale = sic->dest_td_window_scale;

		if (sic->flags & SFE_CREATE_FLAG_NO_SEQ_CHECK) {
			msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_NO_SEQ_CHECK;
		}
	}

	if (sic->flags & SFE_CREATE_FLAG_REMARK_PRIORITY) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_QOS_VALID;
		msg.msg.rule_create.qos_rule.flow_qos_tag = sic->src_priority;
		msg.msg.rule_create.qos_rule.return_qos_tag = sic->dest_priority;
	}

	if (sfe_ipv4_tx_with_resp(sfe_v4_ctx, &msg) != SFE_CMN_RESPONSE_ACK) {
		return -EINVAL;
	}

	return 0;
}

static void sfe_cm_v2_ipv4_destroy_rule(struct sfe_connection_destroy *sid)
{
	struct sfe_ipv4_msg msg;

	if (unlikely(!sfe_v4_ctx))
		return;

	memset(&msg, 0, sizeof(msg));
	sfe_ipv4_msg_init(&msg, 0, SFE_TX_DESTROY_RULE_MSG,
			  sizeof(struct sfe_ipv4_rule_destroy_msg), NULL, NULL);

	msg.msg.rule_destroy.tuple.protocol = sid->protocol;
	msg.msg.rule_destroy.tuple.flow_ip = sid->src_ip.ip;
	msg.msg.rule_destroy.tuple.return_ip = sid->dest_ip.ip;
	msg.msg.rule_destroy.tuple.flow_ident = sid->src_port;
	msg.msg.rule_destroy.tuple.return_ident = sid->dest_port;

	sfe_ipv4_tx_with_resp(sfe_v4_ctx, &msg);
}

#ifdef SFE_SUPPORT_IPV6
static int sfe_cm_v2_ipv6_create_rule(struct sfe_connection_create *sic)
{
	struct sfe_ipv6_msg msg;

	if (unlikely(!sfe_v6_ctx))
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	sfe_ipv6_msg_init(&msg, 0, SFE_TX_CREATE_RULE_MSG,
			  sizeof(struct sfe_ipv6_rule_create_msg), NULL, NULL);

	msg.msg.rule_create.valid_flags = SFE_RULE_CREATE_CONN_VALID;
	/* See sfe_cm_v2_ipv4_create_rule() for the fast transmit trade-off. */
	msg.msg.rule_create.rule_flags = SFE_RULE_CREATE_FLAG_ROUTED |
					 SFE_RULE_CREATE_FLAG_FLOW_TRANSMIT_FAST |
					 SFE_RULE_CREATE_FLAG_RETURN_TRANSMIT_FAST;

	/* See sfe_cm_v2_ipv4_create_rule() for the bottom interface rule. */
	if (sic->flow_is_pppoe)
		msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_USE_FLOW_BOTTOM_INTERFACE;

	if (sic->return_is_pppoe)
		msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_USE_RETURN_BOTTOM_INTERFACE;

	msg.msg.rule_create.tuple.protocol = sic->protocol;
	memcpy(msg.msg.rule_create.tuple.flow_ip, sic->src_ip.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	memcpy(msg.msg.rule_create.tuple.return_ip, sic->dest_ip.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	msg.msg.rule_create.tuple.flow_ident = sic->src_port;
	msg.msg.rule_create.tuple.return_ident = sic->dest_port;

	memcpy(msg.msg.rule_create.conn_rule.flow_ip_xlate, sic->src_ip_xlate.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	memcpy(msg.msg.rule_create.conn_rule.return_ip_xlate, sic->dest_ip_xlate.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	msg.msg.rule_create.conn_rule.flow_ident_xlate = sic->src_port_xlate;
	msg.msg.rule_create.conn_rule.return_ident_xlate = sic->dest_port_xlate;

	msg.msg.rule_create.conn_rule.flow_top_interface_num = sic->flow_top_dev ? sic->flow_top_dev->ifindex : sic->src_dev->ifindex;
	msg.msg.rule_create.conn_rule.return_top_interface_num = sic->return_top_dev ? sic->return_top_dev->ifindex : sic->dest_dev->ifindex;
	msg.msg.rule_create.conn_rule.flow_interface_num = sic->src_dev->ifindex;
	msg.msg.rule_create.conn_rule.return_interface_num = sic->dest_dev->ifindex;

	msg.msg.rule_create.conn_rule.flow_mtu = sic->src_mtu;
	msg.msg.rule_create.conn_rule.return_mtu = sic->dest_mtu;

	memcpy(msg.msg.rule_create.conn_rule.flow_mac, sic->src_mac, ETH_ALEN);
	memcpy(msg.msg.rule_create.conn_rule.return_mac, sic->dest_mac, ETH_ALEN);

	if (sic->flow_is_pppoe) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_PPPOE_DECAP_VALID;
		msg.msg.rule_create.pppoe_rule.flow_pppoe_session_id = sic->flow_pppoe_session_id;
		memcpy(msg.msg.rule_create.pppoe_rule.flow_pppoe_remote_mac, sic->flow_pppoe_remote_mac, ETH_ALEN);
	}

	if (sic->return_is_pppoe) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_PPPOE_ENCAP_VALID;
		msg.msg.rule_create.pppoe_rule.return_pppoe_session_id = sic->return_pppoe_session_id;
		memcpy(msg.msg.rule_create.pppoe_rule.return_pppoe_remote_mac, sic->return_pppoe_remote_mac, ETH_ALEN);
	}

	if (sic->protocol == IPPROTO_TCP) {
		msg.msg.rule_create.valid_flags |= SFE_RULE_CREATE_TCP_VALID;
		msg.msg.rule_create.tcp_rule.flow_max_window = sic->src_td_max_window;
		msg.msg.rule_create.tcp_rule.return_max_window = sic->dest_td_max_window;
		msg.msg.rule_create.tcp_rule.flow_end = sic->src_td_end;
		msg.msg.rule_create.tcp_rule.return_end = sic->dest_td_end;
		msg.msg.rule_create.tcp_rule.flow_max_end = sic->src_td_max_end;
		msg.msg.rule_create.tcp_rule.return_max_end = sic->dest_td_max_end;
		msg.msg.rule_create.tcp_rule.flow_window_scale = sic->src_td_window_scale;
		msg.msg.rule_create.tcp_rule.return_window_scale = sic->dest_td_window_scale;

		if (sic->flags & SFE_CREATE_FLAG_NO_SEQ_CHECK) {
			msg.msg.rule_create.rule_flags |= SFE_RULE_CREATE_FLAG_NO_SEQ_CHECK;
		}
	}

	if (sfe_ipv6_tx_with_resp(sfe_v6_ctx, &msg) != SFE_CMN_RESPONSE_ACK) {
		return -EINVAL;
	}

	return 0;
}

static void sfe_cm_v2_ipv6_destroy_rule(struct sfe_connection_destroy *sid)
{
	struct sfe_ipv6_msg msg;

	if (unlikely(!sfe_v6_ctx))
		return;

	memset(&msg, 0, sizeof(msg));
	sfe_ipv6_msg_init(&msg, 0, SFE_TX_DESTROY_RULE_MSG,
			  sizeof(struct sfe_ipv6_rule_destroy_msg), NULL, NULL);

	msg.msg.rule_destroy.tuple.protocol = sid->protocol;
	memcpy(msg.msg.rule_destroy.tuple.flow_ip, sid->src_ip.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	memcpy(msg.msg.rule_destroy.tuple.return_ip, sid->dest_ip.ip6[0].addr, sizeof(struct sfe_ipv6_addr));
	msg.msg.rule_destroy.tuple.flow_ident = sid->src_port;
	msg.msg.rule_destroy.tuple.return_ident = sid->dest_port;

	sfe_ipv6_tx_with_resp(sfe_v6_ctx, &msg);
}
#endif

/*
 * Device and MAC lookup
 */
static bool sfe_cm_find_dev_and_mac_addr(sfe_ip_addr_t *addr, struct net_device **dev, u8 *mac_addr, int is_v4,
					 bool *is_pppoe, u16 *pppoe_sid, u8 *pppoe_mac, struct net_device **top_dev)
{
	struct neighbour *neigh;
	struct dst_entry *dst;
	struct net_device *mac_dev;

	if (is_pppoe)
		*is_pppoe = false;
	if (top_dev)
		*top_dev = NULL;

	if (likely(is_v4)) {
		struct rtable *rt = sfe_cm_ip_route_output(&init_net, addr->ip, 0, 0, 0);
		if (unlikely(IS_ERR(rt)))
			return false;
		dst = (struct dst_entry *)rt;
		rcu_read_lock();
		neigh = dst_neigh_lookup(dst, &addr->ip);
	} else {
#ifdef SFE_SUPPORT_IPV6
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0))
		struct rt6_info *rt6 = rt6_lookup(&init_net, (struct in6_addr *)addr->ip6, 0, 0, 0);
#else
		struct rt6_info *rt6 = rt6_lookup(&init_net, (struct in6_addr *)addr->ip6, 0, 0, NULL, 0);
#endif
		if (!rt6)
			return false;
		dst = (struct dst_entry *)rt6;
		rcu_read_lock();
		neigh = dst_neigh_lookup(dst, (struct in6_addr *)addr->ip6);
#else
		return false;
#endif
	}

	if (unlikely(!neigh)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0))
		if (dst->dev && (dst->dev->type == ARPHRD_PPP || (dst->dev->flags & IFF_POINTOPOINT))) {
			struct net_device_path_stack stack;
			if (dev_fill_forward_path(dst->dev, NULL, &stack) >= 0) {
				int i;
				for (i = 0; i < stack.num_paths; i++) {
					struct net_device_path *path = &stack.path[i];
					if (path->type == DEV_PATH_PPPOE && path->dev) {
						struct net_device *pppoe_dev = (struct net_device *)path->dev;

						/*
						 * This hop describes the PPPoE netdev the stack
						 * routes through (pppoe-wan); the device the
						 * encapsulated frame has to leave through is the
						 * bottom device of the stack (eth0/eth0.x), which
						 * dev_fill_forward_path() appends as the last hop.
						 *
						 * SFE writes the L2 and PPPoE headers itself, so the
						 * bottom device has to be its interface - handing an
						 * encapsulated frame to the PPPoE netdev would make
						 * the ppp channel encapsulate it a second time.  The
						 * PPPoE netdev is passed along as the top interface.
						 */
						mac_dev = (struct net_device *)stack.path[stack.num_paths - 1].dev;
						if (unlikely(!mac_dev))
							mac_dev = pppoe_dev;

						if (is_pppoe) {
							*is_pppoe = true;
							if (pppoe_sid)
								*pppoe_sid = path->encap.id;
							if (pppoe_mac)
								memcpy(pppoe_mac, path->encap.h_dest, ETH_ALEN);
							if (top_dev) {
								dev_hold(pppoe_dev);
								*top_dev = pppoe_dev;
							}
						}
						memcpy(mac_addr, path->encap.h_dest, ETH_ALEN);
						dev_hold(mac_dev);
						*dev = mac_dev;
						rcu_read_unlock();
						dst_release(dst);
						return true;
					}
				}
			}
		}
#endif
		rcu_read_unlock();
		dst_release(dst);
		return false;
	}

	if (unlikely(!(neigh->nud_state & NUD_VALID))) {
		rcu_read_unlock();
		neigh_release(neigh);
		dst_release(dst);
		return false;
	}

	mac_dev = neigh->dev;
	if (!mac_dev) {
		rcu_read_unlock();
		neigh_release(neigh);
		dst_release(dst);
		return false;
	}

	memcpy(mac_addr, neigh->ha, ETH_ALEN);
	dev_hold(mac_dev);
	*dev = mac_dev;

	rcu_read_unlock();
	neigh_release(neigh);
	dst_release(dst);
	return true;
}

/*
 * Stats synchronization
 */
static void sfe_cm_sync_rule(struct sfe_connection_sync *sis)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;

	memset(&tuple, 0, sizeof(tuple));
	tuple.src.u.all = (__be16)sis->src_port;
	tuple.dst.dir = IP_CT_DIR_ORIGINAL;
	tuple.dst.protonum = (u8)sis->protocol;
	tuple.dst.u.all = (__be16)sis->dest_port;

	if (sis->is_v6) {
		tuple.src.u3.in6 = *((struct in6_addr *)sis->src_ip.ip6);
		tuple.dst.u3.in6 = *((struct in6_addr *)sis->dest_ip.ip6);
		tuple.src.l3num = AF_INET6;
	} else {
		tuple.src.u3.ip = sis->src_ip.ip;
		tuple.dst.u3.ip = sis->dest_ip.ip;
		tuple.src.l3num = AF_INET;
	}

	h = nf_conntrack_find_get(&init_net, SFE_NF_CT_DEFAULT_ZONE, &tuple);
	if (unlikely(!h))
		return;

	ct = nf_ct_tuplehash_to_ctrack(h);

	if (!test_bit(IPS_FIXED_TIMEOUT_BIT, &ct->status)) {
		spin_lock_bh(&ct->lock);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
		ct->timeout.expires += sis->delta_jiffies;
#else
		ct->timeout += sis->delta_jiffies;
#endif
		spin_unlock_bh(&ct->lock);
	}

	sfe_cm_add_acct(ct, IP_CT_DIR_ORIGINAL, sis->src_new_packet_count, sis->src_new_byte_count);
	sfe_cm_add_acct(ct, IP_CT_DIR_REPLY, sis->dest_new_packet_count, sis->dest_new_byte_count);

	nf_ct_put(ct);
}

static void sfe_cm_v2_ipv4_sync_many_callback(void *app_data, struct sfe_ipv4_msg *msg)
{
	struct sfe_ipv4_conn_sync_many_msg *many;
	int i;

	if (unlikely(!msg || msg->cm.type != SFE_TX_CONN_STATS_SYNC_MANY_MSG))
		return;

	many = &msg->msg.conn_stats_many;
	for (i = 0; i < many->count; i++) {
		struct sfe_ipv4_conn_sync *s = &many->conn_sync[i];
		struct sfe_connection_sync sis;

		memset(&sis, 0, sizeof(sis));
		sis.is_v6 = 0;
		sis.protocol = s->protocol;
		sis.src_ip.ip = s->flow_ip;
		sis.dest_ip.ip = s->return_ip;
		sis.src_port = s->flow_ident;
		sis.dest_port = s->return_ident;
		sis.src_new_packet_count = s->flow_rx_packet_count;
		sis.src_new_byte_count = s->flow_rx_byte_count;
		sis.dest_new_packet_count = s->return_rx_packet_count;
		sis.dest_new_byte_count = s->return_rx_byte_count;
		sis.reason = s->reason;
		/*
		 * The engine reports the elapsed time in milliseconds; conntrack
		 * timeouts are in jiffies.
		 */
		sis.delta_jiffies = msecs_to_jiffies(s->inc_ticks);

		sfe_cm_sync_rule(&sis);
	}

	/*
	 * Connections are left: keep the walk of this family going instead of
	 * waiting for the next round.
	 */
	if (many->next)
		mod_delayed_work(system_wq, &sfe_cm_sync[0].dwork, SFE_CM_SYNC_CHAIN_DELAY);
}

#ifdef SFE_SUPPORT_IPV6
static void sfe_cm_v2_ipv6_sync_many_callback(void *app_data, struct sfe_ipv6_msg *msg)
{
	struct sfe_ipv6_conn_sync_many_msg *many;
	int i;

	if (unlikely(!msg || msg->cm.type != SFE_TX_CONN_STATS_SYNC_MANY_MSG))
		return;

	many = &msg->msg.conn_stats_many;
	for (i = 0; i < many->count; i++) {
		struct sfe_ipv6_conn_sync *s = &many->conn_sync[i];
		struct sfe_connection_sync sis;

		memset(&sis, 0, sizeof(sis));
		sis.is_v6 = 1;
		sis.protocol = s->protocol;
		memcpy(sis.src_ip.ip6[0].addr, s->flow_ip, sizeof(struct sfe_ipv6_addr));
		memcpy(sis.dest_ip.ip6[0].addr, s->return_ip, sizeof(struct sfe_ipv6_addr));
		sis.src_port = s->flow_ident;
		sis.dest_port = s->return_ident;
		sis.src_new_packet_count = s->flow_rx_packet_count;
		sis.src_new_byte_count = s->flow_rx_byte_count;
		sis.dest_new_packet_count = s->return_rx_packet_count;
		sis.dest_new_byte_count = s->return_rx_byte_count;
		sis.reason = s->reason;
		/* The engine reports the elapsed time in milliseconds. */
		sis.delta_jiffies = msecs_to_jiffies(s->inc_ticks);

		sfe_cm_sync_rule(&sis);
	}

	if (many->next)
		mod_delayed_work(system_wq, &sfe_cm_sync[1].dwork, SFE_CM_SYNC_CHAIN_DELAY);
}
#endif

/*
 * sfe_cm_sync_request()
 *	Ask the engine for one batch of connection statistics.
 *
 * The engine answers through sfe_cm_v2_ipvX_sync_many_callback(), which runs in
 * workqueue context, so this is a cheap hand-off and not a walk of our own.
 */
static void sfe_cm_sync_request(bool is_v4)
{
	if (is_v4) {
		struct sfe_ipv4_msg msg;

		if (unlikely(!sfe_v4_ctx))
			return;

		memset(&msg, 0, sizeof(msg));
		sfe_ipv4_msg_init(&msg, 0, SFE_TX_CONN_STATS_SYNC_MANY_MSG,
				  sizeof(struct sfe_ipv4_conn_sync_many_msg), NULL, NULL);
		sfe_ipv4_tx(sfe_v4_ctx, &msg);
		return;
	}

#ifdef SFE_SUPPORT_IPV6
	{
		struct sfe_ipv6_msg msg;

		if (unlikely(!sfe_v6_ctx))
			return;

		memset(&msg, 0, sizeof(msg));
		sfe_ipv6_msg_init(&msg, 0, SFE_TX_CONN_STATS_SYNC_MANY_MSG,
				  sizeof(struct sfe_ipv6_conn_sync_many_msg), NULL, NULL);
		sfe_ipv6_tx(sfe_v6_ctx, &msg);
	}
#endif
}

/*
 * sfe_cm_sync_work()
 *	Round driver for the connection synchronisation of one address family.
 */
static void sfe_cm_sync_work(struct work_struct *work)
{
	struct sfe_cm_sync *sync = container_of(work, struct sfe_cm_sync, dwork.work);

	/*
	 * Make sure we still own the conntrack event notifier (vanilla kernels
	 * only, a no-op otherwise).
	 */
	sfe_cm_conntrack_notifier_reclaim();

	sfe_cm_sync_request(sync->is_v4);

	/*
	 * Batches that report work left re-queue this work earlier; this is only
	 * the "start a new round" and lost-response safety net.
	 */
	schedule_delayed_work(&sync->dwork, msecs_to_jiffies(SFE_CM_SYNC_INTERVAL_MS));
}

/*
 * Post-routing packet inspection and rule creation
 */
static unsigned int sfe_cm_post_routing(struct sk_buff *skb, int is_v4)
{
	struct sfe_connection_create sic;
	struct net_device *in, *src_dev, *dest_dev;
	struct net_device *src_dev_tmp = NULL, *dest_dev_tmp = NULL;
	struct net_device *src_br_dev = NULL, *dest_br_dev = NULL;
	struct net_device *dev;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	struct nf_conntrack_tuple orig_tuple, reply_tuple;

	if (unlikely(skb->pkt_type == PACKET_BROADCAST || skb->pkt_type == PACKET_MULTICAST))
		return NF_ACCEPT;

	if (skb->sk)
		return NF_ACCEPT;

	in = dev_get_by_index(&init_net, skb->skb_iif);
	if (!in)
		return NF_ACCEPT;
	dev_put(in);

	ct = nf_ct_get(skb, &ctinfo);
	if (unlikely(!ct))
		return NF_ACCEPT;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	if (unlikely(nf_ct_is_untracked(ct)))
		return NF_ACCEPT;
#else
	if (unlikely(ctinfo == IP_CT_UNTRACKED))
		return NF_ACCEPT;
#endif

	if (!nf_ct_is_confirmed(ct))
		return NF_ACCEPT;

	if (unlikely(nf_ct_is_dying(ct)))
		return NF_ACCEPT;

	if (unlikely(nfct_help(ct)))
		return NF_ACCEPT;

	orig_tuple = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	reply_tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	memset(&sic, 0, sizeof(sic));
	sic.protocol = orig_tuple.dst.protonum;

	if (is_v4) {
		sic.src_ip.ip = orig_tuple.src.u3.ip;
		sic.dest_ip.ip = orig_tuple.dst.u3.ip;
		sic.src_ip_xlate.ip = reply_tuple.dst.u3.ip;
		sic.dest_ip_xlate.ip = reply_tuple.src.u3.ip;
	} else {
#ifdef SFE_SUPPORT_IPV6
		sic.src_ip.ip6[0] = *((struct sfe_ipv6_addr *)&orig_tuple.src.u3.in6);
		sic.dest_ip.ip6[0] = *((struct sfe_ipv6_addr *)&orig_tuple.dst.u3.in6);
		sic.src_ip_xlate.ip6[0] = *((struct sfe_ipv6_addr *)&reply_tuple.dst.u3.in6);
		sic.dest_ip_xlate.ip6[0] = *((struct sfe_ipv6_addr *)&reply_tuple.src.u3.in6);
#else
		return NF_ACCEPT;
#endif
	}

	switch (sic.protocol) {
	case IPPROTO_TCP:
		sic.src_port = orig_tuple.src.u.tcp.port;
		sic.dest_port = orig_tuple.dst.u.tcp.port;
		sic.src_port_xlate = reply_tuple.dst.u.tcp.port;
		sic.dest_port_xlate = reply_tuple.src.u.tcp.port;
		sic.src_td_window_scale = ct->proto.tcp.seen[0].td_scale;
		sic.src_td_max_window = ct->proto.tcp.seen[0].td_maxwin;
		sic.src_td_end = ct->proto.tcp.seen[0].td_end;
		sic.src_td_max_end = ct->proto.tcp.seen[0].td_maxend;
		sic.dest_td_window_scale = ct->proto.tcp.seen[1].td_scale;
		sic.dest_td_max_window = ct->proto.tcp.seen[1].td_maxwin;
		sic.dest_td_end = ct->proto.tcp.seen[1].td_end;
		sic.dest_td_max_end = ct->proto.tcp.seen[1].td_maxend;

		if (!test_bit(IPS_ASSURED_BIT, &ct->status))
			return NF_ACCEPT;

		spin_lock_bh(&ct->lock);
		if (ct->proto.tcp.state != TCP_CONNTRACK_ESTABLISHED) {
			spin_unlock_bh(&ct->lock);
			return NF_ACCEPT;
		}
		spin_unlock_bh(&ct->lock);
		break;

	case IPPROTO_UDP:
		sic.src_port = orig_tuple.src.u.udp.port;
		sic.dest_port = orig_tuple.dst.u.udp.port;
		sic.src_port_xlate = reply_tuple.dst.u.udp.port;
		sic.dest_port_xlate = reply_tuple.src.u.udp.port;
		break;

	default:
		return NF_ACCEPT;
	}

	if (skb->priority) {
		sic.dest_priority = skb->priority;
		sic.src_priority = sic.dest_priority;
		sic.flags |= SFE_CREATE_FLAG_REMARK_PRIORITY;
	}

	if (!sfe_cm_find_dev_and_mac_addr(&sic.src_ip, &src_dev_tmp, sic.src_mac, is_v4,
					  &sic.flow_is_pppoe, &sic.flow_pppoe_session_id, sic.flow_pppoe_remote_mac, &sic.flow_top_dev))
		return NF_ACCEPT;
	src_dev = src_dev_tmp;

	if (!sfe_cm_find_dev_and_mac_addr(&sic.src_ip_xlate, &dev, sic.src_mac_xlate, is_v4, NULL, NULL, NULL, NULL))
		goto done1;
	dev_put(dev);

	if (!sfe_cm_find_dev_and_mac_addr(&sic.dest_ip, &dev, sic.dest_mac, is_v4, NULL, NULL, NULL, NULL))
		goto done1;
	dev_put(dev);

	if (!sfe_cm_find_dev_and_mac_addr(&sic.dest_ip_xlate, &dest_dev_tmp, sic.dest_mac_xlate, is_v4,
					  &sic.return_is_pppoe, &sic.return_pppoe_session_id, sic.return_pppoe_remote_mac, &sic.return_top_dev))
		goto done1;
	dest_dev = dest_dev_tmp;

	if (src_dev->priv_flags & IFF_BRIDGE_PORT) {
		src_br_dev = sfe_dev_get_master(src_dev);
		if (!src_br_dev)
			goto done2;
		src_dev = src_br_dev;
	}

	if (dest_dev->priv_flags & IFF_BRIDGE_PORT) {
		dest_br_dev = sfe_dev_get_master(dest_dev);
		if (!dest_br_dev)
			goto done3;
		dest_dev = dest_br_dev;
	}

	sic.src_dev = src_dev;
	sic.dest_dev = dest_dev;
	sic.src_mtu = src_dev->mtu;
	sic.dest_mtu = dest_dev->mtu;

	if (likely(is_v4)) {
		sfe_cm_v2_ipv4_create_rule(&sic);
	} else {
#ifdef SFE_SUPPORT_IPV6
		sfe_cm_v2_ipv6_create_rule(&sic);
#endif
	}

	if (dest_br_dev)
		dev_put(dest_br_dev);
done3:
	if (src_br_dev)
		dev_put(src_br_dev);
done2:
	dev_put(dest_dev_tmp);
done1:
	if (sic.flow_top_dev)
		dev_put(sic.flow_top_dev);
	if (sic.return_top_dev)
		dev_put(sic.return_top_dev);
	dev_put(src_dev_tmp);

	return NF_ACCEPT;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static unsigned int sfe_cm_ipv4_post_routing_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	return sfe_cm_post_routing(skb, 1);
}

#ifdef SFE_SUPPORT_IPV6
static unsigned int sfe_cm_ipv6_post_routing_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *state)
{
	return sfe_cm_post_routing(skb, 0);
}
#endif
#else
static unsigned int sfe_cm_ipv4_post_routing_hook(unsigned int hooknum, struct sk_buff *skb,
						  const struct net_device *in, const struct net_device *out,
						  int (*okfn)(struct sk_buff *))
{
	return sfe_cm_post_routing(skb, 1);
}

#ifdef SFE_SUPPORT_IPV6
static unsigned int sfe_cm_ipv6_post_routing_hook(unsigned int hooknum, struct sk_buff *skb,
						  const struct net_device *in, const struct net_device *out,
						  int (*okfn)(struct sk_buff *))
{
	return sfe_cm_post_routing(skb, 0);
}
#endif
#endif

/*
 * Conntrack destroy event monitoring
 *
 * The engine only hands a rule back when its conntrack entry dies, so this is
 * the normal way accelerated flows are torn down.
 *
 * Kernels built with the QCA chain-events support (CONFIG_NF_CONNTRACK_CHAIN_EVENTS,
 * from the multi-registrant patch) give every user its own notifier.  Vanilla
 * kernels expose a single global slot instead: we take it over, forward the
 * events we consume to its previous owner (in practice ctnetlink) so that both
 * keep working, and re-claim it from the periodic work in case somebody
 * registers after us.
 */
static void sfe_cm_conntrack_destroy(unsigned int events, const struct nf_ct_event *item)
{
	struct sfe_connection_destroy sid;
	struct nf_conn *ct;
	struct nf_conntrack_tuple orig_tuple;

	if (unlikely(!(events & (1 << IPCT_DESTROY))))
		return;

	if (unlikely(!item || !item->ct))
		return;

	ct = item->ct;

	orig_tuple = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	sid.protocol = orig_tuple.dst.protonum;

	switch (sid.protocol) {
	case IPPROTO_TCP:
		sid.src_port = orig_tuple.src.u.tcp.port;
		sid.dest_port = orig_tuple.dst.u.tcp.port;
		break;
	case IPPROTO_UDP:
		sid.src_port = orig_tuple.src.u.udp.port;
		sid.dest_port = orig_tuple.dst.u.udp.port;
		break;
	default:
		return;
	}

	if (likely(nf_ct_l3num(ct) == AF_INET)) {
		sid.src_ip.ip = orig_tuple.src.u3.ip;
		sid.dest_ip.ip = orig_tuple.dst.u3.ip;
		sfe_cm_v2_ipv4_destroy_rule(&sid);
	} else if (likely(nf_ct_l3num(ct) == AF_INET6)) {
#ifdef SFE_SUPPORT_IPV6
		sid.src_ip.ip6[0] = *((struct sfe_ipv6_addr *)&orig_tuple.src.u3.in6);
		sid.dest_ip.ip6[0] = *((struct sfe_ipv6_addr *)&orig_tuple.dst.u3.in6);
		sfe_cm_v2_ipv6_destroy_rule(&sid);
#endif
	}
}

#ifdef CONFIG_NF_CONNTRACK_CHAIN_EVENTS
static int sfe_cm_conntrack_event(struct notifier_block *this, unsigned long events, void *ptr)
{
	sfe_cm_conntrack_destroy((unsigned int)events, ptr);
	return NOTIFY_DONE;
}

static struct notifier_block sfe_cm_conntrack_notifier = {
	.notifier_call = sfe_cm_conntrack_event,
};

static void sfe_cm_conntrack_notifier_reclaim(void)
{
}
#else
static const struct nf_ct_event_notifier __rcu *sfe_cm_prev_notifier;
static struct nf_ct_event_notifier sfe_cm_conntrack_notifier;

static int sfe_cm_ct_event(unsigned int events, const struct nf_ct_event *item)
{
	const struct nf_ct_event_notifier *prev;
	int ret = 0;

	sfe_cm_conntrack_destroy(events, item);

	/* Keep the previous owner of the slot (ctnetlink) functional. */
	rcu_read_lock();
	prev = rcu_dereference(sfe_cm_prev_notifier);
	if (prev && prev != &sfe_cm_conntrack_notifier && prev->ct_event)
		ret = prev->ct_event(events, item);
	rcu_read_unlock();

	return ret;
}

static int sfe_cm_exp_event(unsigned int events, const struct nf_exp_event *item)
{
	const struct nf_ct_event_notifier *prev;
	int ret = 0;

	rcu_read_lock();
	prev = rcu_dereference(sfe_cm_prev_notifier);
	if (prev && prev->exp_event)
		ret = prev->exp_event(events, item);
	rcu_read_unlock();

	return ret;
}

static struct nf_ct_event_notifier sfe_cm_conntrack_notifier = {
	.ct_event = sfe_cm_ct_event,
	.exp_event = sfe_cm_exp_event,
};

/*
 * sfe_cm_conntrack_notifier_reclaim()
 *	Own the (single) conntrack event notifier slot.
 *
 * Clearing the slot before taking it keeps the kernel from warning about
 * overwriting a live registration; the previous owner is remembered so that its
 * events can be forwarded.
 */
static void sfe_cm_conntrack_notifier_reclaim(void)
{
	const struct nf_ct_event_notifier *cur;

	cur = rcu_access_pointer(init_net.ct.nf_conntrack_event_cb);
	if (cur == &sfe_cm_conntrack_notifier)
		return;

	rcu_assign_pointer(sfe_cm_prev_notifier, cur);
	nf_conntrack_unregister_notifier(&init_net);
	nf_conntrack_register_notifier(&init_net, &sfe_cm_conntrack_notifier);
}

/*
 * sfe_cm_conntrack_notifier_release()
 *	Hand the notifier slot back to its previous owner.
 */
static void sfe_cm_conntrack_notifier_release(void)
{
	const struct nf_ct_event_notifier *prev;

	if (rcu_access_pointer(init_net.ct.nf_conntrack_event_cb) != &sfe_cm_conntrack_notifier)
		return;

	rcu_read_lock();
	prev = rcu_dereference(sfe_cm_prev_notifier);
	rcu_read_unlock();

	nf_conntrack_unregister_notifier(&init_net);
	if (prev)
		nf_conntrack_register_notifier(&init_net, prev);
}
#endif

static struct nf_hook_ops sfe_cm_ops_post_routing[] __read_mostly = {
	{
		.hook = sfe_cm_ipv4_post_routing_hook,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = NF_IP_PRI_NAT_SRC + 1,
	},
#ifdef SFE_SUPPORT_IPV6
	{
		.hook = sfe_cm_ipv6_post_routing_hook,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = NF_IP6_PRI_NAT_SRC + 1,
	},
#endif
};

/*
 * sfe_cm_flush_dev()
 *	Drop every accelerated flow that uses a device.
 *
 * An accelerated rule caches the device, its MAC and (for PPPoE) the session ID
 * and peer MAC, so it must not outlive the device it was built on: after a WAN
 * flap it would keep encapsulating with stale information.  The engine holds
 * device references, so the pointers stay valid but useless - it has to be told
 * explicitly.
 */
static void sfe_cm_flush_dev(struct net_device *dev)
{
	if (unlikely(!dev))
		return;

	sfe_ipv4_destroy_all_rules_for_dev(dev);

#ifdef SFE_SUPPORT_IPV6
	sfe_ipv6_destroy_all_rules_for_dev(dev);
#endif
}

static int sfe_cm_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = SFE_DEV_EVENT_PTR(ptr);

	/*
	 * Rules cache the device, its MAC and the device features captured when the
	 * rule was built, so any change of those has to drop them: DOWN/UNREGISTER
	 * for the device going away, CHANGEADDR for its MAC, FEAT_CHANGE/CHANGEMTU
	 * for the transmit path changing underneath the cached features.
	 */
	if (dev && (event == NETDEV_DOWN || event == NETDEV_UNREGISTER ||
		    event == NETDEV_CHANGEADDR || event == NETDEV_FEAT_CHANGE ||
		    event == NETDEV_CHANGEMTU))
		sfe_cm_flush_dev(dev);

	return NOTIFY_DONE;
}

static int sfe_cm_inet_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;

	/*
	 * Address notifiers report NETDEV_DOWN when an address is removed, and
	 * always carry the in_ifaddr that is going away.
	 */
	if (ifa && ifa->ifa_dev && (event == NETDEV_DOWN))
		sfe_cm_flush_dev(ifa->ifa_dev->dev);

	return NOTIFY_DONE;
}

static int sfe_cm_inet6_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;

	if (ifa && ifa->idev && (event == NETDEV_DOWN))
		sfe_cm_flush_dev(ifa->idev->dev);

	return NOTIFY_DONE;
}

static int __init sfe_cm_init(void)
{
	struct sfe_cm *sc = &__sc;
	int result = -1;

	pr_info("sfe_cm: Qualcomm Shortcut-FE v2 Connection Manager loading\n");

	sfe_cm_sync[0].is_v4 = true;
	INIT_DELAYED_WORK(&sfe_cm_sync[0].dwork, sfe_cm_sync_work);

#ifdef SFE_SUPPORT_IPV6
	sfe_cm_sync[1].is_v4 = false;
	INIT_DELAYED_WORK(&sfe_cm_sync[1].dwork, sfe_cm_sync_work);
#endif

	sfe_v4_ctx = sfe_ipv4_notify_register(NULL, sfe_cm_v2_ipv4_sync_many_callback, NULL);
	if (!sfe_v4_ctx) {
		pr_err("sfe_cm: failed to register with SFE v2 IPv4 engine\n");
		return -ENODEV;
	}

#ifdef SFE_SUPPORT_IPV6
	sfe_v6_ctx = sfe_ipv6_notify_register(NULL, sfe_cm_v2_ipv6_sync_many_callback, NULL);
	if (!sfe_v6_ctx) {
		pr_warn("sfe_cm: failed to register with SFE v2 IPv6 engine\n");
	}
#endif

	sc->dev_notifier.notifier_call = sfe_cm_device_event;
	sc->dev_notifier.priority = 1;
	register_netdevice_notifier(&sc->dev_notifier);

	sc->inet_notifier.notifier_call = sfe_cm_inet_event;
	sc->inet_notifier.priority = 1;
	register_inetaddr_notifier(&sc->inet_notifier);

	sc->inet6_notifier.notifier_call = sfe_cm_inet6_event;
	sc->inet6_notifier.priority = 1;
	register_inet6addr_notifier(&sc->inet6_notifier);

#ifdef CONFIG_NF_CONNTRACK_CHAIN_EVENTS
	result = nf_conntrack_register_notifier(&init_net, &sfe_cm_conntrack_notifier);
	if (result < 0) {
		pr_err("sfe_cm: failed to register conntrack notifier: %d\n", result);
		goto exit_fail;
	}
#else
	sfe_cm_conntrack_notifier_reclaim();
#endif

	result = nf_register_net_hooks(&init_net, sfe_cm_ops_post_routing, ARRAY_SIZE(sfe_cm_ops_post_routing));
	if (result < 0) {
		pr_err("sfe_cm: failed to register post routing hooks: %d\n", result);
#ifdef CONFIG_NF_CONNTRACK_CHAIN_EVENTS
		nf_conntrack_unregister_notifier(&init_net, &sfe_cm_conntrack_notifier);
#else
		sfe_cm_conntrack_notifier_release();
#endif
		goto exit_fail;
	}

	schedule_delayed_work(&sfe_cm_sync[0].dwork, msecs_to_jiffies(SFE_CM_SYNC_INTERVAL_MS));

#ifdef SFE_SUPPORT_IPV6
	schedule_delayed_work(&sfe_cm_sync[1].dwork, msecs_to_jiffies(SFE_CM_SYNC_INTERVAL_MS));
#endif

	pr_info("sfe_cm: SFE v2 Connection Manager successfully initialized\n");
	return 0;

exit_fail:
	unregister_inet6addr_notifier(&sc->inet6_notifier);
	unregister_inetaddr_notifier(&sc->inet_notifier);
	unregister_netdevice_notifier(&sc->dev_notifier);
	sfe_ipv4_notify_unregister();
#ifdef SFE_SUPPORT_IPV6
	if (sfe_v6_ctx)
		sfe_ipv6_notify_unregister();
#endif
	return result;
}

static void __exit sfe_cm_exit(void)
{
	struct sfe_cm *sc = &__sc;

	pr_info("sfe_cm: unloading\n");

	cancel_delayed_work_sync(&sfe_cm_sync[0].dwork);

#ifdef SFE_SUPPORT_IPV6
	cancel_delayed_work_sync(&sfe_cm_sync[1].dwork);
#endif

	nf_unregister_net_hooks(&init_net, sfe_cm_ops_post_routing, ARRAY_SIZE(sfe_cm_ops_post_routing));
#ifdef CONFIG_NF_CONNTRACK_CHAIN_EVENTS
	nf_conntrack_unregister_notifier(&init_net, &sfe_cm_conntrack_notifier);
#else
	sfe_cm_conntrack_notifier_release();
#endif

	unregister_inet6addr_notifier(&sc->inet6_notifier);
	unregister_inetaddr_notifier(&sc->inet_notifier);
	unregister_netdevice_notifier(&sc->dev_notifier);

	sfe_ipv4_notify_unregister();
#ifdef SFE_SUPPORT_IPV6
	if (sfe_v6_ctx)
		sfe_ipv6_notify_unregister();
#endif

	synchronize_rcu();
}

module_init(sfe_cm_init);
module_exit(sfe_cm_exit);

MODULE_AUTHOR("Qualcomm Atheros, Inc.");
MODULE_DESCRIPTION("Shortcut Forwarding Engine v2 Connection Manager");
MODULE_LICENSE("Dual BSD/GPL");
