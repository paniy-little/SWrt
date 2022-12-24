#!/bin/bash

sed -i 's/O2 -Wl,--gc-sections/O2 -Wl,--gc-sections -mtune=goldmont-plus/g' include/target.mk

rm -rf ./package/kernel/linux/modules/video.mk
rm -rf ./target/linux/x86/64/config-5.10
wget -P package/kernel/linux/modules/ https://github.com/coolsnowwolf/lede/raw/master/package/kernel/linux/modules/video.mk
sed -i 's,CONFIG_DRM_I915_CAPTURE_ERROR ,CONFIG_DRM_I915_CAPTURE_ERROR=n ,g' package/kernel/linux/modules/video.mk
wget -P target/linux/x86/64/ https://github.com/coolsnowwolf/lede/raw/master/target/linux/x86/64/config-5.10

echo '# Put your custom commands here that should be executed once
# the system init finished. By default this file does nothing.

grep "Default string" /tmp/sysinfo/model >> /dev/null
if [ $? -ne 0 ];then
    echo should be fine
else
    echo "Compatible PC" > /tmp/sysinfo/model
fi

board=$(cat /tmp/sysinfo/board_name | cut -d , -f2)
if [[ ! -f /etc/panyi_${board} ]]; then
    touch /etc/panyi_${board}
    bash /etc/panyi-begin.sh
fi

exit 0
'> ./package/base-files/files/etc/rc.local

# UKSM
#echo '
#CONFIG_KSM=y
#CONFIG_UKSM=y
#' >>./target/linux/x86/64/config-5.4

#Vermagic
latest_version="$(curl -s https://github.com/openwrt/openwrt/tags | grep -Eo "v[0-9\.]+\-*r*c*[0-9]*.tar.gz" | sed -n '/[2-9][0-9]/p' | sed -n 1p | sed 's/v//g' | sed 's/.tar.gz//g')"
wget https://downloads.openwrt.org/releases/${latest_version}/targets/x86/64/packages/Packages.gz
zgrep -m 1 "Depends: kernel (=.*)$" Packages.gz | sed -e 's/.*-\(.*\))/\1/' >.vermagic
sed -i -e 's/^\(.\).*vermagic$/\1cp $(TOPDIR)\/.vermagic $(LINUX_DIR)\/.vermagic/' include/kernel-defaults.mk

# 预配置一些插件
cp -rf ../PATCH/files ./files

chmod -R 755 ./
find ./ -name *.orig | xargs rm -f
find ./ -name *.rej | xargs rm -f

exit 0
