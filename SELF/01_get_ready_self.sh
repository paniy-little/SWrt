#!/bin/bash

# 这个脚本的作用是从不同的仓库中克隆openwrt相关的代码，并进行一些处理

# 构建模式：nightly（默认，跟踪最新分支/tag）或 release（强制使用 sources.lock 的具体 tag/commit）
SWRT_BUILD_MODE="${SWRT_BUILD_MODE:-nightly}"

# 读取 sources.lock，使其真正参与构建（release 模式必须使用其中的 *_REF）
LOCK_FILE="$(cd "$(dirname "$0")" && pwd)/sources.lock"
if [ -f "$LOCK_FILE" ]; then
    set -a
    # shellcheck disable=SC1090
    . "$LOCK_FILE"
    set +a
    echo "[get_ready] Loaded sources.lock (mode=$SWRT_BUILD_MODE)"
else
    echo "[get_ready] WARNING: sources.lock not found; using default branches" >&2
fi

# 克隆辅助：ref 可为 branch / tag / 40 位 commit SHA
clone_repo_ref() {
  repo_url=$1
  ref=$2
  target_dir=$3

  if printf '%s' "$ref" | grep -qE '^[0-9a-f]{40}$'; then
    # 具体 commit SHA：克隆后 detach 到该 commit
    git clone --filter=blob:none --no-checkout "$repo_url" "$target_dir"
    git -C "$target_dir" fetch --depth 1 origin "$ref"
    git -C "$target_dir" checkout --detach FETCH_HEAD
  else
    # 分支或 tag
    git clone -b "$ref" --depth 1 "$repo_url" "$target_dir"
  fi
}

# release 模式：拒绝“分支式伪 lock”，要求明确 tag/commit
validate_release_ref() {
  ref=$1
  name=$2
  if [ -z "$ref" ]; then
    echo "ERROR: [$name] release mode requires a concrete tag/commit in sources.lock" >&2
    exit 1
  fi
  case "$ref" in
    main|master|openwrt-25.12|openwrt-24.10|openwrt-23.05)
      echo "ERROR: [$name] release mode forbids branch ref '$ref' (use tag/commit)" >&2
      exit 1
      ;;
  esac
}

# 按模式选择 ref：$1=nightly 默认, $2=release lock ref
pick_ref() {
  if [ "$SWRT_BUILD_MODE" = "release" ]; then
    echo "$2"
  else
    echo "$1"
  fi
}

# 定义一些变量，存储仓库地址和分支名
latest_release="$(curl -s https://github.com/openwrt/openwrt/tags | grep -Eo "v[0-9\.]+\-*r*c*[0-9]*.tar.gz" | sed -n '/[2-9][5-9]/p' | sed -n 1p | sed 's/.tar.gz//g')"
immortalwrt_repo="https://github.com/immortalwrt/immortalwrt.git"
immortalwrt_pkg_repo="https://github.com/immortalwrt/packages.git"
immortalwrt_luci_repo="https://github.com/immortalwrt/luci.git"
lede_repo="https://github.com/coolsnowwolf/lede.git"
lede_luci_repo="https://github.com/coolsnowwolf/luci.git"
lede_pkg_repo="https://github.com/coolsnowwolf/packages.git"
openwrt_repo="https://github.com/openwrt/openwrt.git"
openwrt_pkg_repo="https://github.com/openwrt/packages.git"
openwrt_luci_repo="https://github.com/openwrt/luci.git"
lienol_repo="https://github.com/Lienol/openwrt.git"
lienol_pkg_repo="https://github.com/Lienol/openwrt-package"
openwrt_add_repo="https://github.com/QiuSimons/OpenWrt-Add.git"
openwrt_node_repo="https://github.com/nxhack/openwrt-node-packages.git"
passwall_pkg_repo="https://github.com/xiaorouji/openwrt-passwall-packages"
passwall_luci_repo="https://github.com/xiaorouji/openwrt-passwall"
openwrt_third_repo="https://github.com/jjm2473/openwrt-third"
dockerman_repo="https://github.com/lisaac/luci-app-dockerman"
diskman_repo="https://github.com/lisaac/luci-app-diskman"
docker_lib_repo="https://github.com/lisaac/luci-lib-docker"
mosdns_repo="https://github.com/QiuSimons/openwrt-mos"
ssrp_repo="https://github.com/fw876/helloworld"
zxlhhyccc_repo="https://github.com/zxlhhyccc/bf-package-master"
linkease_repo="https://github.com/linkease/openwrt-app-actions"
linkease_pkg_repo="https://github.com/jjm2473/packages"
linkease_luci_repo="https://github.com/jjm2473/luci"
sirpdboy_repo="https://github.com/sirpdboy/sirpdboy-package"
sbwdaednext_repo="https://github.com/sbwml/luci-app-daed-next"
lucidaednext_repo="https://github.com/QiuSimons/luci-app-daed-next"
sbwfw876_repo="https://github.com/sbwml/openwrt_helloworld"
sbw_pkg_repo="https://github.com/sbwml/openwrt_pkgs"
natmap_repo="https://github.com/blueberry-pie-11/luci-app-natmap"
upnp_nat_relay_repo="https://github.com/hello-yunshu/luci-app-upnp-nat-relay.git"
xwrt_repo="https://github.com/QiuSimons/openwrt-natflow"

# 关键源按模式选择 ref（nightly 用分支/最新 tag，release 用 sources.lock）
openwrt_ref="$(pick_ref "$latest_release" "${OPENWRT_REF:-}")"
openwrt_snap_ref="$(pick_ref "openwrt-25.12" "${OPENWRT_25_REF:-}")"
openwrt_ma_ref="$(pick_ref "main" "${OPENWRT_MAIN_REF:-}")"
openwrt_pkg_ma_ref="$(pick_ref "master" "${PACKAGES_REF:-}")"
lede_ref="$(pick_ref "master" "${LEDE_REF:-}")"
openwrt_add_ref="$(pick_ref "master" "${OPENWRT_ADD_REF:-}")"

# release 模式校验：关键源必须是具体 tag/commit，不允许分支伪 lock
if [ "$SWRT_BUILD_MODE" = "release" ]; then
    validate_release_ref "$OPENWRT_REF" "OPENWRT_REF"
    validate_release_ref "$OPENWRT_25_REF" "OPENWRT_25_REF"
    validate_release_ref "$OPENWRT_MAIN_REF" "OPENWRT_MAIN_REF"
    validate_release_ref "$PACKAGES_REF" "PACKAGES_REF"
    validate_release_ref "$LEDE_REF" "LEDE_REF"
    validate_release_ref "$OPENWRT_ADD_REF" "OPENWRT_ADD_REF"
fi

# 开始克隆仓库，并行执行
clone_repo_ref $openwrt_repo "$openwrt_ref" openwrt &
#clone_repo_ref $openwrt_repo openwrt-25.12 openwrt &
clone_repo_ref $openwrt_repo "$openwrt_snap_ref" openwrt_snap &
clone_repo_ref $immortalwrt_repo openwrt-24.10 immortalwrt_24 &
clone_repo_ref $immortalwrt_repo openwrt-23.05 immortalwrt_23 &

clone_repo_ref $lede_repo "$lede_ref" lede &
clone_repo_ref $lede_pkg_repo master lede_pkg_ma &
clone_repo_ref $openwrt_repo "$openwrt_ma_ref" openwrt_ma &
clone_repo_ref $openwrt_pkg_repo "$openwrt_pkg_ma_ref" openwrt_pkg_ma &
clone_repo_ref $openwrt_add_repo "$openwrt_add_ref" OpenWrt-Add &
clone_repo_ref $dockerman_repo master dockerman &
clone_repo_ref $docker_lib_repo master docker_lib &
clone_repo_ref $diskman_repo master diskman &
clone_repo_ref $upnp_nat_relay_repo main luci-app-upnp-nat-relay &
# 等待所有后台任务完成
wait

# 进行一些处理
cp -rf openwrt_snap/include/package-pack.mk /tmp/package-pack.mk.bak
cp -rf openwrt_snap/include/package.mk /tmp/package.mk.bak
cp -rf openwrt_snap/include/kernel.mk /tmp/kernel.mk.bak
cp -rf openwrt_snap/scripts/metadata.pm /tmp/metadata.pm.bak
cp -rf openwrt/package/libs/toolchain/Makefile /tmp/Makefile.bak
cp -rf openwrt/package/system/procd /tmp/procd.bak
cp -rf openwrt/package/libs/libubox /tmp/libubox.bak
find openwrt/package/* -maxdepth 0 ! -name 'firmware' ! -name 'kernel' ! -name 'base-files' ! -name 'Makefile' -exec rm -rf {} +
rm -rf ./openwrt/package/base-files/files/lib
cp -rf ./openwrt_snap/package/base-files/files/lib ./openwrt/package/base-files/files/
rm -rf ./openwrt_snap/package/firmware ./openwrt_snap/package/kernel ./openwrt_snap/package/base-files ./openwrt_snap/package/Makefile
cp -rf ./openwrt_snap/package/* ./openwrt/package/
cp -rf /tmp/package-pack.mk.bak ./openwrt/include/package-pack.mk
cp -rf /tmp/package.mk.bak ./openwrt/include/package.mk
cp -rf /tmp/kernel.mk.bak ./openwrt/include/kernel.mk
cp -rf /tmp/metadata.pm.bak ./openwrt/scripts/metadata.pm
cp -rf /tmp/Makefile.bak ./openwrt/package/libs/toolchain/Makefile
cp -rf ./openwrt_snap/feeds.conf.default ./openwrt/feeds.conf.default
rm -rf openwrt/package/system/procd
cp -rf /tmp/procd.bak ./openwrt/package/system/procd
rm -rf openwrt/package/libs/libubox
cp -rf /tmp/libubox.bak ./openwrt/package/libs/libubox

# 退出脚本
exit 0
