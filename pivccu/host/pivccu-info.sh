#!/bin/bash
if [ -e /lib/modules/$(uname -r)/kernel/drivers/char/eq3_char_loop.ko ]; then
  if [ `lsmod | grep "eq3_char_loop" -c` -eq 1 ]; then
    MODULE_STATE="Loaded"
  else
    MODULE_STATE="Available, but not loaded"
  fi
else
  MODULE_STATE="Not existing"
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

