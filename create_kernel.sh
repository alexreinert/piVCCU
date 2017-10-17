#!/bin/bash
KERNEL_TAG=1.20170811-1

PKG_BUILD=1

PKG_VERSION=$KERNEL_TAG-$PKG_BUILD

KERNEL_SRC_URL=https://github.com/raspberrypi/linux/archive/raspberrypi-kernel_$KERNEL_TAG.tar.gz
KERNEL_MODULES_BASE_URL=https://raw.githubusercontent.com/jens-maus/RaspberryMatic/master/buildroot-external/package/occu/kernel-modules

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

KERNEL=kernel7

cd $WORK_DIR

# download kernel sources
wget -O linux.tar.gz $KERNEL_SRC_URL
tar xf linux.tar.gz
mv linux-raspberrypi-kernel_$KERNEL_TAG linux

SRC_DIR=$WORK_DIR/linux

# add homematic kernel module sources
cd $SRC_DIR
cd $SRC_DIR/drivers/char
wget $KERNEL_MODULES_BASE_URL/eq3_char_loop/eq3_char_loop.c
echo "obj-m += eq3_char_loop.o" >> Makefile
cd $SRC_DIR/drivers/char/broadcom
wget $KERNEL_MODULES_BASE_URL/bcm2835_raw_uart/bcm2835_raw_uart.c
echo "obj-m += bcm2835_raw_uart.o" >> Makefile
cd $SRC_DIR

# create kernel config
make -C $SRC_DIR ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" bcm2709_defconfig
sed -i .config -e "s/CONFIG_SERIAL_AMBA_PL011=y/CONFIG_SERIAL_AMBA_PL011=n/"
sed -i .config -e "s/CONFIG_SERIAL_AMBA_PL011_CONSOLE=y/CONFIG_SERIAL_AMBA_PL011_CONSOLE=n/"
sed -i .config -e "s/CONFIG_LOCALVERSION=.*/CONFIG_LOCALVERSION=\"-v7-homematic\"/"

# build kernel
make -C $SRC_DIR -j$(grep -c processor /proc/cpuinfo) ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" zImage modules dtbs

# Check if kernel compilation was successful
if [ ! -r "$SRC_DIR/arch/arm/boot/zImage" ] ; then
  echo "Kernel compilation was not successful."
  exit 1
fi

cd $SRC_DIR
KERNEL_VERSION=`make kernelversion`
KERNEL_RELEASE=`cat "$SRC_DIR/include/config/kernel.release"`

TARGET_DIR=$WORK_DIR/raspberrypi-kernel-homematic-$PKG_VERSION

# create package
mkdir -p $TARGET_DIR
make -C $SRC_DIR ARCH="arm" CROSS_COMPILE="arm-linux-gnueabihf-" INSTALL_MOD_PATH=$TARGET_DIR modules_install

mkdir -p $TARGET_DIR/boot/overlays
cp $SRC_DIR/.config $TARGET_DIR/boot/config-$KERNEL_RELEASE
cp $SRC_DIR/arch/arm/boot/dts/*.dtb $TARGET_DIR/boot
cp $SRC_DIR/arch/arm/boot/dts/overlays/*.dtb* $TARGET_DIR/boot/overlays
cp $SRC_DIR/arch/arm/boot/dts/overlays/README $TARGET_DIR/boot/overlays
cp $SRC_DIR/arch/arm/boot/zImage $TARGET_DIR/boot/kernel7.img

rm -f $TARGET_DIR/lib/modules/$KERNEL_RELEASE/{build,source}
rm -rf $TARGET_DIR/lib/firmware

mkdir -p $TARGET_DIR/etc/default
cat <<EOT >> $TARGET_DIR/etc/default/raspberrypi-kernel
# Defaults for raspberrypi-kernel

# Uncomment the following line to enable generation of
# /boot/initrd.img-KVER files (requires initramfs-tools)

#INITRD=Yes

# Uncomment the following line to enable generation of
# /boot/initrd(7).img files (requires rpi-initramfs-tools)

#RPI_INITRD=Yes
EOT

mkdir -p $TARGET_DIR/DEBIAN

cat <<EOT >> $TARGET_DIR/DEBIAN/control
Package: raspberrypi-kernel-homematic
Version: $PKG_VERSION
Architecture: armhf
Maintainer: Alexander Reinert <alex@areinert.de>
Replaces: raspberrypi-kernel
Breaks: raspberrypi-kernel
Provides: linux-image
Section: kernel
Priority: extra
Homepage: https://github.com/alexreinert/
Description: Raspberry Pi Kernel with modifications needed for Homematic
  This package contains the Raspberry Pi Linux kernel with modifications needed for Homematic.
EOT

echo "/etc/default/raspberrypi-kernel" > $TARGET_DIR/DEBIAN/conffiles

for file in preinst postinst prerm postrm; do
  echo "#!/bin/sh" > $TARGET_DIR/DEBIAN/$file
  chmod 755 $TARGET_DIR/DEBIAN/$file
done

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

for file in preinst postinst prerm postrm; do
  cat <<EOT >> $TARGET_DIR/DEBIAN/$file
if [ -f /etc/default/raspberrypi-kernel ]; then
  . /etc/default/raspberrypi-kernel
  INITRD=No
  export INITRD
  RPI_INITRD=No
  export RPI_INITRD
fi
if [ -d "/etc/kernel/$file.d" ]; then
  run-parts -v --report --exit-on-error --arg=$KERNEL_RELEASE --arg=/boot/kernel7.img /etc/kernel/$file.d
fi
if [ -d "/etc/kernel/$file.d/$KERNEL_RELEASE" ]; then
  run-parts -v --report --exit-on-error --arg=$KERNEL_RELEASE --arg=/boot/kernel7.img /etc/kernel/$file.d/$KERNEL_RELEASE
fi
EOT
done

cd $WORK_DIR

dpkg-deb --build raspberrypi-kernel-homematic-$PKG_VERSION

cp raspberrypi-kernel-homematic-*.deb $CURRENT_DIR

