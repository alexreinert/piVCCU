#!/bin/bash
modprobe -a plat_eq3ccu2 eq3_char_loop bcm2835_raw_uart &> /dev/log
if [ $? -eq 0 ]; then
  MODULE_STATE="Available"
else
  MODULE_STATE="Not available"
fi
echo "Kernel modules: $MODULE_STATE"

if cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
  UART_STATE="Assigned to GPIO pins"
else
  UART_STATE="Not assigned to GPIO pins"
fi
echo "UART:           $UART_STATE"

if [ -f /sys/module/plat_eq3ccu2/parameters/board_serial ]; then
  BOARD_SERIAL=`cat /sys/module/plat_eq3ccu2/parameters/board_serial | sed 1q`
  if [ -z "$BOARD_SERIAL" ]; then
    BOARD_SERIAL="Unknown"
  fi
else
  BOARD_SERIAL="Unknown"
fi
echo "Board serial:   $BOARD_SERIAL"

/usr/bin/lxc-info --lxcpath /var/lib/piVCCU/ --name lxc --ips --pid --stats --state

