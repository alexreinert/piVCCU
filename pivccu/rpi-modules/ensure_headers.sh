#!/bin/bash
FW_REPO="https://raw.githubusercontent.com/Hexxeh/rpi-firmware/"
KERNEL_REPO="https://github.com/raspberrypi/linux/"

ACTIVE_KERNEL=`uname -r`
MODULE_DIR="/lib/modules/$ACTIVE_KERNEL"

modinfo generic_raw_uart &> /dev/null
if [ $? -eq 0 ]; then
  exit
fi

if [ -d $MODULE_DIR/build ]; then
  exit
fi

set -e

if [ -f /boot/.firmware_revision ]; then
  # Raspberry Pi OS after rpi-update
  FW_HASH=`cat /boot/.firmware_revision`
  KERNEL_GIT_HASH=`wget -O - -q $FW_REPO$FW_HASH/git_hash`
  KERNEL_GIT_VERSION=`wget -O - -q $FW_REPO$FW_HASH/uname_string7 | cut -d' ' -f3`

  if [ "$KERNEL_GIT_VERSION" != "$ACTIVE_KERNEL" ]; then
    echo "/boot/.firmware_revision does not match active kernel version."
    exit 1
  fi

  echo "Downloading kernel sources from GIT"
  wget -O $MODULE_DIR/linux.tar.gz -q $KERNEL_REPO/archive/$KERNEL_GIT_HASH.tar.gz

  echo "Unpacking kernel sources"
  cd $MODULE_DIR
  tar -xzf $MODULE_DIR/linux.tar.gz
  rm -f $MODULE_DIR/linux.tar.gz
  mv -f $MODULE_DIR/linux-$KERNEL_GIT_HASH $MODULE_DIR/source
  ln -sf $MODULE_DIR/source $MODULE_DIR/build

  # check if parameter set
  if [ "$1" = "-c" ]; then
    echo "Configuring kernel sources"
    VER=`uname -r | sed -r 's/.*\-v([0-9a-z]*)(\+*.*)/\1/'`
    if echo "$VER" | grep -qvx "7\|7l\|8"; then
      echo "Unable to configure kernel"
      exit 1
    fi
    KERNEL="kernel$VER"
    cd $MODULE_DIR/source
    modprobe configs &> /dev/null
    zcat /proc/config.gz > $MODULE_DIR/source/.config
    make modules_prepare > /dev/null
  fi
else
  echo "Could not determine kernel source."
  exit 1
fi
