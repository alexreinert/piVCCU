#!/bin/bash

. /etc/default/pivccu

modprobe -a plat_eq3ccu2 eq3_char_loop &> /dev/null
if [ $? -ne 0 ]; then
  logger -t piVCCU -p user.err -s "Could not load kernel modules plat_eq3ccu2 and eq3_char_loop." 1>&2
  exit 1
fi

. /var/lib/piVCCU/detect_hardware.inc

if [ -z "$HMRF_HARDWARE" ]; then
  logger -t piVCCU -p user.warn -s "HMRF hardware was not detected" 1>&2
fi

if [ -z "$HMIP_HARDWARE" ]; then
  logger -t piVCCU -p user.warn -s "HMIP hardware was not detected" 1>&2
fi

mkdir -p /var/lib/piVCCU/lxc

if [ -z "$BRIDGE" ]; then
  logger -t piVCCU -p user.warning -s "No network bridge could be detected." 1>&2
fi

CONFIG_FILE=/var/lib/piVCCU/lxc/config
cat /etc/piVCCU/lxc.config > $CONFIG_FILE

sed -i $CONFIG_FILE -e "s/<mac_auto>/$MAC/"
sed -i $CONFIG_FILE -e "s/<bridge_auto>/$BRIDGE/"
sed -i $CONFIG_FILE -e "s/<eq3loop_major>/$EQ3LOOP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<uart_major>/$UART_MAJOR/"
sed -i $CONFIG_FILE -e "s/<hmip_major>/$HMIP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<hmip_minor>/$HMIP_MINOR/"

echo -n $EQ3LOOP_MAJOR > /sys/module/plat_eq3ccu2/parameters/eq3charloop_major
echo -n $UART_MAJOR > /sys/module/plat_eq3ccu2/parameters/uart_major
echo -n $HMIP_MAJOR > /sys/module/plat_eq3ccu2/parameters/hmip_major
echo -n $HMIP_MINOR > /sys/module/plat_eq3ccu2/parameters/hmip_minor
echo -n $BOARD_SERIAL > /sys/module/plat_eq3ccu2/parameters/board_serial
echo -n $RADIO_MAC > /sys/module/plat_eq3ccu2/parameters/radio_mac
echo -n "$HMRF_HARDWARE;$HMIP_HARDWARE;$SGTIN" > /sys/module/plat_eq3ccu2/parameters/board_extended_info

if [ -x /etc/piVCCU/pre-start.sh ]; then
  /etc/piVCCU/pre-start.sh
fi

sysctl -w kernel.sched_rt_runtime_us=-1

/usr/bin/lxc-start --lxcpath /var/lib/piVCCU --name lxc --pidfile /var/run/pivccu.pid --daemon

if [ -x /etc/piVCCU/post-start.sh ]; then
  /etc/piVCCU/post-start.sh
fi

