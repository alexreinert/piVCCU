#!/bin/sh

. /etc/piVCCU/device_majors

# generate/update dev nodes
rm -f /dev/{eq3loop,ttyS0,mmd_bidcos,mxs_auart_raw.0}
mknod -m 666 /dev/eq3loop c $EQ3LOOP_MAJOR 0
mknod -m 666 /dev/ttyS0 c $EQ3LOOP_MAJOR 1
mknod -m 666 /dev/mmd_bidcos c $EQ3LOOP_MAJOR 2
mknod -m 666 /dev/mxs_auart_raw.0 c $UART_MAJOR 0

mkdir -p /dev/net
if [ ! -e /dev/net/tun ]; then
  mknod -m 666 /dev/net/tun c 10 200
fi

# get radio mac and serial
mkdir -p /tmp/hm-mod-rpi-pcb/parameters
/bin/eq3configcmd update-coprocessor -p /dev/mxs_auart_raw.0 -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5 > /tmp/hm-mod-rpi-pcb/parameters/board_serial
/bin/eq3configcmd read-default-rf-address -f /dev/mxs_auart_raw.0 -h | grep "^0x" > /tmp/hm-mod-rpi-pcb/parameters/radio_mac

# emulate initialised SD card
mkdir -p /var/status
touch /var/status/SDmounted
touch /var/status/hasSD
touch /var/status/SDinitialised

