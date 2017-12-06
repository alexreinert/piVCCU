#!/bin/bash
source $(dirname $0)/detect_board.inc

case "$OVERLAY_MODE" in
  patch)
    if [ -e /boot/dtb/$FDT_FILE.bak ]; then
      cp /boot/dtb/$FDT_FILE.bak /boot/dtb/$FDT_FILE
    else
      echo "piVCCU: Error! Could not find backup file of FDT, cannot revert"
      exit
    fi
    ;;
  overlay)
    sed -i "s/^\(user_overlays=.*\)$OVERLAY\(.*\)/\1\2/" /boot/armbianEnv.txt
    sed -i "/^user_overlays=\s*$/d" /boot/armbianEnv.txt
    ;;
esac

