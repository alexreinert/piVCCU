#!/bin/bash
source $(dirname $0)/detect_board.inc

case "$OVERLAY_MODE" in
  patch)
    for FILE in $FDT_FILE; do
      if [ -e /boot/dtb/$FILE ]; then
        if [ -e /boot/dtb/$FILE.bak ]; then
          cp /boot/dtb/$FILE.bak /boot/dtb/$FILE
        else
          echo "piVCCU: Error! Could not find backup file of $FILE, cannot revert"
          exit
        fi
      fi
    done
    ;;
  overlay)
    sed -i "s/^\(user_overlays=.*\)$OVERLAY\(.*\)/\1\2/" /boot/armbianEnv.txt
    sed -i "/^user_overlays=\s*$/d" /boot/armbianEnv.txt
    ;;
esac

