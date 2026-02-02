#!/bin/bash
PKG_BUILD=88

PKG_VERSION=1.0.$PKG_BUILD

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
TARGET_DIR=$WORK_DIR/pivccu-modules-dkms-$PKG_VERSION

function create_package_dir {
  mkdir -p $TARGET_DIR/usr/src/pivccu-$PKG_VERSION || throw "Could not create temporary directory"
}

function create_dkms_config {
  DKMS_CONF_FILE=$TARGET_DIR/usr/src/pivccu-$PKG_VERSION/dkms.conf

  cat <<EOF >> $DKMS_CONF_FILE
PACKAGE_NAME="pivccu"
PACKAGE_VERSION="$PKG_VERSION"

MAKE="make ARCH=\`uname -m | sed -e s/i.86/x86/ -e s/x86_64/x86/ -e s/arm.*/arm/ -e s/aarch64.*/arm64/\` all"
CLEAN="make ARCH=\`uname -m | sed -e s/i.86/x86/ -e s/x86_64/x86/ -e s/arm.*/arm/ -e s/aarch64.*/arm64/\` clean"

AUTOINSTALL="yes"

EOF

  index=0
  for file in $TARGET_DIR/usr/src/pivccu-$PKG_VERSION/*.c; do
    modname=$(basename "$file" .c)
    echo "BUILT_MODULE_NAME[$index]=\"$modname\"" >> $DKMS_CONF_FILE
    echo "DEST_MODULE_LOCATION[$index]=\"/kernel/drivers/pivccu\"" >> $DKMS_CONF_FILE
    index=$(expr $index + 1)
  done
}

function copy_files {
  cp $CURRENT_DIR/kernel/* $TARGET_DIR/usr/src/pivccu-$PKG_VERSION
  mkdir -p $TARGET_DIR/lib/systemd/system/
  cp -p $CURRENT_DIR/pivccu/dkms/*.service $TARGET_DIR/lib/systemd/system/
  cp -p $CURRENT_DIR/pivccu/rtc/*.service $TARGET_DIR/lib/systemd/system/
  cp -p $CURRENT_DIR/pivccu/rtc/*.timer $TARGET_DIR/lib/systemd/system/

  mkdir -p $TARGET_DIR/var/lib/piVCCU/dkms
  cp -p $CURRENT_DIR/pivccu/dkms/*.sh $TARGET_DIR/var/lib/piVCCU/dkms

  mkdir -p $TARGET_DIR/lib/udev/rules.d
  cp -p $CURRENT_DIR/pivccu/rtc/*.rules $TARGET_DIR/lib/udev/rules.d
}

function create_package_files {
  mkdir -p $TARGET_DIR/DEBIAN
  cp -p $CURRENT_DIR/package/pivccu-modules-dkms/* $TARGET_DIR/DEBIAN
  for file in $TARGET_DIR/DEBIAN/*; do
    sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
  done
}

cd $WORK_DIR

run "Create package directory" create_package_dir
run "Copy files" copy_files
run "Create DKMS config file" create_dkms_config
run "Create package meta files" create_package_files

cd $WORK_DIR

run "Create deb package" dpkg-deb --build -Zxz pivccu-modules-dkms-$PKG_VERSION
run "Copy package to local directory" cp pivccu-modules-dkms-*.deb $CURRENT_DIR
run "Cleanup temporary files" rm -rf $WORK_DIR

