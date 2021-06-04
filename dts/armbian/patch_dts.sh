#!/bin/bash

source $(dirname $0)/detect_board.inc

case "$OVERLAY_MODE" in
  patch)
    TMP_DIR=`mktemp -d`
    for FILE in $FDT_FILE; do
      if [ -e /boot/dtb/$FILE ]; then
        dtc -I dtb -O dts -q -o $TMP_DIR/devicetree.dts /boot/dtb/$FILE

        if [ `grep -c -e 'compatible = "pivccu,' $TMP_DIR/devicetree.dts` -eq 0 ]; then
          echo "piVCCU: Patching DTB $FILE"
          cp /boot/dtb/$FILE /boot/dtb/$FILE.bak
          cat $INCLUDE_FILE >> $TMP_DIR/devicetree.dts
          dtc -I dts -O dtb -q -o $TMP_DIR/devicetree.dtb $TMP_DIR/devicetree.dts
          cp $TMP_DIR/devicetree.dtb /boot/dtb/$FILE
        else
          echo "piVCCU: DTB $FILE was already patched"
        fi

        rm $TMP_DIR/devicetree.dts
      fi
    done

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

