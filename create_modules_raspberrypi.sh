#!/bin/bash
PKG_BUILD=19

PKG_VERSION=2.0-$PKG_BUILD

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

cd $WORK_DIR

TARGET_DIR=$WORK_DIR/pivccu-modules-raspberrypi-$PKG_VERSION

mkdir -p $TARGET_DIR/lib/systemd/system/
cp -p $CURRENT_DIR/pivccu/rpi-modules/pivccu-rpi-modules.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/var/lib/piVCCU/rpi-modules
cp -p $CURRENT_DIR/pivccu/rpi-modules/*.sh $TARGET_DIR/var/lib/piVCCU/rpi-modules

mkdir -p $TARGET_DIR/var/lib/piVCCU/dtb/overlays

cd $CURRENT_DIR/dts
dtc -@ -I dts -O dtb -W no-unit_address_vs_reg -o $TARGET_DIR/var/lib/piVCCU/dtb/overlays/pivccu-raspberrypi.dtbo pivccu-raspberrypi.dts

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu-modules-raspberrypi/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build -Zxz pivccu-modules-raspberrypi-$PKG_VERSION

cp pivccu-modules-raspberrypi-*.deb $CURRENT_DIR

echo "Please clean-up the work dir temp folder $WORK_DIR, e.g. by doing rm -R $WORK_DIR"

