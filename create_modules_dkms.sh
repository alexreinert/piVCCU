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

cat <<EOT >> $TARGET_DIR/DEBIAN/control
Package: pivccu-modules-dkms
Version: $PKG_VERSION
Architecture: armhf
Maintainer: Alexander Reinert <alex@areinert.de>
Provides: pivccu-kernel-modules
Pre-Depends: dkms, build-essential
Recommends: pivccu-devicetree
Section: kernel
Priority: extra
Homepage: https://github.com/alexreinert/piVCCU
Description: DKMS package for kernel modules needed for Homematic
  This package contains the a DKMS package for kernel needed for Homematic.
EOT

echo /lib/systemd/system/pivccu-dkms.service >> $TARGET_DIR/DEBIAN/conffiles

for file in preinst postinst prerm postrm; do
  echo "#!/bin/sh" > $TARGET_DIR/DEBIAN/$file
  chmod 755 $TARGET_DIR/DEBIAN/$file
done

cat <<EOF >> $TARGET_DIR/DEBIAN/postinst
set -e

systemctl enable pivccu-dkms.service

DKMS_NAME=pivccu
DKMS_PACKAGE_NAME=\$DKMS_NAME-dkms
DKMS_VERSION=$PKG_VERSION

postinst_found=0

case "\$1" in
        configure)
                for DKMS_POSTINST in /usr/lib/dkms/common.postinst /usr/share/\$DKMS_PACKAGE_NAME/postinst; do
                        if [ -f \$DKMS_POSTINST ]; then
                                \$DKMS_POSTINST \$DKMS_NAME \$DKMS_VERSION /usr/share/\$DKMS_PACKAGE_NAME "" \$2
                                postinst_found=1
                                break
                        fi
                done
                if [ "\$postinst_found" -eq 0 ]; then
                        echo "ERROR: DKMS version is too old and \$DKMS_PACKAGE_NAME was not"
                        echo "built with legacy DKMS support."
                        echo "You must either rebuild \$DKMS_PACKAGE_NAME with legacy postinst"
                        echo "support or upgrade DKMS to a more current version."
                        exit 1
                fi
        ;;
esac
EOF

cat <<EOF >> $TARGET_DIR/DEBIAN/prerm
set -e

systemctl disable pivccu-dkms.service

DKMS_NAME=pivccu
DKMS_VERSION=$PKG_VERSION

case "\$1" in
    remove|upgrade|deconfigure)
      if [  "\$(dkms status -m \$DKMS_NAME -v \$DKMS_VERSION)" ]; then
         dkms remove -m \$DKMS_NAME -v \$DKMS_VERSION --all
      fi
    ;;
esac
EOF

cd $WORK_DIR

dpkg-deb --build pivccu-modules-dkms-$PKG_VERSION

cp pivccu-modules-dkms-*.deb $CURRENT_DIR

