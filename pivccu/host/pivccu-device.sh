#!/bin/bash

function showUsage {
  echo "pivccu-device <add|del|listavailable> [device]"
  exit 1
}

if [ $EUID != 0 ]; then
  echo "Please run as root"
  exit
fi

case "$1" in
  add|del)
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
      syspath="${sysdevpath%/dev}"

      devname="$(udevadm info -q name -p $syspath)"
      [[ "$devname" == "bus/"* ]] && continue

      ID_SERIAL=`udevadm info -q property --export -p $syspath | grep "ID_SERIAL=" | sed -e "s/.*='\(.*\)'/\1/"`
      [[ -z "$ID_SERIAL" ]] && continue

      echo "/dev/$devname - $ID_SERIAL"
    done
    ;;

  *)
    showUsage
    ;;
esac

