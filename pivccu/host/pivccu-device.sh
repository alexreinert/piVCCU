#!/bin/bash

function showUsage {
  echo "pivccu-device <add|delete|listavailable> [device]"
  exit 1
}

case "$1" in
  add|delete)
    if [ $# -ne 2 ]; then
      showUsage
    else
      if [ -e $2 ]; then
        /usr/bin/lxc-device --lxcpath /var/lib/piVCCU --name lxc $1 $2
      else
        echo "Device \"$2\" does not exist"
      fi
    fi
    ;;

  listavailable)
    for sysdevpath in $(find /sys/bus/usb/devices/usb*/ -name dev); do
    (
      syspath="${sysdevpath%/dev}"
      devname="$(udevadm info -q name -p $syspath)"
      [[ "$devname" == "bus/"* ]] && continue
      eval "$(udevadm info -q property --export -p $syspath)"
      [[ -z "$ID_SERIAL" ]] && continue
      echo "/dev/$devname - $ID_SERIAL"
    )
    done
    ;;

  *)
    showUsage
    ;;
esac

