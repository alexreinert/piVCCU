#!/bin/bash
PKG_BUILD=5

PKG_VERSION=1.0.$PKG_BUILD

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

cd $WORK_DIR

TARGET_DIR=$WORK_DIR/pivccu-modules-dkms-$PKG_VERSION

mkdir -p $TARGET_DIR/usr/src/pivccu-$PKG_VERSION
wget -q -O $TARGET_DIR/usr/src/pivccu-$PKG_VERSION/eq3_char_loop.c https://raw.githubusercontent.com/eq-3/occu/e60183fc5b8375d9eea185c716f716c07657fa00/KernelDrivers/eq3_char_loop.c
cp $CURRENT_DIR/kernel/* $TARGET_DIR/usr/src/pivccu-$PKG_VERSION

DKMS_CONF_FILE=$TARGET_DIR/usr/src/pivccu-$PKG_VERSION/dkms.conf

cat <<EOF >> $DKMS_CONF_FILE
PACKAGE_NAME="pivccu"
PACKAGE_VERSION="$PKG_VERSION"

MAKE="make all"
CLEAN="make clean"

AUTOINSTALL="yes"

EOF

index=0
for file in $TARGET_DIR/usr/src/pivccu-$PKG_VERSION/*.c; do
  modname=$(basename "$file" .c)
  echo "BUILT_MODULE_NAME[$index]=\"$modname\"" >> $DKMS_CONF_FILE
  echo "DEST_MODULE_LOCATION[$index]=\"/kernel/drivers/pivccu\"" >> $DKMS_CONF_FILE
  index=$(expr $index + 1)
done

mkdir -p $TARGET_DIR/lib/systemd/system/
cp -p $CURRENT_DIR/pivccu/dkms/pivccu-dkms.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/var/lib/piVCCU/dkms
cp -p $CURRENT_DIR/pivccu/dkms/*.sh $TARGET_DIR/var/lib/piVCCU/dkms

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu-modules-dkms/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu-modules-dkms-$PKG_VERSION

cp pivccu-modules-dkms-*.deb $CURRENT_DIR

