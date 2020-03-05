#!/bin/bash

. /etc/default/pivccu3

HMRF_HARDWARE=''
HMIP_HARDWARE=''

case $PIVCCU_HMRF_MODE in
  "Fake-HmRF")
    HMRF_HARDWARE='FAKE_HMRF'

    if [ -z "$PIVCCU_FAKE_SERIAL" ]; then
      PIVCCU_FAKE_SERIAL=`shuf -i 1-9999999 -n 1`
      PIVCCU_FAKE_SERIAL=`printf "FKE%07d" $PIVCCU_FAKE_SERIAL`
      echo "PIVCCU_FAKE_SERIAL=\"$PIVCCU_FAKE_SERIAL\"" >> /etc/default/pivccu3
    fi

    if [ -z "$PIVCCU_FAKE_RADIO_MAC" ]; then
      PIVCCU_FAKE_RADIO_MAC=`shuf -i 1-16777215 -n 1`
      PIVCCU_FAKE_RADIO_MAC=`printf "0x%06x" $PIVCCU_FAKE_RADIO_MAC`
      echo "PIVCCU_FAKE_RADIO_MAC=\"$PIVCCU_FAKE_RADIO_MAC\"" >> /etc/default/pivccu3
    fi

    modprobe -a fake_hmrf &> /dev/null
    if [ -e /sys/module/fake_hmrf ]; then
      UART_MAJOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f1`
      UART_MINOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f2`
      echo -n "$PIVCCU_FAKE_SERIAL" > /sys/module/fake_hmrf/parameters/board_serial
      echo -n "$PIVCCU_FAKE_RADIO_MAC" > /sys/module/fake_hmrf/parameters/radio_mac
    fi

    BOARD_SERIAL=$PIVCCU_FAKE_SERIAL
    RADIO_MAC=$PIVCCU_FAKE_RADIO_MAC

    mount --bind /dev /var/lib/piVCCU3/rootfs/dev
    mount --bind /proc /var/lib/piVCCU3/rootfs/proc
    SGTIN=`chroot /var/lib/piVCCU3/rootfs /bin/eq3configcmd update-coprocessor -p /dev/fake-hmrf -t HM-MOD-UART -c -sg 2>&1 | grep "SGTIN:" | cut -d' ' -f5`

    umount /var/lib/piVCCU3/rootfs/dev
    umount /var/lib/piVCCU3/rootfs/proc

    grep "^CCU2 " /var/lib/piVCCU3/rootfs/firmware/fwmap | awk -F ' ' '{print $3}' > /sys/module/fake_hmrf/parameters/firmware_version
    FW_VERSION=`cat /sys/module/fake_hmrf/parameters/firmware_version`
    ;;

  "HM-MOD-RPI-PCB"|"RPI-RF-MOD"|"")
    for syspath in $(find /sys/bus/usb/devices/); do
      if [ -e $syspath/idVendor ] && [ "`cat $syspath/idVendor`" == "0403" ] && [ "`cat $syspath/idProduct`" == "6f70" ]; then
        if [ $(lsmod | grep hb_rf_usb | wc -l) -eq 0 ]; then
          modprobe -q hb_rf_usb

          for try in {0..30}; do
            lsmod | grep -q hb_rf_usb
            if [ $? -eq 0 ]; then
              break
            fi
            sleep 1
          done
        fi

        for try in {0..30}; do
          if [ $(find $syspath/ -name gpiochip* | wc -l) -ne 0 ]; then
            break
          fi
          sleep 1
        done
      fi
    done

    mount --bind /dev /var/lib/piVCCU3/rootfs/dev
    mount --bind /proc /var/lib/piVCCU3/rootfs/proc

    for dev_no in {0..5}
    do
      if [ $dev_no -eq 0 ]; then
        UART_DEV="raw-uart"
      else
        UART_DEV="raw-uart$dev_no"
      fi

      if [ -e "/dev/$UART_DEV" ]; then
        echo 1 > /sys/class/raw-uart/$UART_DEV/reset_radio_module

        UART_MAJOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f1`
        UART_MINOR=`cat /sys/class/raw-uart/$UART_DEV/dev | cut -d: -f2`

        RADIO_MAC=`chroot /var/lib/piVCCU3/rootfs /bin/eq3configcmd read-default-rf-address -f /dev/$UART_DEV -h 2>&1 | grep "^0x"` || true
        if [ -z "$RADIO_MAC" ] || [ "$RADIO_MAC" == "0x00ffff" ] || [ "$RADIO_MAC" == "0xa9ffff" ]; then
          RF_RPI_MOD_INFO=`timeout 20 chroot /var/lib/piVCCU3/rootfs /opt/java/bin/java -Dgnu.io.rxtx.SerialPorts=/dev/$UART_DEV -jar /opt/HmIP/hmip-copro-update.jar -p /dev/$UART_DEV -v 2>&1` || true
          RADIO_MAC=`echo "$RF_RPI_MOD_INFO" | sed -n 's/.*Radio address = \([0-9A-Fa-f]\{6\}\).*/0x\1/p'`
          SGTIN=`echo "$RF_RPI_MOD_INFO" | sed -n 's/.*SGTIN = \([0-9A-Fa-f]\{24\}\).*/\1/p'`
          FW_VERSION=`echo "$RF_RPI_MOD_INFO" | sed -n 's/.*Application version = \([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p'`

          if [ ! -z "$RADIO_MAC" ] || [ ! -z "$SGTIN" ] ; then
            HMRF_HARDWARE='RPI-RF-MOD'
            BOARD_SERIAL=`echo -n "$SGTIN" | tail -c 10`
          fi
        else
          HMRF_HARDWARE='HM-MOD-RPI-PCB'
          SGTIN=`chroot /var/lib/piVCCU3/rootfs /bin/eq3configcmd update-coprocessor -p /dev/$UART_DEV -t HM-MOD-UART -c -sg 2>&1 | grep "SGTIN:" | cut -d' ' -f5`
          BOARD_SERIAL=`chroot /var/lib/piVCCU3/rootfs /bin/eq3configcmd update-coprocessor -p /dev/$UART_DEV -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5`
          FW_VERSION=`chroot /var/lib/piVCCU3/rootfs /bin/eq3configcmd update-coprocessor -p /dev/$UART_DEV -t HM-MOD-UART -c -v 2>&1 | grep "Version:" | cut -d' ' -f5`
        fi

        if [ ! -z "$HMRF_HARDWARE" ]; then
          break
        fi
      fi
    done

    umount /var/lib/piVCCU3/rootfs/dev
    umount /var/lib/piVCCU3/rootfs/proc
    ;;
esac

if [ -e /sys/devices/virtual/eq3loop ]; then
  EQ3LOOP_MAJOR=`cat /sys/devices/virtual/eq3loop/eq3loop/dev | cut -d: -f1`
fi

case $PIVCCU_HMIP_MODE in
  "Multimacd"|"")
    HMIP_MAJOR=$EQ3LOOP_MAJOR
    HMIP_MINOR=1
    HMIP_HARDWARE=$HMRF_HARDWARE
    ;;

  "HmIP-RFUSB")
    if [ ! -e /sys/bus/usb-serial/drivers/cp210x ]; then
      modprobe -a cp210x &> /dev/null

      for try in {0..30}; do
        if [ -e /sys/bus/usb-serial/drivers/cp210x ]; then
          break
        fi
        sleep 1
      done
    fi

    if [ `grep -c "1b1f c020" /sys/bus/usb-serial/drivers/cp210x/new_id` -eq 0 ]; then
      echo "1b1f c020" > /sys/bus/usb-serial/drivers/cp210x/new_id
    fi

    for try in {0..30}; do
      if [ $(find $syspath/ -name ttyUSB* | wc -l) -ne 0 ]; then
        break
      fi
      sleep 1
    done

    for syspath in $(find /sys/bus/usb/devices/usb*/ -name ttyUSB*); do
      if [ -e $syspath/dev ]; then
        eval "$(udevadm info -q property --export -p $syspath)"
        if [ "$ID_VENDOR_ID $ID_MODEL_ID" == "1b1f c020" ]; then
          HMIP_MAJOR=$MAJOR
          HMIP_MINOR=$MINOR
          HMIP_DEVNAME=$DEVNAME
          break
        fi
      fi
    done

    if [ -z "$HMIP_MAJOR" ]; then
      HMIP_MAJOR=1
      HMIP_MINOR=3
    else
      HMIP_HARDWARE='HMIP-RFUSB'

      mount --bind /dev /var/lib/piVCCU3/rootfs/dev
      mount --bind /proc /var/lib/piVCCU3/rootfs/proc

      HMIP_INFO=`timeout 20 chroot /var/lib/piVCCU3/rootfs /opt/java/bin/java -Dgnu.io.rxtx.SerialPorts=$HMIP_DEVNAME -jar /opt/HmIP/hmip-copro-update.jar -p $HMIP_DEVNAME -v 2>&1` || true
      SGTIN=`echo "$HMIP_INFO" | sed -n 's/.*SGTIN = \([0-9A-Fa-f]\{24\}\).*/\1/p'`
      FW_VERSION=`echo "$HMIP_INFO" | sed -n 's/.*Application version = \([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p'`

      if [[ "$SGTIN" =~ "3014f5ac94" ]]; then
       HMIP_HARDWARE='HMIP-RFUSB-TK'
      fi

      umount /var/lib/piVCCU3/rootfs/dev
      umount /var/lib/piVCCU3/rootfs/proc
    fi
    ;;
esac

BRIDGE=`brctl show | sed -n 2p | awk '{print $1}'`
MAIN_INTERFACE=`route | grep 'default' | awk '{print $5" "$8}' | sort | awk '{print $2}' | uniq | head -n 1`
HOST_MAC=`cat /sys/class/net/$MAIN_INTERFACE/address`
MAC=`echo $HOST_MAC | md5sum | sed 's/\(.\)\(..\)\(..\)\(..\)\(..\)\(..\).*/\1a:\2:\3:\4:\5:\6/'`

