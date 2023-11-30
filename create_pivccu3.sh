#!/bin/bash

CCU_VERSION=3.73.9
CCU_DOWNLOAD_SPLASH_URL="https://www.eq-3.de/service/downloads.html"
CCU_DOWNLOAD_URL="https://www.eq-3.de/downloads/software/firmware/ccu3-firmware/ccu3-$CCU_VERSION.tgz"
CCU_DOWNLOAD_URL="https://homematic-ip.com/sites/default/files/downloads/ccu3-$CCU_VERSION.tgz"

PKG_BUILD=87

function throw {
  echo $1
  exit 1
}

function run {
  echo -n "$1 ... "
  shift
  ERR=`$* 2>&1` && RC=$? || RC=$?
  if [ $RC -eq 0 ]; then
    echo -e "\033[0;32mDone\033[0;0m"
  else
    echo -e "\033[1;91mFAILED\033[0;0m"
    echo "$ERR"
    exit 1
  fi
}

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

PKG_VERSION=$CCU_VERSION-$PKG_BUILD

TARGET_DIR=$WORK_DIR/pivccu3-$PKG_VERSION
CNT_ROOT=$TARGET_DIR/var/lib/piVCCU3
CNT_ROOTFS=$CNT_ROOT/rootfs

cd $WORK_DIR

function download_ccu3_firmware {
  run "Get splash page" wget -O /dev/null --save-cookies=cookies.txt --keep-session-cookies $CCU_DOWNLOAD_SPLASH_URL
  run "Download package" wget -O ccu3.tar.gz --load-cookies=cookies.txt --referer=$CCU_DOWNLOAD_SPLASH_URL $CCU_DOWNLOAD_URL
}
run "Download CCU3 firmware" download_ccu3_firmware

function extract_ccu3_firmware {
  run "Extract CCU3 firmware" tar xzf ccu3.tar.gz
  run "Extract root fs" gunzip rootfs.ext4.gz

  mkdir $WORK_DIR/image
  run "Mount root fs" fuse2fs -o ro,fakeroot rootfs.ext4 $WORK_DIR/image

  mkdir -p $CNT_ROOTFS

  run "Copy root fs files" cp -p -P -R $WORK_DIR/image/* $CNT_ROOTFS

  run "Umount root fs" umount $WORK_DIR/image
}
run "Extract CCU3 firmware" extract_ccu3_firmware

cd $CNT_ROOTFS

run "Patch CCU3 firmware" patch -E -l -p1 < $CURRENT_DIR/pivccu/firmware3.patch

function patch_version {
  sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/config/cp_maintenance.cgi || throw "Could not patch cp_maintenance.cgi"
  sed -i "s/@@@pivccu_version@@@/$PKG_VERSION/g" $CNT_ROOTFS/www/webui/webui.js || throw "Could not patch webui.js"
}
run "Patch version numbers" patch_version

function add_hm_mod_rpi_pcb_firmware {
  mkdir -p $CNT_ROOTFS/firmware/HM-MOD-UART
  run "Download firmware" wget -q -O $CNT_ROOTFS/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3 https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/dualcopro_si1002_update_blhm.eq3
  run "Download fwmap" wget -q -O $CNT_ROOTFS/firmware/HM-MOD-UART/fwmap https://raw.githubusercontent.com/eq-3/occu/abc3d4c8ee7d0ba090407b6b4431aeca42aeb014/firmware/HM-MOD-UART/fwmap
}
run "Add HM-MOD-RPI-PCB firmware" add_hm_mod_rpi_pcb_firmware

mkdir -p $CNT_ROOTFS/firmware/HmIP-RFUSB
run "Add HmIP-RFUSB firmware" wget -q -P $CNT_ROOTFS/firmware/HmIP-RFUSB https://raw.githubusercontent.com/eq-3/occu/77f5f55eb456e9974355d645e2005a1d355063af/firmware/HmIP-RFUSB/dualcopro_update_blhmip-4.4.18.eq3

mkdir -p $CNT_ROOTFS/etc/piVCCU3
run "Add piVCCU container files" cp -p $CURRENT_DIR/pivccu/container3/* $CNT_ROOTFS/etc/piVCCU3

mkdir -p $CNT_ROOT/userfs

mkdir -p $TARGET_DIR/etc/piVCCU3
run "Add lxc.config" cp -p $CURRENT_DIR/pivccu/host3/lxc.config $TARGET_DIR/etc/piVCCU3

mkdir -p $TARGET_DIR/etc/default
run "Add /etc/default/pivccu3" cp -p $CURRENT_DIR/pivccu/host3/default.config $TARGET_DIR/etc/default/pivccu3

mkdir -p $TARGET_DIR/etc/udev/rules.d
run "Add udev rules" cp -p $CURRENT_DIR/pivccu/host3/*.rules $TARGET_DIR/etc/udev/rules.d

function copy_host_binaries {
  cp -p $CURRENT_DIR/pivccu/host3/*.sh $CNT_ROOT || throw "Could not copy .sh files"
  cp -p $CURRENT_DIR/pivccu/host3/*.inc $CNT_ROOT || throw "Could not copy .inc files"
}
run "Add piVCCU host binaries" copy_host_binaries

mkdir -p $TARGET_DIR/lib/systemd/system/
run "Add systemd service files" cp -p $CURRENT_DIR/pivccu/host3/*.service $TARGET_DIR/lib/systemd/system/

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/pivccu3/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
  sed -i "s/{PKG_ARCH}/armhf/g" $file
done

cd $WORK_DIR

run "Build armhf package" dpkg-deb --build -Zxz pivccu3-$PKG_VERSION

run "Copy armhf package to local directory" cp pivccu3-$PKG_VERSION.deb $CURRENT_DIR/pivccu3-$PKG_VERSION-armhf.deb

function add_openjdk {
  run "Download openjdk-11-jre.deb" wget -O openjdk-11-jre.deb https://archive.debian.org/debian/pool/main/o/openjdk-11/openjdk-11-jre_11.0.6+10-1~bpo9+1_armhf.deb
  run "Download openjdk-11-jre-headless.deb" wget -O openjdk-11-jre-headless.deb https://archive.debian.org/debian/pool/main/o/openjdk-11/openjdk-11-jre-headless_11.0.6+10-1~bpo9+1_armhf.deb

  run "Extract openjdk-11-jre.deb" dpkg-deb -x openjdk-11-jre.deb .
  run "Extract openjdk-11-jre-headless.deb" dpkg-deb -x openjdk-11-jre-headless.deb .

  run "Copy openjdk files" mv usr/lib/jvm/java-11-openjdk-armhf $CNT_ROOTFS/opt/openjdk
  run "Copy openjdk files" mv etc/java-11-openjdk $CNT_ROOTFS/etc
  run "Remove old jdk files" rm -f $CNT_ROOTFS/opt/java
  run "Create java symlink" ln -s /opt/openjdk $CNT_ROOTFS/opt/java
}
run "Add compatible openjdk package for arm64" add_openjdk

rm -rf $TARGET_DIR/DEBIAN/*
cp -p $CURRENT_DIR/package/pivccu3/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  sed -i "s/{CCU_VERSION}/$CCU_VERSION/g" $file
  sed -i "s/{PKG_ARCH}/arm64/g" $file
done

run "Build arm64 package" dpkg-deb --build -Zxz pivccu3-$PKG_VERSION

run "Copy arm64 package to local directory" cp pivccu3-$PKG_VERSION.deb $CURRENT_DIR/pivccu3-$PKG_VERSION-arm64.deb

run "Remove temporary files" rm -rf $WORK_DIR

