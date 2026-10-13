/*
 * sfe_compat.h
 *     Unified compatibility shim for Qualcomm Shortcut-FE v2
 *     Allows building SFE v2 on standard/vanilla Linux kernels (5.15 / 6.1 / 6.6 / 6.12)
 *     without any proprietary Qualcomm hardware or kernel patches.
 */

#ifndef _SFE_COMPAT_H_
#define _SFE_COMPAT_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/if_arp.h>
#include <net/rtnetlink.h>
#include <net/dst.h>
#include <net/route.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#if defined(SFE_SUPPORT_IPV6)
#include <linux/netfilter_ipv6.h>
#endif
#include <net/sch_generic.h>
#include <net/pkt_sched.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

/* Debug memory tracing stub */
#define mem_debug_update_skb(skb) do {} while (0)

/*
 * Kernels carrying the QCA short-circuit patch (CONFIG_SHORTCUT_FE) have a real
 * fast_forwarded flag bit in struct sk_buff which the transmit path uses to keep
 * fast-forwarded packets away from the net taps.  Without that bit the same flag
 * is kept in the skb control buffer, which the fast path owns end to end.
 */
#if !defined(CONFIG_SHORTCUT_FE)
#define fast_forwarded cb[47]
#endif

/* priv_flags_ext - QCA extension to struct net_device */
#define priv_flags_ext priv_flags
#define IFF_EXT_MAPT 0
#define IFF_EXT_ETH_L2TPV3 0

/* VLAN traversal */
#ifndef vlan_dev_next_dev
#define vlan_dev_next_dev(dev) (vlan_dev_priv(dev)->real_dev)
#endif

/* Tunnel type identification */
#ifndef netif_is_gre
static inline bool netif_is_gre(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "gre");
}
#endif

#ifndef netif_is_ip6gre
static inline bool netif_is_ip6gre(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "ip6gre");
}
#endif

/* IPPROTO_ETHERIP */
#ifndef IPPROTO_ETHERIP
#define IPPROTO_ETHERIP 97
#endif

/* dev_kfree_skb_list_fast */
static inline void dev_kfree_skb_list_fast(struct sk_buff_head *list)
{
	__skb_queue_purge(list);
}

/* Cisco TrustSec stubs */
#include "linux/if_trustsec.h"

/* QCA PPPoE manager stubs - supersede sfe_pppoe_mgr.h */
#ifndef _SFE_PPPOE_MGR_H_
#define _SFE_PPPOE_MGR_H_
struct sfe_pppoe_mgr_session_info {
	uint32_t session_id;
	uint8_t server_mac[ETH_ALEN];
};
struct sfe_pppoe_mgr_session_entry {
	struct sfe_pppoe_mgr_session_info info;
	struct net_device *dev;
	struct hlist_node hash_list;
};
static inline bool sfe_pppoe_mgr_find_session(uint16_t sid, uint8_t *mac) { return false; }
static inline int sfe_pppoe_mgr_init(void) { return 0; }
static inline void sfe_pppoe_mgr_exit(void) {}
#endif /* _SFE_PPPOE_MGR_H_ */

/* Linux 6.12 route scope compatibility */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
#undef ip_route_output
static inline struct rtable *sfe_ip_route_output(struct net *net, __be32 daddr, __be32 saddr, u8 tos, int oif)
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
#define ip_route_output(net, daddr, saddr, tos, oif) sfe_ip_route_output(net, daddr, saddr, tos, oif)
#endif

/*
 * Transmit path inspection.
 *
 * All three questions below exist to answer one thing: "would bypassing this
 * device's queue and TC programs change something the user asked for?".  A
 * blacklist of qdisc names cannot answer it: it fires on whatever the
 * distribution installs as its default queueing discipline (OpenWrt builds with
 * CONFIG_DEFAULT_NET_SCH="fq_codel", so every single queue device would be
 * flagged as shaped and fast transmit would never be used), and it misses every
 * discipline that is not spelled out in the list (cake below an mq root, a
 * shaper added by a newer kernel, an out of tree qdisc).
 *
 * The test is therefore inverted: only a queue that cannot be doing anything
 * interesting may be bypassed, namely
 *   - the built in FIFOs (noqueue/noop/pfifo/pfifo_fast),
 *   - the multiqueue container, whose children are inspected instead,
 *   - whatever the kernel installed as net.core.default_qdisc
 *     (CONFIG_DEFAULT_NET_SCH), which is never a user's traffic policy.
 * Everything else keeps its packets.
 *
 * A device is "shaped" for traffic control purposes as soon as it has a TC
 * ingress or egress program attached; note that a bare clsact qdisc is enough,
 * because the kernel creates the tcx entry when clsact is attached even if no
 * filter was added to it.
 *
 * The helpers must be called from an RCU read side section: dev->qdisc,
 * txq->qdisc and dev->tcx_* are all RCU protected.
 */
static inline bool sfe_qdisc_is_trivial(const struct Qdisc *q)
{
	const char *id;

	if (!q || !q->ops)
		return true;

	if (q->ops == default_qdisc_ops || q->ops == &pfifo_fast_ops)
		return true;

	id = q->ops->id;
	return !strcmp(id, "noqueue") || !strcmp(id, "noop");
}

static inline bool sfe_dev_has_shaping_qdisc(struct net_device *dev)
{
	const struct Qdisc *q;
	unsigned int i;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held() && !rcu_read_lock_bh_held(),
			 "sfe: qdisc probe outside of an RCU read side section");
	if (!dev)
		return false;

	q = rcu_dereference(dev->qdisc);
	if (!q || !q->ops)
		return false;

	if (!strcmp(q->ops->id, "mq")) {
		/*
		 * Multiqueue devices keep the real queueing discipline on their
		 * per queue children - that is where SQM attaches cake - so the
		 * root qdisc alone does not say anything.
		 */
		for (i = 0; i < dev->num_tx_queues; i++) {
			const struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

			if (!sfe_qdisc_is_trivial(rcu_dereference(txq->qdisc)))
				return true;
		}
		return false;
	}

	return !sfe_qdisc_is_trivial(q);
}

static inline bool sfe_dev_has_tc_ingress(struct net_device *dev)
{
#ifdef CONFIG_NET_CLS_ACT
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
	return rcu_access_pointer(dev->miniq_ingress) != NULL;
#else
	return rcu_access_pointer(dev->tcx_ingress) != NULL;
#endif
#else
	return false;
#endif
}

static inline bool sfe_dev_has_tc_egress(struct net_device *dev)
{
#ifdef CONFIG_NET_CLS_ACT
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
	return rcu_access_pointer(dev->miniq_egress) != NULL;
#else
	return rcu_access_pointer(dev->tcx_egress) != NULL;
#endif
#else
	return false;
#endif
}

/*
 * Is bypassing this device's transmit path a problem?  Only egress traffic
 * control matters here: the packet is already leaving through the device.
 */
static inline bool sfe_dev_tx_is_shaped(struct net_device *dev)
{
	if (!dev)
		return false;

	return sfe_dev_has_tc_egress(dev) || sfe_dev_has_shaping_qdisc(dev);
}

/*
 * Is bypassing this device a problem in both directions?  Used for devices the
 * engine does not transmit on at all (the top device of a stacked interface
 * such as pppoe-wan when the frame is handed to the bottom eth0): those are
 * skipped for ingress as well, so their TC ingress programs matter too.
 */
static inline bool sfe_dev_is_shaped(struct net_device *dev)
{
	if (!dev)
		return false;

	return sfe_dev_has_tc_ingress(dev) || sfe_dev_tx_is_shaped(dev);
}

/*
 * dev_fast_xmit - Zero-overhead fast transmit path that bypasses Linux qdisc
 * Directly locks TX queue and calls netdev_start_xmit().
 * If TX queue is full or stopped, returns false to fall back to dev_queue_xmit().
 */
#ifndef DEV_FAST_XMIT_SUPPORTED
static inline bool dev_fast_xmit(struct sk_buff *skb,
				 struct net_device *dev,
				 netdev_features_t features)
{
	struct netdev_queue *txq;
	u16 queue_index;
	int cpu;
	netdev_tx_t rc;

	if (unlikely(!(dev->flags & IFF_UP)))
		return false;

	/*
	 * This path skips validate_xmit_skb(), and the flow level check in
	 * sfe_fast_xmit_check() runs only on the first packet of a direction, so
	 * the per packet cases it covers have to be rejected here.  Every test is
	 * a single field of an already hot skb.
	 */
	if (unlikely(skb_vlan_tag_present(skb)))
		return false;

	if (unlikely(skb_sec_path(skb)))
		return false;

#ifdef CONFIG_SOCK_VALIDATE_XMIT
	if (unlikely(skb->sk && sk_fullsock(skb->sk) && skb->sk->sk_validate_xmit_skb))
		return false;
#endif

	if (unlikely(netif_needs_gso(skb, features)))
		return false;

	if (unlikely(skb_needs_linearize(skb, features)))
		return false;

	rcu_read_lock_bh();
	cpu = smp_processor_id();

	/*
	 * Traffic control can be attached to the device at any time, long after this
	 * fast transmit path was enabled for the flow, so the state has to be looked
	 * at for every packet.  This runs inside the RCU section below because
	 * dev->qdisc and the tcx entries are RCU protected; bailing out here leaves
	 * the skb untouched for the caller, which falls back to dev_queue_xmit()
	 * (dst, queue mapping and the reference are all still in place).
	 */
	if (unlikely(sfe_dev_tx_is_shaped(dev)))
		goto fail;
	if (likely(dev->priv_flags & IFF_XMIT_DST_RELEASE)) {
		skb_dst_drop(skb);
	} else {
		if (unlikely(!skb_dst_force(skb)))
			goto fail;
	}

	/*
	 * Mirror netdev_core_pick_tx(): cap the index against this device's queue
	 * count and record it in the skb before netdev_start_xmit().  Drivers index
	 * their TX ring with skb_get_queue_mapping() (ixgbe does so without any
	 * bounds check), and what the skb still carries here is the *ingress*
	 * device's RX queue - one based - which can point past the egress device's
	 * ring when the LAN side has more queues than the WAN side.
	 */
	queue_index = netdev_cap_txqueue(dev, netdev_pick_tx(dev, skb, NULL));
	txq = netdev_get_tx_queue(dev, queue_index);
	skb_set_queue_mapping(skb, queue_index);

	if (likely(txq->xmit_lock_owner != cpu)) {
		HARD_TX_LOCK(dev, txq, cpu);
		if (likely(!netif_xmit_stopped(txq))) {
			skb->dev = dev;
			rc = netdev_start_xmit(skb, dev, txq, 0);
			if (unlikely(!dev_xmit_complete(rc))) {
				HARD_TX_UNLOCK(dev, txq);
				goto fail;
			}
			HARD_TX_UNLOCK(dev, txq);
			rcu_read_unlock_bh();
			return true;
		}
		HARD_TX_UNLOCK(dev, txq);
	}

fail:
	rcu_read_unlock_bh();
	return false;
}

static inline bool dev_fast_xmit_qdisc(struct sk_buff *skb, struct net_device *dev, struct net_device *qdisc_dev)
{
	return false;
}
#endif

#endif /* _SFE_COMPAT_H_ */
