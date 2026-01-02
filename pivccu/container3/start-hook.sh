#!/bin/sh

. /var/pivccu/conf

/bin/find /var/* -maxdepth 0 ! -name pivccu -exec  /bin/rm -rf {} \;
/bin/rm -rf /tmp/*

/bin/rm -f /dev/eq3loop
/bin/rm -f /dev/mmd_bidcos
/bin/rm -f /dev/mmd_hmip
/bin/rm -f /dev/raw-uart

/bin/mknod -m 666 /dev/eq3loop c $HM_EQ3LOOP_MAJOR 0
/bin/mknod -m 666 /dev/mmd_hmip c $HM_HMIP_MAJOR $HM_HMIP_MINOR
/bin/mknod -m 666 /dev/mmd_bidcos c $HM_EQ3LOOP_MAJOR 2
/bin/mknod -m 666 /dev/raw-uart c $HM_RAW_UART_MAJOR $HM_RAW_UART_MINOR

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

if [ "0$HAS_USB" -eq 1 ]; then
  touch /var/status/hasUSB
  touch /var/status/hasSD
  mkdir -p /media/usb0/measurement
  if [ -d /media/usb0/measurement ]; then
    touch /var/status/USBinitialised
    touch /var/status/SDinitialised
  fi
fi

HM_HMIP_DEVNODE="/dev/raw-uart"
HM_HMRF_DEVNODE="/dev/raw-uart"
if [ "$HM_HMIP_DEV" == "HMIP-RFUSB-TK" ]; then
  HM_HMIP_DEVNODE="/dev/mmd_hmip"
fi

cat > /var/hm_mode << EOF
HM_HOST='rpi3'
HM_MODE='NORMAL'
HM_LED_GREEN=''
HM_LED_GREEN_MODE1='none'
HM_LED_GREEN_MODE2='none'
HM_LED_RED=''
HM_LED_RED_MODE1='none'
HM_LED_RED_MODE2='none'
HM_LED_YELLOW=''
HM_LED_YELLOW_MODE1='none'
HM_LED_YELLOW_MODE2='none'
HM_HOST_GPIO_UART='/dev/raw-uart'
HM_HOST_GPIO_RESET=''
HM_RTC=''
HM_HMIP_DEV='$HM_HMIP_DEV'
HM_HMIP_DEVNODE='$HM_HMIP_DEVNODE'
HM_HMIP_SERIAL='$HM_HMIP_SERIAL'
HM_HMIP_VERSION='$HM_HMIP_VERSION'
HM_HMIP_SGTIN='$HM_HMIP_SGTIN'
HM_HMIP_ADDRESS='$HM_HMIP_ADDRESS'
HM_HMIP_ADDRESS_ACTIVE='$HM_HMIP_ADDRESS'
HM_HMIP_DEVTYPE='$HM_HMIP_DEVTYPE'
HM_HMRF_DEV='$HM_HMRF_DEV'
HM_HMRF_DEVNODE='$HM_HMRF_DEVNODE'
HM_HMRF_SERIAL='$HM_HMRF_SERIAL'
HM_HMRF_VERSION='$HM_HMRF_VERSION'
HM_HMRF_ADDRESS='$HM_HMRF_ADDRESS'
HM_HMRF_ADDRESS_ACTIVE='$HM_HMRF_ADDRESS'
HM_HMRF_DEVTYPE='$HM_HMRF_DEVTYPE'
EOF

if [[ -n "${HM_HMIP_SERIAL}" ]]; then
  echo -n "${HM_HMIP_SERIAL}" > /var/board_serial
else
  echo -n "${HM_HMRF_SERIAL}" > /var/board_serial
fi

if [[ -n "${HM_HMIP_SGTIN}" ]]; then
  echo -n "${HM_HMIP_SGTIN}" > /var/board_sgtin
fi

echo -n "${HM_HMRF_SERIAL}" > /var/rf_board_serial
echo -n "${HM_HMRF_ADDRESS}" > /var/rf_address
echo -n "${HM_HMRF_VERSION}" > /var/rf_firmware_version
echo -n "${HM_HMIP_SERIAL}" > /var/hmip_board_serial
echo -n "${HM_HMIP_VERSION}" > /var/hmip_firmware_version
echo -n "${HM_HMIP_ADDRESS}" > /var/hmip_address
echo -n "${HM_HMIP_SGTIN}" > /var/hmip_board_sgtin

