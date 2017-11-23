#!/bin/bash
echo "piVCCU version: `dpkg -s pivccu | grep '^Version: ' | cut -d' ' -f2`"

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

if [ -f /proc/device-tree/model ] && [ `grep -c "Raspberry Pi 3" /proc/device-tree/model` == 1 ]; then
  if cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
    UART_STATE="Assigned to GPIO pins"
  else
    UART_STATE="Not assigned to GPIO pins"
  fi
  echo "Rasp.Pi3 UART:  $UART_STATE"
fi

if [ -e /sys/devices/virtual/raw-uart ] && [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU/ --name lxc --state --no-humanize` == "STOPPED" ]; then
  mount --bind /dev /var/lib/piVCCU/rootfs/dev
  BOARD_SERIAL=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/raw-uart -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5`
  umount /var/lib/piVCCU/rootfs/dev
else
  if [ -f /sys/module/plat_eq3ccu2/parameters/board_serial ]; then
    BOARD_SERIAL=`cat /sys/module/plat_eq3ccu2/parameters/board_serial | sed 1q`
  fi
fi
if [ -z "$BOARD_SERIAL" ]; then
  BOARD_SERIAL="Unknown"
fi
echo "Board serial:   $BOARD_SERIAL"

/usr/bin/lxc-info --lxcpath /var/lib/piVCCU/ --name lxc --ips --pid --stats --state

