#!/bin/bash

if [ -e /etc/default/pivccu3 ]; then
  . /etc/default/pivccu3
fi

if [ -e /sys/devices/virtual/eq3loop ]; then
  HM_EQ3LOOP_MAJOR=`cat /sys/devices/virtual/eq3loop/eq3loop/dev | cut -d: -f1`
fi

if [ -z "$PIVCCU_FAKE_SERIAL" ]; then
  PIVCCU_FAKE_SERIAL=`shuf -i 1-9999999 -n 1`
  PIVCCU_FAKE_SERIAL=`printf "FKE%07d" $PIVCCU_FAKE_SERIAL`
  echo "PIVCCU_FAKE_SERIAL=\"$PIVCCU_FAKE_SERIAL\"" >> /etc/default/pivccu3
fi

if [ -z "$PIVCCU_FAKE_RADIO_MAC" ]; then
  PIVCCU_FAKE_RADIO_MAC=`shuf -i 16711680-16777214 -n 1`
  PIVCCU_FAKE_RADIO_MAC=`printf "0x%06x" $PIVCCU_FAKE_RADIO_MAC`
  echo "PIVCCU_FAKE_RADIO_MAC=\"$PIVCCU_FAKE_RADIO_MAC\"" >> /etc/default/pivccu3
fi

HM_RAW_UART_MAJOR=1
HM_RAW_UART_MINOR=3
HM_HMIP_MAJOR=1
HM_HMIP_MINOR=3

case "$PIVCCU_RF_MODE" in
  "Fake")
    modprobe -q fake_hmrf
    if [ -e /sys/module/fake_hmrf ]; then
      HM_RAW_UART_MAJOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f1`
      HM_RAW_UART_MINOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f2`
      echo -n "$PIVCCU_FAKE_SERIAL" > /sys/module/fake_hmrf/parameters/board_serial
      echo -n "$PIVCCU_FAKE_RADIO_MAC" > /sys/module/fake_hmrf/parameters/radio_mac
    fi

    HM_HMIP_DEV="HM-MOD-RPI-PCB"
    HM_HMIP_DEVNODE="/dev/fake_hmrf"
    HM_HMIP_SERIAL="$PIVCCU_FAKE_SERIAL"
    HM_HMIP_VERSION=`grep "^CCU2 " /var/lib/piVCCU3/rootfs/firmware/fwmap | awk -F ' ' '{print $3}'`
    HM_HMIP_SGTIN="3014F711A061A7D5699D"
    HM_HMIP_ADDRESS="$PIVCCU_FAKE_RADIO_MAC"
    HM_HMIP_DEVTYPE="FAKE"
    HM_HMRF_DEV="$HM_HMIP_DEV"
    HM_HMRF_DEVNODE="$HM_HMIP_DEVNODE"
    HM_HMRF_SERIAL="$HM_HMIP_SERIAL"
    HM_HMRF_VERSION="$HM_HMIP_VERSION"
    HM_HMRF_ADDRESS="$PIVCCU_FAKE_RADIO_MAC"
    HM_HMRF_DEVTYPE="$HM_HMIP_DEVTYPE"
    HM_HMIP_MAJOR="$HM_EQ3LOOP_MAJOR"
    HM_HMIP_MINOR=1
    ;;

  *)
    if [ -e /etc/default/hb_rf_eth ]; then
      . /etc/default/hb_rf_eth
    fi

    if [ ! -z "$HB_RF_ETH_ADDRESS" ]; then
      if [ ! -e /sys/module/hb_rf_eth/parameters/connect ]; then
        modprobe -q hb_rf_eth

        /var/lib/piVCCU3/wait_network_up.sh

        for try in {0..30}; do
          if [ -e /sys/module/hb_rf_eth/parameters/connect ]; then
            break
          fi
          sleep 1
        done
      fi

      if [ -e /sys/module/hb_rf_eth/parameters/connect ]; then
        for try in {0..30}; do
          if [ -e /sys/class/hb-rf-eth/hb-rf-eth/connect ]; then
            echo "$HB_RF_ETH_ADDRESS" > /sys/class/hb-rf-eth/hb-rf-eth/connect && break
          else
            echo "$HB_RF_ETH_ADDRESS" > /sys/module/hb_rf_eth/parameters/connect && break
          fi
          sleep 2
        done
      fi
    fi

    for syspath in $(find /sys/bus/usb/devices/); do
      if [ ! -e $syspath/idVendor ]; then
        continue
      fi

      USBID="`cat $syspath/idVendor`:`cat $syspath/idProduct`"

      case "$USBID" in
        "0403:6f70")
          KMOD="hb_rf_usb"
          ;;
        "10c4:8c07" | "1b1f:c020")
          KMOD="hb_rf_usb_2"
          ;;
        *)
          continue
          ;;
      esac

      if [ $(lsmod | grep -w $KMOD | wc -l) -eq 0 ]; then
        modprobe -q $KMOD

        for try in {0..30}; do
          lsmod | grep -q -w $KMOD && RC=$? || RC=$?
          if [ $RC -eq 0 ]; then
            break
          fi
          sleep 1
        done
      fi

      for try in {0..30}; do
        if [ $(find $syspath/ -mindepth 2 -name driver | wc -l) -ne 0 ]; then
          break
        fi
        sleep 1
      done
    done

    for dev_no in {0..5}
    do
      if [ $dev_no -eq 0 ]; then
        UART_DEV="raw-uart"
      else
        UART_DEV="raw-uart$dev_no"
      fi

      if [ -e "/sys/class/raw-uart/$UART_DEV" ]; then
        if [ ! -e "/dev/$UART_DEV" ]; then
          mknod "/dev/$UART_DEV" c `cat /sys/class/raw-uart/$UART_DEV/dev | tr ':' ' '`
        fi

        MODULE_INFO=`detect_radio_module /dev/$UART_DEV` && RC=$? || RC=$?
        if [ $RC -eq 0 ]; then
          DEV_TYPE=`echo $MODULE_INFO | cut -d' ' -f1`
          DEV_SERIAL=`echo $MODULE_INFO | cut -d' ' -f2`

          HM_HMIP_DEV="$DEV_TYPE"
          HM_HMIP_DEVNODE="/dev/$UART_DEV"
          HM_HMIP_SERIAL="$DEV_SERIAL"
          HM_HMIP_VERSION=`echo $MODULE_INFO | cut -d' ' -f6`
          HM_HMIP_SGTIN=`echo $MODULE_INFO | cut -d' ' -f3`
          HM_HMIP_ADDRESS=`echo $MODULE_INFO | cut -d' ' -f5`
          if [ -e "/sys/class/raw-uart/$UART_DEV/device_type" ]; then
            HM_HMIP_DEVTYPE=`cat /sys/class/raw-uart/$UART_DEV/device_type`
          fi

          if [ "$DEV_TYPE" == "HMIP-RFUSB-TK" ]; then
            HM_HMRF_DEV="HM-MOD-RPI-PCB"
            HM_HMRF_DEVNODE="/dev/fake_hmrf"
            HM_HMRF_SERIAL="$PIVCCU_FAKE_SERIAL"
            HM_HMRF_VERSION=`grep "^CCU2 " /var/lib/piVCCU3/rootfs/firmware/fwmap | awk -F ' ' '{print $3}'`
            HM_HMRF_ADDRESS="$PIVCCU_FAKE_RADIO_MAC"
            HM_HMRF_DEVTYPE="FAKE"

            modprobe -q fake_hmrf
            if [ -e /sys/module/fake_hmrf ]; then
              echo -n "$PIVCCU_FAKE_SERIAL" > /sys/module/fake_hmrf/parameters/board_serial
              echo -n "$PIVCCU_FAKE_RADIO_MAC" > /sys/module/fake_hmrf/parameters/radio_mac
              HM_RAW_UART_MAJOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f1`
              HM_RAW_UART_MINOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f2`
            fi
            HM_HMIP_MAJOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f1`
            HM_HMIP_MINOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f2`
          else
            HM_HMRF_DEV="$HM_HMIP_DEV"
            HM_HMRF_DEVNODE="$HM_HMIP_DEVNODE"
            HM_HMRF_SERIAL="$HM_HMIP_SERIAL"
            HM_HMRF_VERSION="$HM_HMIP_VERSION"
            HM_HMRF_ADDRESS=`echo $MODULE_INFO | cut -d' ' -f4`
            HM_HMRF_DEVTYPE="$HM_HMIP_DEVTYPE"

            HM_RAW_UART_MAJOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f1`
            HM_RAW_UART_MINOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f2`
            HM_HMIP_MAJOR="$HM_EQ3LOOP_MAJOR"
            HM_HMIP_MINOR=1
          fi

          break
        fi
      fi
    done
    ;;
esac

BRIDGE=`brctl show | grep -v "lxcbr0" | grep -v "^\s" | sed -n 2p | awk '{print $1}'`
MAIN_INTERFACE=`route | grep 'default' | awk '{print $5" "$8}' | sort | awk '{print $2}' | uniq | head -n 1`
HOST_MAC=`cat /sys/class/net/$MAIN_INTERFACE/address`
MAC=`echo $HOST_MAC | md5sum | sed 's/\(.\)\(..\)\(..\)\(..\)\(..\)\(..\).*/\1a:\2:\3:\4:\5:\6/'`

