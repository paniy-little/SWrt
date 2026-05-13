#!/bin/bash

#sed -i 's/O2/O2 -march=x86-64-v2/g' include/target.mk

# libsodium
sed -i 's,no-mips16 no-lto,no-mips16,g' feeds/packages/libs/libsodium/Makefile

cat > ./package/base-files/files/etc/rc.local <<'EOF'
#!/bin/sh
# Put your custom commands here that should be executed once
# the system init finished. By default this file does nothing.

echo "Hyper-V Virtual Machine" > /tmp/sysinfo/model

if [ -r /sys/devices/system/cpu/intel_pstate/status ]; then
    status=$(cat /sys/devices/system/cpu/intel_pstate/status)

    if [ "$status" = "passive" ]; then
        echo "active" > /sys/devices/system/cpu/intel_pstate/status
    fi
fi

exit 0
EOF

#Vermagic
latest_version="$(curl -s https://github.com/openwrt/openwrt/tags | grep -Eo "v[0-9\.]+\-*r*c*[0-9]*.tar.gz" | sed -n '/[2-9][5-9]/p' | sed -n 1p | sed 's/v//g' | sed 's/.tar.gz//g')"
wget https://downloads.openwrt.org/releases/${latest_version}/targets/x86/64/profiles.json
jq -r '.linux_kernel.vermagic' profiles.json >.vermagic
sed -i -e 's/^\(.\).*vermagic$/\1cp $(TOPDIR)\/.vermagic $(LINUX_DIR)\/.vermagic/' include/kernel-defaults.mk

# Hyper-V 专用：默认第一个网卡为 WAN，第二个网卡为 LAN
sed -i '/^esac$/i\
*)\
\tucidef_set_interfaces_lan_wan "eth1" "eth0"\
\t;;' target/linux/x86/base-files/etc/board.d/02_network

# 预配置一些插件
cp -rf ../PATCH/files ./files

find ./ -name *.orig | xargs rm -f
find ./ -name *.rej | xargs rm -f

exit 0
