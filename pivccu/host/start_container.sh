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

command -v lxc-update-config > /dev/null && lxc-update-config -c $CONFIG_FILE

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

PIVCCU_VERSION=$(dpkg -s pivccu | grep '^Version: ' | cut -d' ' -f2)

if [ -e /etc/os-release ]; then
  OS_ID=$(grep '^ID=' /etc/os-release | cut -d '=' -f2)
  VERSION_CODENAME=$(grep '^VERSION_CODENAME=' /etc/os-release | cut -d '=' -f2)
  if [ -z "$VERSION_CODENAME" ]; then
    VERSION_CODENAME=$(grep '^VERSION_ID=' /etc/os-release | cut -d '=' -f2)
  fi
else
  OS_ID=unknown
  VERSION_CODENAME=unknown
fi

OS_ARCH=$(uname -m)

if [ -e /etc/armbian-release ]; then
  BOARD_TYPE=$(grep '^BOARD=' /etc/armbian-release | cut -d '=' -f2)
  ARMBIAN_CODENAME=$(grep '^DISTRIBUTION_CODENAME=' /etc/armbian-release | cut -d '=' -f2)
  if [ -n "$ARMBIAN_CODENAME" ]; then
    VERSION_CODENAME=$ARMBIAN_CODENAME
  fi
  OS_ID=armbian
elif [ -e /sys/firmware/devicetree/base/compatible ]; then
  BOARD_TYPE=$(strings /sys/firmware/devicetree/base/compatible | tr '\n' ':' | tr ',' '_')
else
  BOARD_TYPE=unknown
fi

OS_RELEASE=${OS_ID}_${VERSION_CODENAME}

wget -O /dev/null -q --timeout=5 "https://www.pivccu.de/latestVersion?version=$PIVCCU_VERSION&product=HM-CCU2&serial=$BOARD_SERIAL&os=$OS_RELEASE&board=$BOARD_TYPE" || true

sysctl -w kernel.sched_rt_runtime_us=-1

/usr/bin/lxc-start --lxcpath /var/lib/piVCCU --name lxc --pidfile /var/run/pivccu.pid --daemon

if [ -x /etc/piVCCU/post-start.sh ]; then
  /etc/piVCCU/post-start.sh
fi

