#!/bin/bash
PKG_BUILD=2

PKG_VERSION=1.0.$PKG_BUILD

CURRENT_DIR=$(pwd)
WORK_DIR=$(mktemp -d)

cd $WORK_DIR

TARGET_DIR=$WORK_DIR/hb-rf-eth-$PKG_VERSION

mkdir -p $TARGET_DIR/DEBIAN
cp -p $CURRENT_DIR/package/hb-rf-eth/* $TARGET_DIR/DEBIAN
for file in $TARGET_DIR/DEBIAN/*; do
  sed -i "s/{PKG_VERSION}/$PKG_VERSION/g" $file
done

cd $WORK_DIR

dpkg-deb --build -Zxz hb-rf-eth-$PKG_VERSION

cp hb-rf-eth-*.deb $CURRENT_DIR

echo "Please clean-up the work dir temp folder $WORK_DIR, e.g. by doing rm -R $WORK_DIR"

