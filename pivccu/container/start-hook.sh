#!/bin/sh

EQ3LOOP_MAJOR=`cat /sys/module/plat_eq3ccu2/parameters/eq3charloop_major`
UART_MAJOR=`cat /sys/module/plat_eq3ccu2/parameters/uart_major`

# generate/update dev nodes
rm -f /dev/eq3loop
rm -f /dev/ttyS0
rm -f /dev/mmd_bidcos
rm -f /dev/mxs_auart_raw.0

mknod -m 666 /dev/eq3loop c $EQ3LOOP_MAJOR 0
mknod -m 666 /dev/ttyS0 c $EQ3LOOP_MAJOR 1
mknod -m 666 /dev/mmd_bidcos c $EQ3LOOP_MAJOR 2
mknod -m 666 /dev/mxs_auart_raw.0 c $UART_MAJOR 0

mkdir -p /dev/net
if [ ! -e /dev/net/tun ]; then
  mknod -m 666 /dev/net/tun c 10 200
fi

if [ ! -e /dev/ptmx ]; then
  mknod -m 666 /dev/ptmx c 5 2
fi

# emulate initialised SD card
mkdir -p /var/status
touch /var/status/SDmounted
touch /var/status/hasSD
touch /var/status/SDinitialised

