include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=qca-nss-ecm
PKG_VERSION:=14.0.1.00011
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://git.codelinaro.org/clo/qsdk/oss/lklm/qca-nss-ecm.git
PKG_SOURCE_VERSION:=NHSS.QSDK.14.0.r1-00011-O
PKG_MIRROR_HASH:=skip

include $(INCLUDE_DIR)/package.mk

define KernelPackage/qca-nss-ecm
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Network Support
  TITLE:=Qualcomm ECM (SFE Mode) - Upstream
  DEPENDS:=+kmod-qca-nss-sfe +kmod-nf-conntrack
  FILES:=$(PKG_BUILD_DIR)/ecm.ko
endef

define KernelPackage/qca-nss-ecm/description
  Enhanced Connection Manager for SFE-only software acceleration.
  All hardware (NSS/PPE) and proprietary classifiers DISABLED.
  Module loading controlled by /etc/init.d/sfe-ecm based on firewall config.
endef

define KernelPackage/qca-nss-ecm/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/sfe-ecm.init $(1)/etc/init.d/sfe-ecm
endef

PKG_BUILD_DEPENDS:=qca-nss-sfe

# 强制配置：SFE 模式，禁用硬件加速，通用 SoC
ECM_MAKE_OPTS:= \
	ECM_FRONT_END_SFE_ENABLE=y \
	ECM_FRONT_END_NSS_ENABLE=n \
	ECM_FRONT_END_PPE_ENABLE=n \
	ECM_NON_PORTED_SUPPORT_ENABLE=y \
	ECM_IPV6_ENABLE=y \
	ECM_MULTICAST_ENABLE=n \
	ECM_INTERFACE_VLAN_ENABLE=y \
	ECM_INTERFACE_PPPOE_ENABLE=y \
	ECM_INTERFACE_L2TPV2_ENABLE=y \
	ECM_INTERFACE_PPTP_ENABLE=y \
	ECM_INTERFACE_GRE_ENABLE=y \
	ECM_INTERFACE_GRE_TAP_ENABLE=y \
	ECM_INTERFACE_GRE_TUN_ENABLE=y \
	ECM_INTERFACE_SIT_ENABLE=y \
	ECM_INTERFACE_TUNIPIP6_ENABLE=y \
	ECM_INTERFACE_RAWIP_ENABLE=y \
	ECM_INTERFACE_VXLAN_ENABLE=y \
	ECM_CLASSIFIER_DSCP_ENABLE=n \
	ECM_CLASSIFIER_NL_ENABLE=n \
	ECM_CLASSIFIER_MARK_ENABLE=y \
	ECM_CLASSIFIER_HYFI_ENABLE=n \
	ECM_CLASSIFIER_PCC_ENABLE=n \
	ECM_CLASSIFIER_MSCS_ENABLE=n \
	ECM_CLASSIFIER_EMESH_ENABLE=n \
	ECM_CLASSIFIER_OVS_ENABLE=n \
	ECM_CLASSIFIER_WIFI_ENABLE=n \
	ECM_BAND_STEERING_ENABLE=n \
	ECM_MHT_ENABLE=n \
	ECM_STATE_OUTPUT_ENABLE=y \
	ECM_DB_CONNECTION_CROSS_REFERENCING_ENABLE=y \
	ECM_TRACKER_DPI_SUPPORT_ENABLE=y \
	ECM_DB_CLASSIFIER_TYPE_ASSIGNMENTS_TRACK_ENABLE=y \
	SoC=generic

# 指定 SFE 头文件路径
EXTRA_CFLAGS+= -I$(STAGING_DIR)/usr/include/shortcut-fe

define Build/Compile
	$(KERNEL_MAKE) \
		M="$(PKG_BUILD_DIR)" \
		$(ECM_MAKE_OPTS) \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
		modules
endef

$(eval $(call KernelPackage,qca-nss-ecm))
