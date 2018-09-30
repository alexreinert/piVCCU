#!/bin/bash

CCU_VERSION=3.37.8
CCU_DOWNLOAD_SPLASH_URL="http://www.eq-3.de/service/downloads.html?id=287"
CCU_DOWNLOAD_URL="https://www.eq-3.de/Downloads/Software/CCU3-Firmware/CCU3-$CCU_VERSION/ccu3-$CCU_VERSION.tgz"

PKG_BUILD=3

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

PKG_VERSION=$CCU_VERSION-$PKG_BUILD

TARGET_DIR=$WORK_DIR/pivccu3-$PKG_VERSION
CNT_ROOT=$TARGET_DIR/var/lib/piVCCU3
CNT_ROOTFS=$CNT_ROOT/rootfs

cd $WORK_DIR


# download firmware image
wget -O /dev/null --save-cookies=cookies.txt --keep-session-cookies $CCU_DOWNLOAD_SPLASH_URL
wget -O ccu3.tar.gz --load-cookies=cookies.txt --referer=$CCU_DOWNLOAD_SPLASH_URL $CCU_DOWNLOAD_URL

tar xzf ccu3.tar.gz

gunzip rootfs.ext4.gz

mkdir $WORK_DIR/image
mount -t ext4 -o loop,ro /root/ccu3/unpacked/rootfs.ext4 $WORK_DIR/image

mkdir -p $CNT_ROOTFS

cp -p -P -R $WORK_DIR/image/* $CNT_ROOTFS

umount $WORK_DIR/image

cd $CNT_ROOTFS

patch -l -p1 < $CURRENT_DIR/pivccu/firmware3.patch
sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/config/cp_maintenance.cgi

mkdir -p $CNT_ROOTFS/firmware
wget -q -O $CNT_ROOTFS/firmware/dualcopro_si1002_update_blhm.eq3 https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3

mkdir -p $CNT_ROOTFS/firmware/HMIP-RFUSB
wget -q -O $CNT_ROOTFS/firmware/HMIP-RFUSB/hmip_coprocessor_update-2.8.4.eq3 https://raw.githubusercontent.com/eq-3/occu/04877bbc3b36a716e50d774554cf88959c51d54e/firmware/HmIP-RFUSB/hmip_coprocessor_update-2.8.4.eq3

mkdir -p $CNT_ROOTFS/etc/piVCCU3
cp -p $CURRENT_DIR/pivccu/container3/* $CNT_ROOTFS/etc/piVCCU3

mkdir -p $CNT_ROOT/userfs

mkdir -p $TARGET_DIR/etc/piVCCU3
cp -p $CURRENT_DIR/pivccu/host3/lxc.config $TARGET_DIR/etc/piVCCU3

mkdir -p $TARGET_DIR/etc/default
cp -p $CURRENT_DIR/pivccu/host3/default.config $TARGET_DIR/etc/default/pivccu3

cp -p $CURRENT_DIR/pivccu/host3/*.sh $CNT_ROOT
cp -p $CURRENT_DIR/pivccu/host3/*.inc $CNT_ROOT

mkdir -p $TARGET_DIR/lib/systemd/system/
cp -p $CURRENT_DIR/pivccu/host3/pivccu.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu3/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu3-$PKG_VERSION

cp pivccu3-*.deb $CURRENT_DIR

