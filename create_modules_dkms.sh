#!/bin/bash
PKG_BUILD=1

PKG_VERSION=1.0.$PKG_BUILD

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

cd $WORK_DIR

TARGET_DIR=$WORK_DIR/pivccu-modules-dkms-$PKG_VERSION

mkdir -p $TARGET_DIR/usr/src/pivccu-$PKG_VERSION
cp $CURRENT_DIR/kernel/* $TARGET_DIR/usr/src/pivccu-$PKG_VERSION

cd $CURRENT_DIR/dts
dtc -@ -I dts -O dtb -o $TARGET_DIR/boot/overlays/pivccu-bcm2835.dtbo pivccu-bcm2835.dts

mkdir -p $TARGET_DIR/DEBIAN

cat <<EOT >> $TARGET_DIR/DEBIAN/control
Package: pivccu-modules-dkms
Version: $PKG_VERSION
Architecture: armhf
Maintainer: Alexander Reinert <alex@areinert.de>
Provides: pivccu-kernel-modules
Pre-Depends: dkms, build-essential
Section: kernel
Priority: extra
Homepage: https://github.com/alexreinert/piVCCU
Description: DKMS package for kernel modules needed for Homematic
  This package contains the a DKMS package for kernel needed for Homematic.
EOT

for file in preinst postinst prerm postrm; do
  echo "#!/bin/sh" > $TARGET_DIR/DEBIAN/$file
  chmod 755 $TARGET_DIR/DEBIAN/$file
done

for file in postinst postrm; do
  cat <<EOF >> $TARGET_DIR/DEBIAN/$file
sed -i /boot/config.txt -e '/dtoverlay=pivccu-bcm2835/d'
EOF

done

cat <<EOF >> $TARGET_DIR/DEBIAN/postinst
echo "dtoverlay=pivccu-bcm2835" >> /boot/config.txt
EOF

cd $WORK_DIR

dpkg-deb --build pivccu-modules-dkms-$PKG_VERSION

cp pivccu-modules-dkms-*.deb $CURRENT_DIR

