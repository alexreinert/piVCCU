#!/bin/bash
if [ -e /boot/armbianEnv.txt ]; then
  FDT_FILE=`grep -e '^fdt_file=' /boot/armbianEnv.txt | cut -d= -f2`
fi

if [ ! -z "$FDT_FILE" ]; then
  if [ -e /boot/dtb/$FDT_FILE.bak ]; then
    cp /boot/dtb/$FDT_FILE.bak /boot/dtb/$FDT_FILE
  else
    echo "piVCCU: Could not find backup file of FDT, cannot revert"
    exit 1
  fi
else
  echo "piVCCU: FDT could not be determined"
  exit 1
fi

