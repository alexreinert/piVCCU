#!/bin/bash
KERNEL_TAG=1.20171029-1

PKG_BUILD=5

PKG_VERSION=$KERNEL_TAG-$PKG_BUILD

KERNEL_SRC_URL=https://github.com/raspberrypi/linux/archive/raspberrypi-kernel_$KERNEL_TAG.tar.gz

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

KERNEL=kernel7

cd $WORK_DIR

# download kernel sources
wget -O linux.tar.gz $KERNEL_SRC_URL
tar xf linux.tar.gz
mv linux-raspberrypi-kernel_$KERNEL_TAG linux

SRC_DIR=$WORK_DIR/linux

# create kernel config
cd $SRC_DIR
make -C $SRC_DIR ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" bcm2709_defconfig

# build kernel
make -C $SRC_DIR -j$(grep -c processor /proc/cpuinfo) ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" LOCALVERSION="+" modules_prepare

MOD_DIR=$WORK_DIR/modules
mkdir -p $MOD_DIR

wget -q -O $MOD_DIR/eq3_char_loop.c https://raw.githubusercontent.com/eq-3/occu/e60183fc5b8375d9eea185c716f716c07657fa00/KernelDrivers/eq3_char_loop.c
cp -p $CURRENT_DIR/kernel/*.c $MOD_DIR
cp -p $CURRENT_DIR/kernel/*.h $MOD_DIR
cp -p $CURRENT_DIR/kernel/Makefile $MOD_DIR

cd $MOD_DIR
make -C $SRC_DIR -j$(grep -c processor /proc/cpuinfo) ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" KERNEL_DIR="$SRC_DIR" M="$MOD_DIR" LOCALVERSION="+" modules

cd $SRC_DIR
KERNEL_RELEASE=`cat "$SRC_DIR/include/config/kernel.release"`

TARGET_DIR=$WORK_DIR/pivccu-modules-raspberrypi-$PKG_VERSION

mkdir -p $TARGET_DIR/lib/modules/$KERNEL_RELEASE/kernel/drivers/pivccu
cp $MOD_DIR/*.ko $TARGET_DIR/lib/modules/$KERNEL_RELEASE/kernel/drivers/pivccu

mkdir -p $TARGET_DIR/boot/overlays

cd $CURRENT_DIR/dts
dtc -@ -I dts -O dtb -o $TARGET_DIR/boot/overlays/pivccu-bcm2835.dtbo pivccu-bcm2835.dts

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu-modules-raspberrypi/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{KERNEL_TAG}/$KERNEL_TAG/g" $file
  sed -i "s/{KERNEL_RELEASE}/$KERNEL_RELEASE/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu-modules-raspberrypi-$PKG_VERSION

cp pivccu-modules-raspberrypi-*.deb $CURRENT_DIR

