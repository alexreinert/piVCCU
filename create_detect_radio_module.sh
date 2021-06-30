#!/bin/bash
PKG_BUILD=5

PKG_VERSION=1.0-$PKG_BUILD

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

cd $WORK_DIR

SRC_DIR=$WORK_DIR/src
mkdir -p $SRC_DIR

cp -p $CURRENT_DIR/detect_radio_module/* $SRC_DIR

declare -A architectures=(["armhf"]="arm-linux-gnueabihf-g++" ["arm64"]="aarch64-linux-gnu-g++" ["i386"]="i686-linux-gnu-g++" ["amd64"]="g++")
for ARCH in "${!architectures[@]}"
do
  ARCH_COMP=${architectures[$ARCH]}

  cd $SRC_DIR
  make clean
  make CXX=$ARCH_COMP

  TARGET_DIR=$WORK_DIR/detect-radio-module-$PKG_VERSION-$ARCH
  mkdir -p $TARGET_DIR/bin

  cp -p $SRC_DIR/detect_radio_module $TARGET_DIR/bin

  mkdir -p $TARGET_DIR/DEBIAN
  cp -p $CURRENT_DIR/package/detect-radio-module/* $TARGET_DIR/DEBIAN
  for file in $TARGET_DIR/DEBIAN/*; do
    sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
    sed -i "s/{PKG_ARCH}/$ARCH/g" $file
  done

  cd $WORK_DIR

  dpkg-deb --build detect-radio-module-$PKG_VERSION-$ARCH
done

cp detect-radio-module-*.deb $CURRENT_DIR

echo "Please clean-up the work dir temp folder $WORK_DIR, e.g. by doing rm -R $WORK_DIR"

