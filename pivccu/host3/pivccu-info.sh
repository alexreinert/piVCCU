#!/bin/bash
echo "piVCCU version: `dpkg -s pivccu3 | grep '^Version: ' | cut -d' ' -f2`"

if [ $EUID != 0 ]; then
  echo "Please run as root"
  exit 1
fi

. /etc/default/pivccu3

modprobe -a eq3_char_loop &> /dev/null && RC=$? || RC=$?
if [ $RC -eq 0 ]; then
  MODULE_STATE="Available"
else
  MODULE_STATE="Not available"
fi
echo "Kernel modules: $MODULE_STATE"

if [ -e /sys/devices/virtual/raw-uart ] && [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --state --no-humanize` == "STOPPED" ]; then
  . /var/lib/piVCCU3/detect_hardware.inc
else
  if [ -e /tmp/pivccu-var/pivccu/conf ]; then
    . /tmp/pivccu-var/pivccu/conf
  fi
fi

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

if [ -z "$HMRF_HARDWARE" ]; then
  HMRF_HARDWARE='unknown'
fi
echo "HMRF Hardware:  $HMRF_HARDWARE"

if [ ! -z "$UART_DEVICE_TYPE" ]; then
  echo " Connected via: $UART_DEVICE_TYPE (/dev/$UART_DEV)"
fi

if [ -z "$BOARD_SERIAL" ]; then
  BOARD_SERIAL='unknown'
fi
echo " Board serial:  $BOARD_SERIAL"

if [ -z "$RADIO_MAC" ]; then
  RADIO_MAC='unknown'
fi
echo " Radio MAC:     $RADIO_MAC"

if [ -z "$HMIP_HARDWARE" ]; then
  HMIP_HARDWARE='unknown'
fi
echo "HMIP Hardware:  $HMIP_HARDWARE"

if [ -z "$SGTIN" ]; then
  SGTIN='unknown'
fi
echo " SGTIN:         $SGTIN"

if [ -z "$HMIP_RADIO_MAC" ]; then
  HMIP_RADIO_MAC='unknown'
fi
echo " Radio MAC:     $HMIP_RADIO_MAC"

/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --ips --pid --stats --state

