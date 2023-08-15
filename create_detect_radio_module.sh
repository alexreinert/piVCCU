#!/bin/bash
PKG_BUILD=7

PKG_VERSION=1.0-$PKG_BUILD

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

cd $WORK_DIR

SRC_DIR=$WORK_DIR/src
mkdir -p $SRC_DIR

cp -p $CURRENT_DIR/detect_radio_module/* $SRC_DIR

function build_binaries {
  ARCH=$1

  cd $SRC_DIR
  run "make clean" make clean
  run "make ($ARCH)" make CXX=$ARCH_COMP
}

function build_package {
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

  dpkg-deb --build -Zxz detect-radio-module-$PKG_VERSION-$ARCH || throw "Error on dpkg-deb"
}

declare -A architectures=(["armhf"]="arm-linux-gnueabihf-g++" ["arm64"]="aarch64-linux-gnu-g++" ["i386"]="i686-linux-gnu-g++" ["amd64"]="g++")
for ARCH in "${!architectures[@]}"
do
  ARCH_COMP=${architectures[$ARCH]}

  run "Build binaries for $ARCH" build_binaries $ARCH
  run "Build package for $ARCH" build_package $ARCH
done

cd $WORK_DIR

run "Copy package to local directory" cp detect-radio-module-*.deb $CURRENT_DIR
run "Cleanup temporary files" rm -rf $WORK_DIR

