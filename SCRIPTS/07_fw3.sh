#!/bin/bash
# 滚回fw3
sed -i 's,iptables-nft,iptables-legacy,g' ./package/new/luci-app-passwall2/Makefile
sed -i 's,iptables-nft,iptables-legacy,g' ./package/new/luci-app-passwall2/Makefile
sed -i 's,iptables-nft +kmod-nft-fullcone,iptables-mod-fullconenat,g' ./package/lean/lean-translate/Makefile
rm -rf ./feeds/packages/net/miniupnpd
svn export https://github.com/coolsnowwolf/packages/trunk/net/miniupnpd feeds/packages/net/miniupnpd
rm -rf ./feeds/luci/applications/luci-app-upnp
svn export https://github.com/coolsnowwolf/luci/trunk/applications/luci-app-upnp feeds/luci/applications/luci-app-upnp
sed -i '/firewall/d' ./.config
sed -i '/offload/d' ./.config
sed -i '/tables/d' ./.config
sed -i '/nft/d' ./.config
echo '
CONFIG_PACKAGE_firewall=y
# CONFIG_PACKAGE_firewall4 is not set
' >>./.config
rm -rf ./feeds/luci/applications/luci-app-zerotier
svn export https://github.com/coolsnowwolf/luci/trunk/applications/luci-app-zerotier feeds/luci/applications/luci-app-zerotier
wget -P feeds/luci/applications/luci-app-zerotier/ https://github.com/QiuSimons/OpenWrt-Add/raw/master/move_2_services.sh
chmod -R 755 ./feeds/luci/applications/luci-app-zerotier/move_2_services.sh
pushd feeds/luci/applications/luci-app-zerotier
bash move_2_services.sh
popd
ln -sf ../../../feeds/luci/applications/luci-app-zerotier ./package/feeds/luci/luci-app-zerotier

po_file="$({ find | grep -E "[a-z0-9]+\.zh\-cn.+po"; } 2>"/dev/null")"
for a in ${po_file}; do
  [ -n "$(grep "Language: zh_CN" "$a")" ] && sed -i "s/Language: zh_CN/Language: zh_Hans/g" "$a"
  po_new_file="$(echo -e "$a" | sed "s/zh-cn/zh_Hans/g")"
  mv "$a" "${po_new_file}" 2>"/dev/null"
done

po_file2="$({ find | grep "/zh-cn/" | grep "\.po"; } 2>"/dev/null")"
for b in ${po_file2}; do
  [ -n "$(grep "Language: zh_CN" "$b")" ] && sed -i "s/Language: zh_CN/Language: zh_Hans/g" "$b"
  po_new_file2="$(echo -e "$b" | sed "s/zh-cn/zh_Hans/g")"
  mv "$b" "${po_new_file2}" 2>"/dev/null"
done

lmo_file="$({ find | grep -E "[a-z0-9]+\.zh_Hans.+lmo"; } 2>"/dev/null")"
for c in ${lmo_file}; do
  lmo_new_file="$(echo -e "$c" | sed "s/zh_Hans/zh-cn/g")"
  mv "$c" "${lmo_new_file}" 2>"/dev/null"
done

lmo_file2="$({ find | grep "/zh_Hans/" | grep "\.lmo"; } 2>"/dev/null")"
for d in ${lmo_file2}; do
  lmo_new_file2="$(echo -e "$d" | sed "s/zh_Hans/zh-cn/g")"
  mv "$d" "${lmo_new_file2}" 2>"/dev/null"
done

po_dir="$({ find | grep "/zh-cn" | sed "/\.po/d" | sed "/\.lmo/d"; } 2>"/dev/null")"
for e in ${po_dir}; do
  po_new_dir="$(echo -e "$e" | sed "s/zh-cn/zh_Hans/g")"
  mv "$e" "${po_new_dir}" 2>"/dev/null"
done

makefile_file="$({ find | grep Makefile | sed "/Makefile./d"; } 2>"/dev/null")"
for f in ${makefile_file}; do
  [ -n "$(grep "zh-cn" "$f")" ] && sed -i "s/zh-cn/zh_Hans/g" "$f"
  [ -n "$(grep "zh_Hans.lmo" "$f")" ] && sed -i "s/zh_Hans.lmo/zh-cn.lmo/g" "$f"
done

makefile_file="$({ find package | grep Makefile | sed "/Makefile./d"; } 2>"/dev/null")"
for g in ${makefile_file}; do
  [ -n "$(grep "golang-package.mk" "$g")" ] && sed -i "s,\../..,\$(TOPDIR)/feeds/packages,g" "$g"
  [ -n "$(grep "luci.mk" "$g")" ] && sed -i "s,\../..,\$(TOPDIR)/feeds/luci,g" "$g"
done

exit 0