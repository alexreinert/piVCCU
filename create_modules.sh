#!/bin/bash
KERNEL_TAG=1.20170703-1

PKG_BUILD=1

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

cp -p $CURRENT_DIR/kernel/*.c $MOD_DIR
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
for dts in $(find *.dts -type f); do
  dtc -@ -I dts -O dtb -o $TARGET_DIR/boot/overlays/${dts%.dts}.dtbo $dts
done

mkdir -p $TARGET_DIR/DEBIAN

cat <<EOT >> $TARGET_DIR/DEBIAN/control
Package: pivccu-modules-raspberrypi
Version: $PKG_VERSION
Architecture: armhf
Maintainer: Alexander Reinert <alex@areinert.de>
Provides: pivccu-kernel-modules
Depends: raspberrypi-kernel (= $KERNEL_TAG)
Section: kernel
Priority: extra
Homepage: https://github.com/alexreinert/piVCCU
Description: Raspberry Pi kernel modules needed for Homematic
  This package contains the Raspberry Pi kernel needed for Homematic.
EOT

for file in preinst postinst prerm postrm; do
  echo "#!/bin/sh" > $TARGET_DIR/DEBIAN/$file
  chmod 755 $TARGET_DIR/DEBIAN/$file
done

for file in postinst postrm; do
  echo "depmod -a $KERNEL_RELEASE" >> $TARGET_DIR/DEBIAN/$file

  cat <<EOF >> $TARGET_DIR/DEBIAN/$file
sed -i /boot/config.txt -e '/dtoverlay=bcm2835-raw-uart/d'
EOF

done

cat <<EOF >> $TARGET_DIR/DEBIAN/postinst
echo "dtoverlay=bcm2835-raw-uart" >> /boot/config.txt
EOF

cd $TARGET_DIR/boot

for file in $(find * -type f)
do
  cat <<EOF >> $TARGET_DIR/DEBIAN/preinst
dpkg-divert --package rpikernelhack --divert /usr/share/rpikernelhack/$file /boot/$file
EOF

  cat <<EOF >> $TARGET_DIR/DEBIAN/postinst
if [ -f /usr/share/rpikernelhack/$file ]; then
  rm -f /boot/$file
  dpkg-divert --package rpikernelhack --remove --rename /boot/$file
  sync
fi
EOF
done

cd $WORK_DIR

dpkg-deb --build pivccu-modules-raspberrypi-$PKG_VERSION

cp pivccu-modules-raspberrypi-*.deb $CURRENT_DIR

