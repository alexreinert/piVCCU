#!/bin/bash
PKG_BUILD=6

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

PKG_VERSION=1.0.$PKG_BUILD

TARGET_DIR=$WORK_DIR/pivccu-devicetree-armbian-$PKG_VERSION

cd $WORK_DIR

mkdir -p $TARGET_DIR/var/lib/piVCCU/dts

cp $CURRENT_DIR/dts/armbian/* $TARGET_DIR/var/lib/piVCCU/dts

mkdir -p $TARGET_DIR/boot/overlay-user

cd $CURRENT_DIR/dts
for dts in $(find *.dts -type f); do
  dtc -@ -I dts -O dtb -o $TARGET_DIR/boot/overlay-user/${dts%.dts}.dtbo $dts
done

mkdir -p $TARGET_DIR/etc/apt/apt.conf.d

cat <<EOF >> $TARGET_DIR/etc/apt/apt.conf.d/99pivccu_patch_dts
DPkg::Post-Invoke {"if [ -e /var/lib/piVCCU/dts/patch_dts.sh ]; then /var/lib/piVCCU/dts/patch_dts.sh; fi";};
EOF

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu-devicetree-armbian/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build pivccu-devicetree-armbian-$PKG_VERSION

cp pivccu-devicetree-armbian*.deb $CURRENT_DIR

