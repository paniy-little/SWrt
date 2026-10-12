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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

/* Debug memory tracing stub */
#define mem_debug_update_skb(skb) do {} while (0)

/* VLAN traversal */
#ifndef vlan_dev_next_dev
#define vlan_dev_next_dev(dev) (vlan_dev_priv(dev)->real_dev)
#endif

/* Tunnel type identification */
#ifndef netif_is_gre
static inline bool netif_is_gre(const struct net_device *dev)
{
	return dev->type == ARPHRD_IPGRE;
}
#endif

#ifndef netif_is_ip6gre
static inline bool netif_is_ip6gre(const struct net_device *dev)
{
	return dev->type == ARPHRD_IP6GRE;
}
#endif

/* Cisco TrustSec stubs */
struct trustsec_hdr {
	u8 meta_ver;
	u8 flags;
	u16 payload_proto;
	u16 sgt;
} __attribute__((packed));

/* QCA PPPoE manager stubs */
static inline void sfe_pppoe_mgr_init(void) {}
static inline void sfe_pppoe_mgr_exit(void) {}
static inline bool sfe_pppoe_mgr_find_session(u16 sid, u8 *mac) { return false; }

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

/* Generic fallback for fast_xmit */
#ifndef DEV_FAST_XMIT_SUPPORTED
static inline int dev_fast_xmit(struct sk_buff *skb, struct net_device *dev, netdev_features_t features)
{
	skb->dev = dev;
	return dev_queue_xmit(skb);
}
#endif

#endif /* _SFE_COMPAT_H_ */
