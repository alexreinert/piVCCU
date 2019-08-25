#!/bin/bash

. /etc/default/pivccu

HMRF_HARDWARE=''
HMIP_HARDWARE=''

case $PIVCCU_HMRF_MODE in
  "Fake-HmRF")
    HMRF_HARDWARE='FAKE_HMRF'

    if [ -z "$PIVCCU_FAKE_SERIAL" ]; then
      PIVCCU_FAKE_SERIAL=`shuf -i 1-9999999 -n 1`
      PIVCCU_FAKE_SERIAL=`printf "FKE%07d" $PIVCCU_FAKE_SERIAL`
      echo "PIVCCU_FAKE_SERIAL=\"$PIVCCU_FAKE_SERIAL\"" >> /etc/default/pivccu
    fi

    if [ -z "$PIVCCU_FAKE_RADIO_MAC" ]; then
      PIVCCU_FAKE_RADIO_MAC=`shuf -i 1-16777215 -n 1`
      PIVCCU_FAKE_RADIO_MAC=`printf "0x%06x" $PIVCCU_FAKE_RADIO_MAC`
      echo "PIVCCU_FAKE_RADIO_MAC=\"$PIVCCU_FAKE_RADIO_MAC\"" >> /etc/default/pivccu
    fi

    modprobe -a fake_hmrf &> /dev/null
    if [ -e /sys/module/fake_hmrf ]; then
      UART_MAJOR=`cat /sys/devices/virtual/fake-hmrf/fake-hmrf/dev | cut -d: -f1`
      echo -n "$PIVCCU_FAKE_SERIAL" > /sys/module/fake_hmrf/parameters/board_serial
      echo -n "$PIVCCU_FAKE_RADIO_MAC" > /sys/module/fake_hmrf/parameters/radio_mac
    fi

    BOARD_SERIAL=$PIVCCU_FAKE_SERIAL
    RADIO_MAC=$PIVCCU_FAKE_RADIO_MAC

    mount --bind /dev /var/lib/piVCCU/rootfs/dev
    mount --bind /proc /var/lib/piVCCU/rootfs/proc
    SGTIN=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/fake-hmrf -t HM-MOD-UART -c -sg 2>&1 | grep "SGTIN:" | cut -d' ' -f5`

    umount /var/lib/piVCCU/rootfs/dev
    umount /var/lib/piVCCU/rootfs/proc

    grep "^CCU2 " /var/lib/piVCCU/rootfs/firmware/fwmap | awk -F ' ' '{print $3}' > /sys/module/fake_hmrf/parameters/firmware_version
    ;;

  "HM-MOD-RPI-PCB"|"RPI-RF-MOD"|"")
    if [ -e /dev/raw-uart ]; then
      UART_MAJOR=`cat /sys/devices/virtual/raw-uart/raw-uart/dev | cut -d: -f1`

      mount --bind /dev /var/lib/piVCCU/rootfs/dev
      mount --bind /proc /var/lib/piVCCU/rootfs/proc

      RADIO_MAC=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd read-default-rf-address -f /dev/raw-uart -h 2>&1 | grep "^0x"` || true

      if [ -z "$RADIO_MAC" ] || [ "$RADIO_MAC" == "0x00ffff" ]; then
        RF_RPI_MOD_INFO=`timeout 20 chroot /var/lib/piVCCU/rootfs /opt/jre/bin/java -Dgnu.io.rxtx.SerialPorts=/dev/raw-uart -jar /opt/HmIP/hmip-copro-update.jar -p /dev/raw-uart -v -a 2>&1` || true
        RADIO_MAC=`echo "$RF_RPI_MOD_INFO" | sed -n 's/.*Radio address = \([0-9A-Fa-f]\{6\}\).*/0x\1/p'`
        SGTIN=`echo "$RF_RPI_MOD_INFO" | sed -n 's/.*SGTIN = \([0-9A-Fa-f]\{24\}\).*/\1/p'`

        if [ ! -z "$RADIO_MAC" ] || [ ! -z "$SGTIN" ]; then
          HMRF_HARDWARE='RPI-RF-MOD'
          BOARD_SERIAL=`echo -n "$SGTIN" | tail -c 10`
        fi
      else
        HMRF_HARDWARE='HM-MOD-RPI-PCB'
        SGTIN=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/raw-uart -t HM-MOD-UART -c -sg 2>&1 | grep "SGTIN:" | cut -d' ' -f5`
        BOARD_SERIAL=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/raw-uart -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5`
      fi

      umount /var/lib/piVCCU/rootfs/dev
      umount /var/lib/piVCCU/rootfs/proc
    fi
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
    modprobe -a cp210x &> /dev/null

    if [ `grep -c "1b1f c020" /sys/bus/usb-serial/drivers/cp210x/new_id` -eq 0 ]; then
      echo "1b1f c020" > /sys/bus/usb-serial/drivers/cp210x/new_id
    fi

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

      mount --bind /dev /var/lib/piVCCU/rootfs/dev
      mount --bind /proc /var/lib/piVCCU/rootfs/proc

      HMIP_INFO=`timeout 20 chroot /var/lib/piVCCU/rootfs /opt/jre/bin/java -Dgnu.io.rxtx.SerialPorts=$HMIP_DEVNAME -jar /opt/HmIP/hmip-copro-update.jar -p $HMIP_DEVNAME -v -a 2>&1` || true
      SGTIN=`echo "$HMIP_INFO" | sed -n 's/.*SGTIN = \([0-9A-Fa-f]\{24\}\).*/\1/p'`

      umount /var/lib/piVCCU/rootfs/dev
      umount /var/lib/piVCCU/rootfs/proc
    fi
    ;;
esac

BRIDGE=`brctl show | sed -n 2p | awk '{print $1}'`
MAIN_INTERFACE=`route | grep 'default' | awk '{print $5" "$8}' | sort | awk '{print $2}' | uniq | head -n 1`
HOST_MAC=`cat /sys/class/net/$MAIN_INTERFACE/address`
MAC=`echo $HOST_MAC | md5sum | sed 's/\(.\)\(..\)\(..\)\(..\)\(..\)\(..\).*/\1a:\2:\3:\4:\5:\6/'`

