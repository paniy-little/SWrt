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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

/* Debug memory tracing stub */
#define mem_debug_update_skb(skb) do {} while (0)

/* fast_forwarded stub for unpatched kernels */
#ifndef fast_forwarded
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

	if (unlikely(skb_needs_linearize(skb, features)))
		return false;

	rcu_read_lock_bh();
	cpu = smp_processor_id();

	if (likely(dev->priv_flags & IFF_XMIT_DST_RELEASE)) {
		skb_dst_drop(skb);
	} else {
		if (unlikely(!skb_dst_force(skb)))
			goto fail;
	}

	queue_index = netdev_pick_tx(dev, skb, NULL);
	txq = netdev_get_tx_queue(dev, queue_index);

	if (likely(txq->xmit_lock_owner != cpu)) {
		HARD_TX_LOCK(dev, txq, cpu);
		if (likely(!netif_xmit_stopped(txq))) {
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
