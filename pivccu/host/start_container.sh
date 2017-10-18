#!/bin/bash

# test if hardware uart is assigned to gpio pins
if ! cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
  logger -t piVCCU -p user.err -s "Hardware UART is not assigned to GPIO pins." 1>&2
  exit 1
fi

# load modules
modprobe -a eq3_char_loop bcm2835_raw_uart
if [ $? -ne 0 ]; then
  logger -t piVCCU -p user.err -s "Could not load kernel modules eq3_char_loop and bcm2835_raw_uart." 1>&2
  exit 1
fi

# reset homematic board
if [ ! -d /sys/class/gpio/gpio18 ]
then
  echo 18 > /sys/class/gpio/export
  echo out > /sys/class/gpio/gpio18/direction
fi

echo 0 > /sys/class/gpio/gpio18/value
sleep 0.1
echo 1 > /sys/class/gpio/gpio18/value

# determine char major number
EQ3LOOP_MAJOR=`cat /sys/devices/virtual/eq3loop/eq3loop/dev | cut -d: -f1`
UART_MAJOR=`cat /sys/devices/virtual/bcm2835-raw-uart/bcm2835-raw-uart/dev | cut -d: -f1`

# create config file
mkdir -p /var/lib/piVCCU/lxc

BRIDGE=`brctl show | sed -n 2p | awk '{print $1}'`
MAIN_INTERFACE=`route | grep 'default' | awk '{print $8}'`
HOST_MAC=`cat /sys/class/net/$MAIN_INTERFACE/address`
MAC=`echo $HOST_MAC | md5sum | sed 's/\(.\)\(..\)\(..\)\(..\)\(..\)\(..\).*/\1a:\2:\3:\4:\5:\6/'`

if [ -z "$BRIDGE" ]; then
  logger -t piVCCU -p user.warning -s "No network bridge could be detected." 1>&2
fi

CONFIG_FILE=/var/lib/piVCCU/lxc/config
cat /etc/piVCCU/lxc.config > $CONFIG_FILE

sed -i $CONFIG_FILE -e "s/<mac_auto>/$MAC/"
sed -i $CONFIG_FILE -e "s/<bridge_auto>/$BRIDGE/"
sed -i $CONFIG_FILE -e "s/<eq3loop_major>/$EQ3LOOP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<uart_major>/$UART_MAJOR/"

DEVICES_FILE=/var/lib/piVCCU/lxc/device_majors
rm -f $DEVICES_FILE
echo "EQ3LOOP_MAJOR=$EQ3LOOP_MAJOR" >> $DEVICES_FILE
echo "UART_MAJOR=$UART_MAJOR" >> $DEVICES_FILE

# start container
/usr/bin/lxc-start --lxcpath /var/lib/piVCCU --name lxc --pidfile /var/run/pivccu.pid --daemon
