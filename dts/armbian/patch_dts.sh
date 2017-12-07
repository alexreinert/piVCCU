#!/bin/bash

source $(dirname $0)/detect_board.inc

case "$OVERLAY_MODE" in
  patch)
    TMP_DIR=`mktemp -d`
    cp /boot/dtb/$FDT_FILE $TMP_DIR

    dtc -I dtb -O dts -q -o $TMP_DIR/devicetree.dts /boot/dtb/$FDT_FILE

    if [ `grep -c -e 'compatible = "pivccu,' $TMP_DIR/devicetree.dts` -eq 0 ]; then
      echo "piVCCU: Patching DTB $FDT_FILE"
      cp /boot/dtb/$FDT_FILE /boot/dtb/$FDT_FILE.bak
      cat $INCLUDE_FILE >> $TMP_DIR/devicetree.dts
      dtc -I dts -O dtb -q -o $TMP_DIR/$FDT_FILE $TMP_DIR/devicetree.dts
      cp $TMP_DIR/$FDT_FILE /boot/dtb
    else
      echo "piVCCU: DTB $FDT_FILE was already patched"
    fi

    rm -rf $TMP_DIR
    ;;
  overlay)
    if [ `grep -c "^user_overlays=" /boot/armbianEnv.txt` -eq 0 ]; then
      echo "user_overlays=$OVERLAY" >> /boot/armbianEnv.txt
    elif [ `grep -c "^user_overlays=.*$OVERLAY.*" /boot/armbianEnv.txt` -eq 0 ]; then
      sed -i "s/^user_overlays=/user_overlays=$OVERLAY /" /boot/armbianEnv.txt
    fi
    ;;
esac

