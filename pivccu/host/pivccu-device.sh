#!/bin/bash

function showUsage {
  echo "pivccu-device <add|delete> <device>"
  exit 1
}

if [ $# -ne 2 ]; then
  showUsage
fi

/usr/bin/lxc-device --lxcpath /var/lib/piVCCU --name lxc $1 $2

