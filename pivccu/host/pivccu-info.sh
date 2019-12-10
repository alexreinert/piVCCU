#!/bin/bash
echo "piVCCU version: `dpkg -s pivccu | grep '^Version: ' | cut -d' ' -f2`"

if [ $EUID != 0 ]; then
  echo "Please run as root"
  exit 1
fi

. /etc/default/pivccu

modprobe -a plat_eq3ccu2 eq3_char_loop &> /dev/null
if [ $? -eq 0 ]; then
  MODULE_STATE="Available"
else
  MODULE_STATE="Not available"
fi
echo "Kernel modules: $MODULE_STATE"

if [ -e /sys/devices/virtual/raw-uart ]; then
  RAW_UART_STATE="Available"
else
  RAW_UART_STATE="Not available"
fi
echo "Raw UART dev:   $RAW_UART_STATE"

if [ -f /proc/device-tree/model ] && [ `grep -c "Raspberry Pi" /proc/device-tree/model` == 1 ] && [ `grep -c "Raspberry Pi 2" /proc/device-tree/model` == 0 ]; then
  if cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
    UART_STATE="Assigned to GPIO pins"
  else
    UART_STATE="Not assigned to GPIO pins"
  fi
  echo "Rasp.Pi UART:   $UART_STATE"
fi

if [ -e /sys/devices/virtual/raw-uart ] && [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU/ --name lxc --state --no-humanize` == "STOPPED" ]; then
  . /var/lib/piVCCU/detect_hardware.inc
else
  if [ -e /sys/module/plat_eq3ccu2 ]; then
    BOARD_SERIAL=`cat /sys/module/plat_eq3ccu2/parameters/board_serial | sed 1q`
    RADIO_MAC=`cat /sys/module/plat_eq3ccu2/parameters/radio_mac | sed 1q`
    HMRF_HARDWARE=`cat /sys/module/plat_eq3ccu2/parameters/board_extended_info | sed 1q | cut -d';' -f1`
    HMIP_HARDWARE=`cat /sys/module/plat_eq3ccu2/parameters/board_extended_info | sed 1q | cut -d';' -f2`
    SGTIN=`cat /sys/module/plat_eq3ccu2/parameters/board_extended_info | sed 1q | cut -d';' -f3`
  fi
fi

if [ -z "$HMRF_HARDWARE" ]; then
  HMRF_HARDWARE='unknown'
fi
echo "HMRF Hardware:  $HMRF_HARDWARE"

if [ -z "$HMIP_HARDWARE" ]; then
  HMIP_HARDWARE='unknown'
fi
echo "HMIP Hardware:  $HMIP_HARDWARE"

if [ -z "$BOARD_SERIAL" ]; then
  BOARD_SERIAL='unknown'
fi
echo "Board serial:   $BOARD_SERIAL"

if [ -z "$RADIO_MAC" ]; then
  RADIO_MAC='unknown'
fi
echo "Radio MAC:      $RADIO_MAC"

if [ -z "$SGTIN" ]; then
  SGTIN='unknown'
fi
echo "SGTIN:          $SGTIN"

/usr/bin/lxc-info --lxcpath /var/lib/piVCCU/ --name lxc --ips --pid --stats --state

