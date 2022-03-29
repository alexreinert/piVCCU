#!/bin/bash

CCU_VERSION=3.63.8
CCU_DOWNLOAD_SPLASH_URL="https://www.eq-3.de/service/downloads.html"
CCU_DOWNLOAD_URL="https://www.eq-3.de/downloads/software/firmware/ccu3-firmware/ccu3-$CCU_VERSION.tgz"
CCU_DOWNLOAD_URL="https://homematic-ip.com/sites/default/files/downloads/ccu3-$CCU_VERSION.tgz"

PKG_BUILD=70

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
mount -t ext4 -o loop,ro rootfs.ext4 $WORK_DIR/image

mkdir -p $CNT_ROOTFS

cp -p -P -R $WORK_DIR/image/* $CNT_ROOTFS

umount $WORK_DIR/image

cd $CNT_ROOTFS

patch -E -l -p1 < $CURRENT_DIR/pivccu/firmware3.patch
sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/config/cp_maintenance.cgi
sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/webui/webui.js

mkdir -p $CNT_ROOTFS/firmware/HM-MOD-UART
wget -q -O $CNT_ROOTFS/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3 https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3
wget -q -O $CNT_ROOTFS/firmware/HM-MOD-UART/fwmap https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/fwmap

mkdir -p $CNT_ROOTFS/firmware/HmIP-RFUSB
wget -q -P $CNT_ROOTFS/firmware/HmIP-RFUSB https://raw.githubusercontent.com/eq-3/occu/a2641df55cdde81239bbdbe7cf4dbcd4e3e62c5b/firmware/HmIP-RFUSB-Beta/dualcopro_update_blhmip-4.4.16.eq3

mkdir -p $CNT_ROOTFS/etc/piVCCU3
cp -p $CURRENT_DIR/pivccu/container3/* $CNT_ROOTFS/etc/piVCCU3

mkdir -p $CNT_ROOT/userfs

mkdir -p $TARGET_DIR/etc/piVCCU3
cp -p $CURRENT_DIR/pivccu/host3/lxc.config $TARGET_DIR/etc/piVCCU3

mkdir -p $TARGET_DIR/etc/default
cp -p $CURRENT_DIR/pivccu/host3/default.config $TARGET_DIR/etc/default/pivccu3

mkdir -p $TARGET_DIR/etc/udev/rules.d
cp -p $CURRENT_DIR/pivccu/host3/*.rules $TARGET_DIR/etc/udev/rules.d

cp -p $CURRENT_DIR/pivccu/host3/*.sh $CNT_ROOT
cp -p $CURRENT_DIR/pivccu/host3/*.inc $CNT_ROOT

mkdir -p $TARGET_DIR/lib/systemd/system/
cp -p $CURRENT_DIR/pivccu/host3/*.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu3/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
  sed -i "s/{PKG_ARCH}/armhf/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu3-$PKG_VERSION

cp pivccu3-$PKG_VERSION.deb $CURRENT_DIR/pivccu3-$PKG_VERSION-armhf.deb

wget -O openjdk-8-jre.deb http://security.debian.org/debian-security/pool/updates/main/o/openjdk-8/openjdk-8-jre_8u322-b06-1~deb9u1_armhf.deb
wget -O openjdk-8-jre-headless.deb http://security.debian.org/debian-security/pool/updates/main/o/openjdk-8/openjdk-8-jre-headless_8u322-b06-1~deb9u1_armhf.deb

dpkg-deb -x openjdk-8-jre.deb .
dpkg-deb -x openjdk-8-jre-headless.deb .

mv usr/lib/jvm/java-8-openjdk-armhf/jre $CNT_ROOTFS/opt/openjdk
rm -f $CNT_ROOTFS/opt/java
ln -s /opt/openjdk $CNT_ROOTFS/opt/java

rm -rf $TARGET_DIR/DEBIAN/*
cp -p $CURRENT_DIR/package/pivccu3/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
  sed -i "s/{PKG_ARCH}/arm64/g" $file
done

dpkg-deb --build pivccu3-$PKG_VERSION

cp pivccu3-$PKG_VERSION.deb $CURRENT_DIR/pivccu3-$PKG_VERSION-arm64.deb

echo "Please clean-up the work dir temp folder $WORK_DIR, e.g. by doing rm -R $WORK_DIR"
