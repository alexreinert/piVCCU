#!/bin/bash
PKG_BUILD=1

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

PKG_VERSION=1.0.$PKG_BUILD

TARGET_DIR=$WORK_DIR/pivccu-devicetree-armbian-$PKG_VERSION

cd $WORK_DIR

mkdir -p $TARGET_DIR/var/lib/piVCCU/dts

cp $CURRENT_DIR/dts/armbian/* $TARGET_DIR/var/lib/piVCCU/dts

mkdir -p $TARGET_DIR/etc/apt/apt.conf.d

cat <<EOF >> $TARGET_DIR/etc/apt/apt.conf.d/99pivccu_patch_dts
DPkg::Post-Invoke {"if [ -e /var/lib/piVCCU/dts/patch_dts.sh ]; then /var/lib/piVCCU/dts/patch_dts.sh; fi";};
EOF

mkdir -p $TARGET_DIR/DEBIAN

cat <<EOT >> $TARGET_DIR/DEBIAN/control
Package: pivccu-devicetree-armbian
Version: $PKG_VERSION
Architecture: armhf
Maintainer: Alexander Reinert <alex@areinert.de>
Provides: pivccu-devicetree
Section: misc
Priority: extra
Homepage: https://github.com/alexreinert/piVCCU
Description: piVCCU DeviceTree files for armbian
  This package contains piVCCU DeviceTree files for armbian
EOT

cat <<EOT >> $TARGET_DIR/DEBIAN/postinst
#!/bin/sh
#/var/lib/piVCCU/dts/patch_dts.sh
EOT

chmod +x $TARGET_DIR/DEBIAN/postinst

cat <<EOT >> $TARGET_DIR/DEBIAN/prerm
#!/bin/sh
/var/lib/piVCCU/dts/revert_dts.sh
EOT

chmod +x $TARGET_DIR/DEBIAN/prerm

cd $WORK_DIR

dpkg-deb --build pivccu-devicetree-armbian-$PKG_VERSION

cp pivccu-devicetree-armbian*.deb $CURRENT_DIR

