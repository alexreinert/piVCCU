#!/bin/bash

# test if hardware uart of Raspberry Pi 3 is assigned to gpio pins
if [ -f /proc/device-tree/model ] && [ `grep -c "Raspberry Pi 3" /proc/device-tree/model` == 1 ]; then
  if ! cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
    logger -t piVCCU -p user.err -s "Hardware UART is not assigned to GPIO pins." 1>&2
    exit 1
  fi
fi

if [ ! -e /sys/devices/virtual/raw-uart ]; then
  logger -t piVCCU -p user.err -s "Could not locate raw uart interface. Are the kernel modules and the device tree overlays installed?" 1>&2
  exit 1
fi

# load modules
modprobe -a plat_eq3ccu2 eq3_char_loop &> /dev/null
if [ $? -ne 0 ]; then
  logger -t piVCCU -p user.err -s "Could not load kernel modules plat_eq3ccu2 and eq3_char_loop." 1>&2
  exit 1
fi

# determine char major number
EQ3LOOP_MAJOR=`cat /sys/devices/virtual/eq3loop/eq3loop/dev | cut -d: -f1`
UART_MAJOR=`cat /sys/devices/virtual/raw-uart/raw-uart/dev | cut -d: -f1`

#determine radio mac and serial
mount --bind /dev /var/lib/piVCCU/rootfs/dev
BOARD_SERIAL=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/raw-uart -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5`
RADIO_MAC=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd read-default-rf-address -f /dev/raw-uart -h | grep "^0x"`
umount /var/lib/piVCCU/rootfs/dev

if [ -z "$BOARD_SERIAL" ]; then
  logger -t piVCCU -p user.warning -s "Radio module was not detected." 1>&2
fi

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

echo -n $EQ3LOOP_MAJOR > /sys/module/plat_eq3ccu2/parameters/eq3charloop_major
echo -n $UART_MAJOR > /sys/module/plat_eq3ccu2/parameters/uart_major
echo -n $BOARD_SERIAL > /sys/module/plat_eq3ccu2/parameters/board_serial
echo -n $RADIO_MAC > /sys/module/plat_eq3ccu2/parameters/radio_mac

if [ -x /etc/piVCCU/pre-start.sh ]; then
  /etc/piVCCU/pre-start.sh
fi

# multimacd needs rt scheduling to work
sysctl -w kernel.sched_rt_runtime_us=-1

# start container
/usr/bin/lxc-start --lxcpath /var/lib/piVCCU --name lxc --pidfile /var/run/pivccu.pid --daemon

if [ -x /etc/piVCCU/post-start.sh ]; then
  /etc/piVCCU/post-start.sh
fi

