#!/bin/bash

CCU_VERSION=2.35.16
CCU_DOWNLOAD_SPLASH_URL="http://www.eq-3.de/service/downloads.html?id=285"
CCU_DOWNLOAD_URL="https://www.eq-3.de/Downloads/Software/HM-CCU2-Firmware_Updates/HM-CCU-$CCU_VERSION/HM-CCU-$CCU_VERSION.tgz"

PKG_BUILD=37

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

PKG_VERSION=$CCU_VERSION-$PKG_BUILD

TARGET_DIR=$WORK_DIR/pivccu-$PKG_VERSION
CNT_ROOT=$TARGET_DIR/var/lib/piVCCU
CNT_ROOTFS=$CNT_ROOT/rootfs

cd $WORK_DIR

# download firmware image
wget -O /dev/null --save-cookies=cookies.txt --keep-session-cookies $CCU_DOWNLOAD_SPLASH_URL
wget -O ccu.tar.gz --load-cookies=cookies.txt --referer=$CCU_DOWNLOAD_SPLASH_URL $CCU_DOWNLOAD_URL

tar xf ccu.tar.gz

mkdir -p $CNT_ROOT

ubireader_extract_files -k -o rootfs rootfs.ubi
mv rootfs/*/root $CNT_ROOTFS

cd $CNT_ROOTFS

patch -l -p1 < $CURRENT_DIR/pivccu/firmware.patch
sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/config/cp_maintenance.cgi
wget -q -O $CNT_ROOTFS/firmware/dualcopro_si1002_update_blhm.eq3 https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3

wget -q -O $CNT_ROOTFS/opt/HmIP/hmip-copro-update.jar https://raw.githubusercontent.com/eq-3/occu/04877bbc3b36a716e50d774554cf88959c51d54e/HMserver/opt/HmIP/hmip-copro-update.jar

mkdir -p $CNT_ROOTFS/firmware/HMIP-RFUSB
wget -q -O $CNT_ROOTFS/firmware/HMIP-RFUSB/hmip_coprocessor_update-2.8.4.eq3 https://raw.githubusercontent.com/eq-3/occu/04877bbc3b36a716e50d774554cf88959c51d54e/firmware/HmIP-RFUSB/hmip_coprocessor_update-2.8.4.eq3

mkdir -p $CNT_ROOTFS/firmware/RPI-RF-MOD
wget -q -O $CNT_ROOTFS/firmware/RPI-RF-MOD/dualcopro_update_blhmip-3.4.2.eq3 https://raw.githubusercontent.com/eq-3/occu/1120262d0efd6f1ae1129646dbeea61eb12ae111/firmware/RPI-RF-MOD/dualcopro_update_blhmip-3.4.2.eq3

rm -rf $CNT_ROOTFS/dev/*

mkdir $CNT_ROOTFS/dev/pts
mkdir $CNT_ROOTFS/dev/shm
mkdir $CNT_ROOTFS/dev/net

mknod -m 666 $CNT_ROOTFS/dev/console c 136 1
mknod -m 666 $CNT_ROOTFS/dev/null c 1 3
mknod -m 666 $CNT_ROOTFS/dev/random c 1 8
mknod -m 666 $CNT_ROOTFS/dev/tty c 5 0
mknod -m 666 $CNT_ROOTFS/dev/urandom c 1 9
mknod -m 666 $CNT_ROOTFS/dev/zero c 1 5
mknod -m 666 $CNT_ROOTFS/dev/net/tun c 10 200

mkdir -p $CNT_ROOTFS/media/sd-mmcblk0

mkdir -p $CNT_ROOTFS/etc/piVCCU
cp -p $CURRENT_DIR/pivccu/container/* $CNT_ROOTFS/etc/piVCCU
rm -f $CNT_ROOTFS/etc/piVCCU/S59FirmwareUpdate

mkdir -p $CNT_ROOTFS/etc/init.d
cp -p $CURRENT_DIR/pivccu/container/S59FirmwareUpdate $CNT_ROOTFS/etc/init.d

mkdir -p $CNT_ROOT/userfs
mkdir -p $CNT_ROOT/sdcardfs
touch $CNT_ROOT/sdcardfs/.initialised

mkdir -p $TARGET_DIR/etc/piVCCU
cp -p $CURRENT_DIR/pivccu/host/lxc.config $TARGET_DIR/etc/piVCCU

mkdir -p $TARGET_DIR/etc/default
cp -p $CURRENT_DIR/pivccu/host/default.config $TARGET_DIR/etc/default/pivccu

cp -p $CURRENT_DIR/pivccu/host/*.sh $CNT_ROOT
cp -p $CURRENT_DIR/pivccu/host/*.inc $CNT_ROOT

mkdir -p $TARGET_DIR/lib/systemd/system/
cp -p $CURRENT_DIR/pivccu/host/pivccu.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu-$PKG_VERSION

cp pivccu-*.deb $CURRENT_DIR

