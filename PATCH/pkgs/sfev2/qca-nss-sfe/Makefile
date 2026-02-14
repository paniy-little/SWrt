include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=qca-nss-sfe
PKG_VERSION:=14.0.1.00011
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://git.codelinaro.org/clo/qsdk/oss/lklm/shortcut-fe.git
PKG_SOURCE_VERSION:=NHSS.QSDK.14.0.r1-00011-O
PKG_MIRROR_HASH:=skip

include $(INCLUDE_DIR)/package.mk

define KernelPackage/qca-nss-sfe
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Network Support
  TITLE:=Qualcomm Shortcut FE v2 (SFE) - Upstream
  DEPENDS:=+kmod-nf-conntrack
  FILES:=$(PKG_BUILD_DIR)/qca-nss-sfe.ko
  KCONFIG:= \
	   CONFIG_NF_CONNTRACK_CHAIN_EVENTS=y \
	   CONFIG_NF_CONNTRACK_EVENTS=y \
	   CONFIG_NF_CONNTRACK_TIMEOUT=y \
	   CONFIG_SHORTCUT_FE=y \
	   CONFIG_XFRM=y
endef

define KernelPackage/qca-nss-sfe/description
  Shortcut Forwarding Engine v2 - software packet acceleration.
  Built from upstream QSDK source with vanilla kernel compatibility patches.
endef

# 安装 ECM 需要的所有头文件
define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include/shortcut-fe
	$(CP) $(PKG_BUILD_DIR)/exports/sfe_api.h $(1)/usr/include/shortcut-fe/
	$(CP) $(PKG_BUILD_DIR)/sfe.h $(1)/usr/include/shortcut-fe/
endef

define Build/Compile
	$(KERNEL_MAKE) \
		M="$(PKG_BUILD_DIR)" \
		SFE_SUPPORT_IPV6=1 \
		modules
endef

$(eval $(call KernelPackage,qca-nss-sfe))
