#!/bin/sh

. /var/pivccu/conf

rm -f /dev/eq3loop
rm -f /dev/mmd_bidcos
rm -f /dev/mmd_hmip
rm -f /dev/raw-uart

mknod -m 666 /dev/eq3loop c $EQ3LOOP_MAJOR 0
mknod -m 666 /dev/mmd_hmip c $HMIP_MAJOR $HMIP_MINOR
mknod -m 666 /dev/mmd_bidcos c $EQ3LOOP_MAJOR 2
mknod -m 666 /dev/raw-uart c $UART_MAJOR 0

mkdir -p /dev/net
if [ ! -e /dev/net/tun ]; then
  mknod -m 666 /dev/net/tun c 10 200
fi

if [ ! -e /dev/ptmx ]; then
  mknod -m 666 /dev/ptmx c 5 2
fi

if [ -e /var/pivccu/create-devs ]; then
  /bin/sh /var/pivccu/create-devs
fi
if [ -e /var/pivccu/create-mounts ]; then
  /bin/sh /var/pivccu/create-mounts
fi

mkdir -p /var/status

if [ -f /var/status/hasUSB ]; then
  mkdir -p /media/usb0/measurement
  if [ -d /media/usb0/measurement ]; then
    touch /var/status/USBinitialised
    touch /var/status/SDinitialised
  fi
fi

case $HMRF_HARDWARE in
  "FAKE_HMRF")
    HM_HMRF_DEV="HM-MOD-RPI-PCB"
    HM_HMRF_DEVNODE="/dev/raw-uart"
    HM_HMRF_VERSION=`cat /sys/module/fake_hmrf/parameters/firmware_version` 
    ;;
  "HM-MOD-RPI-PCB")
    HM_HMRF_DEV="HM-MOD-RPI-PCB"
    HM_HMRF_DEVNODE="/dev/raw-uart"
    HM_HMRF_VERSION="$FW_VERSION"
    ;;
  "RPI-RF-MOD")
    HM_HMRF_DEV="RPI-RF-MOD"
    HM_HMRF_DEVNODE="/dev/raw-uart"
    HM_HMRF_VERSION="$FW_VERSION"
    ;;
esac

case $HMIP_HARDWARE in
  "HmIP-RFUSB")
    HM_HMIP_DEV="HMIP-RFUSB"
    HM_HMIP_DEVNODE="/dev/mmd_hmip"
    HM_HMIP_VERSION="$FW_VERSION"
    ;;
  *)
    HM_HMIP_DEV="$HM_HMRF_DEV"
    HM_HMIP_DEVNODE="$HM_HMRF_DEVNODE"
    HM_HMIP_VERSION="$HM_HMRF_VERSION"
    ;;
esac

cat > /var/hm_mode << EOF
HM_HOST='rpi3'
HM_HOST_GPIO_UART='/dev/raw-uart'
HM_HOST_GPIO_RESET=''
HM_LED_GREEN=''
HM_LED_RED=''
HM_LED_YELLOW=''
HM_RTC=''
HM_MODE='NORMAL'
HM_HMRF_DEVNODE='$HM_HMRF_DEVNODE'
HM_HMIP_DEVNODE='$HM_HMIP_DEVNODE'
HM_HMRF_DEV='$HM_HMRF_DEV'
HM_HMIP_DEV='$HM_HMIP_DEV'
HM_HMRF_SERIAL='$BOARD_SERIAL'
HM_HMRF_VERSION='$HM_HMRF_VERSION'
HM_HMRF_ADDRESS='$RADIO_MAC'
HM_HMIP_SGTIN='$SGTIN'
HM_HMIP_SERIAL='$BOARD_SERIAL'
HM_HMIP_VERSION='$HM_HMIP_VERSION'
HM_HMIP_ADDRESS='$RADIO_MAC'
EOF

. /var/hm_mode

echo "${HM_HMRF_SERIAL}" >/var/board_serial
echo "${HM_HMRF_VERSION}" >/var/rf_firmware_version
echo "${HM_HMRF_ADDRESS}" >/var/rf_address
echo "${HM_HMIP_SERIAL}" >/var/hmip_board_serial
echo "${HM_HMIP_VERSION}" >/var/hmip_firmware_version
echo "${HM_HMIP_ADDRESS}" >/var/hmip_address
echo "${HM_HMIP_SGTIN}" >/var/board_sgtin
echo "${HM_HMIP_SGTIN}" >/var/hmip_board_sgtin

