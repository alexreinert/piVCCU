#!/bin/bash

. /etc/default/pivccu3

modprobe ip_tables || true
modprobe ip6_tables || true

modprobe eq3_char_loop && RC=$? || RC=$?
if [ $RC -ne 0 ]; then
  logger -t piVCCU3 -p user.err -s "Could not load required kernel module eq3_char_loop." 1>&2
  exit
fi

. /var/lib/piVCCU3/detect_hardware.inc

if [ -z "$HM_HMRF_DEV" ]; then
  logger -t piVCCU3 -p user.warn -s "HMRF hardware was not detected" 1>&2
fi

if [ -z "$HM_HMIP_DEV" ]; then
  logger -t piVCCU3 -p user.warn -s "HMIP hardware was not detected" 1>&2
fi

RED_PIN=0
GREEN_PIN=0
BLUE_PIN=0

if [ "$HM_HMIP_DEV" == "RPI-RF-MOD" ]; then
  modprobe dummy_rx8130 || true

  if [ -e "/sys/module/generic_raw_uart/parameters/red_gpio_pin" ]; then
    RED_PIN=`cat /sys/module/generic_raw_uart/parameters/red_gpio_pin`
    GREEN_PIN=`cat /sys/module/generic_raw_uart/parameters/green_gpio_pin`
    BLUE_PIN=`cat /sys/module/generic_raw_uart/parameters/blue_gpio_pin`
  fi
  if [ -e "/sys/class/raw-uart/$UART_DEV/red_gpio_pin" ]; then
    RED_PIN=`cat /sys/class/raw-uart/$UART_DEV/red_gpio_pin`
    GREEN_PIN=`cat /sys/class/raw-uart/$UART_DEV/green_gpio_pin`
    BLUE_PIN=`cat /sys/class/raw-uart/$UART_DEV/blue_gpio_pin`
  fi
fi

modprobe ledtrig-timer || modprobe led_trigger_timer || true
modprobe ledtrig-default-on || true
modprobe rpi_rf_mod_led red_gpio_pin=$RED_PIN green_gpio_pin=$GREEN_PIN blue_gpio_pin=$BLUE_PIN || true

mkdir -p /var/lib/piVCCU3/lxc

if [ -z "$BRIDGE" ]; then
  logger -t piVCCU3 -p user.warning -s "No network bridge could be detected." 1>&2
fi

rm -rf /tmp/pivccu-media
mkdir -p /tmp/pivccu-media

rm -rf /tmp/pivccu-var
mkdir -p /tmp/pivccu-var/pivccu

CONFIG_FILE=/var/lib/piVCCU3/lxc/config
cat /etc/piVCCU3/lxc.config > $CONFIG_FILE

sed -i $CONFIG_FILE -e "s/<mac_auto>/$MAC/"
sed -i $CONFIG_FILE -e "s/<bridge_auto>/$BRIDGE/"
sed -i $CONFIG_FILE -e "s/<eq3loop_major>/$HM_EQ3LOOP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<uart_major>/$HM_RAW_UART_MAJOR/"
sed -i $CONFIG_FILE -e "s/<uart_minor>/$HM_RAW_UART_MINOR/"
sed -i $CONFIG_FILE -e "s/<hmip_major>/$HM_HMIP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<hmip_minor>/$HM_HMIP_MINOR/"

command -v lxc-update-config > /dev/null && lxc-update-config -c $CONFIG_FILE

cat > /tmp/pivccu-var/pivccu/conf << EOF
HM_RAW_UART_MAJOR='$HM_RAW_UART_MAJOR'
HM_RAW_UART_MINOR='$HM_RAW_UART_MINOR'
HM_HMIP_MAJOR='$HM_HMIP_MAJOR'
HM_HMIP_MINOR='$HM_HMIP_MINOR'
HM_EQ3LOOP_MAJOR='$HM_EQ3LOOP_MAJOR'
HM_HMIP_DEV='$HM_HMIP_DEV'
HM_HMIP_DEVNODE='$HM_HMIP_DEVNODE'
HM_HMIP_SERIAL='$HM_HMIP_SERIAL'
HM_HMIP_VERSION='$HM_HMIP_VERSION'
HM_HMIP_SGTIN='$HM_HMIP_SGTIN'
HM_HMIP_ADDRESS='$HM_HMIP_ADDRESS'
HM_HMIP_DEVTYPE='$HM_HMIP_DEVTYPE'
HM_HMRF_DEV='$HM_HMRF_DEV'
HM_HMRF_DEVNODE='$HM_HMRF_DEVNODE'
HM_HMRF_SERIAL='$HM_HMRF_SERIAL'
HM_HMRF_VERSION='$HM_HMRF_VERSION'
HM_HMRF_ADDRESS='$HM_HMRF_ADDRESS'
HM_HMRF_DEVTYPE='$HM_HMRF_DEVTYPE'
EOF

OIFS=$IFS
IFS=,
declare -a devices=($PIVCCU_USB_DEVICES)
IFS=$OIFS

USBDISKINDEX=1

for dev in ${devices[@]}; do
  IFS=";" read vendor_id model_id serial_short usb_interface_num part_entry_uuid  <<< "$dev"

  FOUND=0
  for sysdevpath in $(find /sys/bus/usb/devices/usb*/ -name dev); do
    syspath="${sysdevpath%/dev}"

    declare -A UDEV_PROPERTIES=()
    while IFS='=' read -r a b; do UDEV_PROPERTIES["$a"]="$b"; done < <(udevadm info -q property -p $syspath)

    [[ "${UDEV_PROPERTIES[ID_VENDOR_ID]}" != "$vendor_id" ]] && continue
    [[ "${UDEV_PROPERTIES[ID_MODEL_ID]}" != "$model_id" ]] && continue
    [[ "${UDEV_PROPERTIES[ID_SERIAL_SHORT]}" != "$serial_short" ]] && continue
    [[ "${UDEV_PROPERTIES[ID_USB_INTERFACE_NUM]}" != "$usb_interface_num" ]] && continue
    [[ "${UDEV_PROPERTIES[ID_PART_ENTRY_UUID]}" != "$part_entry_uuid" ]] && continue

    FOUND=1

    if [ "${UDEV_PROPERTIES[DEVTYPE]}" == "partition" ]; then
      DEVTYPE="b"
      echo "mount -t ${UDEV_PROPERTIES[ID_FS_TYPE]} -o noexec,nodev,noatime,nodiratime ${UDEV_PROPERTIES[DEVNAME]} /media/usb$USBDISKINDEX" >> /tmp/pivccu-var/pivccu/create-mounts
      mkdir -p /tmp/pivccu-media/usb$USBDISKINDEX
      if [ ! -e /tmp/pivccu-media/usb0 ]; then
        ln -s /media/usb$USBDISKINDEX /tmp/pivccu-media/usb0
      fi
      mkdir -p /tmp/pivccu-var/status
      echo "HAS_USB=1" >> /tmp/pivccu-var/pivccu/conf
      let "USBDISKINDEX++"
    else
      DEVTYPE="c"
    fi

    echo "lxc.cgroup.devices.allow = $DEVTYPE ${UDEV_PROPERTIES[MAJOR]}:${UDEV_PROPERTIES[MINOR]} rwm" >> $CONFIG_FILE

    echo "rm -f ${UDEV_PROPERTIES[DEVNAME]}" >> /tmp/pivccu-var/pivccu/create-devs
    echo "mknod -m 666 ${UDEV_PROPERTIES[DEVNAME]} $DEVTYPE ${UDEV_PROPERTIES[MAJOR]} ${UDEV_PROPERTIES[MINOR]}" >> /tmp/pivccu-var/pivccu/create-devs

    for alias in ${UDEV_PROPERTIES[DEVLINKS]}; do
      aliasdir=`dirname $alias`
      echo "mkdir -p $aliasdir" >> /tmp/pivccu-var/pivccu/create-devs
      echo "rm -f $alias" >> /tmp/pivccu-var/pivccu/create-devs
      echo "ln -s ${UDEV_PROPERTIES[DEVNAME]} $alias" >> /tmp/pivccu-var/pivccu/create-devs
    done
  done

  (($FOUND)) && continue
  logger -t piVCCU3 -p user.warning -s "Could not find configured USB device $vendor_id:$model_id ($serial_short)." 1>&2
done

PIVCCU_VERSION=$(dpkg -s pivccu3 | grep '^Version: ' | cut -d' ' -f2)

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

wget -O /dev/null -q --timeout=5 "https://www.pivccu.de/latestVersion?version=$PIVCCU_VERSION&product=HM-CCU3&serial=$HM_HMIP_SERIAL&os=$OS_RELEASE&board=$BOARD_TYPE" || true

sysctl -w kernel.sched_rt_runtime_us=-1

if [ -x /etc/piVCCU3/pre-start.sh ]; then
  /etc/piVCCU3/pre-start.sh
fi

if [ -e /proc/sys/abi/cp15_barrier ]; then
  echo 2 > /proc/sys/abi/cp15_barrier
fi
if [ -e /proc/sys/abi/setend ]; then
  echo 2 > /proc/sys/abi/setend
fi

/usr/bin/lxc-start --lxcpath /var/lib/piVCCU3 --name lxc --pidfile /var/run/pivccu3.pid --daemon

if [ -x /etc/piVCCU3/post-start.sh ]; then
  /etc/piVCCU3/post-start.sh
fi

