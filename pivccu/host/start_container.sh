#!/bin/bash

. /etc/default/pivccu

modprobe -a plat_eq3ccu2 eq3_char_loop &> /dev/null
if [ $? -ne 0 ]; then
  logger -t piVCCU -p user.err -s "Could not load kernel modules plat_eq3ccu2 and eq3_char_loop." 1>&2
  exit 1
fi

case $PIVCCU_HMRF_MODE in
  "Fake-HmRF")
    UART_MODULE="fake-hmrf"
    modprobe -a fake_hmrf &> /dev/null

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

    echo -ne "$PIVCCU_FAKE_SERIAL" > /sys/module/fake_hmrf/parameters/board_serial
    echo -ne "$PIVCCU_FAKE_RADIO_MAC" > /sys/module/fake_hmrf/parameters/radio_mac
    grep "^CCU2 " /var/lib/piVCCU/rootfs/firmware/fwmap | awk -F ' ' '{print $3}' > /sys/module/fake_hmrf/parameters/firmware_version
    ;;

  "HM-MOD-RPI-PCB"|"")
    # test if hardware uart of Raspberry Pi 3 is assigned to gpio pins
    if [ -f /proc/device-tree/model ] && [ `grep -c "Raspberry Pi 3" /proc/device-tree/model` == 1 ]; then
      if ! cmp -s /proc/device-tree/aliases/uart0 /proc/device-tree/aliases/serial0; then
        logger -t piVCCU -p user.err -s "Hardware UART is not assigned to GPIO pins." 1>&2
        exit 1
      fi
    fi

    UART_MODULE="raw-uart"
    ;;

  *)
    logger -t piVCCU -p user.err -s "Unsupported HmRF mode." 1>&2
    exit 1
    ;;
esac

if [ ! -e /sys/devices/virtual/$UART_MODULE ]; then
  logger -t piVCCU -p user.err -s "Could not locate $UART_MODULE interface. Are the kernel modules and the device tree overlays installed?" 1>&2
  exit 1
fi

EQ3LOOP_MAJOR=`cat /sys/devices/virtual/eq3loop/eq3loop/dev | cut -d: -f1`
UART_MAJOR=`cat /sys/devices/virtual/$UART_MODULE/$UART_MODULE/dev | cut -d: -f1`

mount --bind /dev /var/lib/piVCCU/rootfs/dev
BOARD_SERIAL=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd update-coprocessor -p /dev/$UART_MODULE -t HM-MOD-UART -c -se 2>&1 | grep "SerialNumber:" | cut -d' ' -f5`
RADIO_MAC=`chroot /var/lib/piVCCU/rootfs /bin/eq3configcmd read-default-rf-address -f /dev/$UART_MODULE -h | grep "^0x"`
umount /var/lib/piVCCU/rootfs/dev

if [ -z "$BOARD_SERIAL" ]; then
  logger -t piVCCU -p user.warning -s "HM-MOD-RPI-PCB was not detected." 1>&2
fi

case $PIVCCU_HMIP_MODE in
  "Multimacd"|"")
    HMIP_MAJOR=$EQ3LOOP_MAJOR
    HMIP_MINOR=1
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
          break
        fi
      fi
    done

    if [ -z "$HMIP_MAJOR" ]; then
      HMIP_MAJOR=1
      HMIP_MINOR=3
      logger -t piVCCU -p user.warning -s "HmIP-RFUSB stick was not detected." 1>&2
    fi
    ;;

  *)
    logger -t piVCCU -p user.err -s "Unsupported HmIP mode." 1>&2
    exit 1
    ;;
esac

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
sed -i $CONFIG_FILE -e "s/<hmip_major>/$HMIP_MAJOR/"
sed -i $CONFIG_FILE -e "s/<hmip_minor>/$HMIP_MINOR/"

echo -n $EQ3LOOP_MAJOR > /sys/module/plat_eq3ccu2/parameters/eq3charloop_major
echo -n $UART_MAJOR > /sys/module/plat_eq3ccu2/parameters/uart_major
echo -n $HMIP_MAJOR > /sys/module/plat_eq3ccu2/parameters/hmip_major
echo -n $HMIP_MINOR > /sys/module/plat_eq3ccu2/parameters/hmip_minor
echo -n $BOARD_SERIAL > /sys/module/plat_eq3ccu2/parameters/board_serial
echo -n $RADIO_MAC > /sys/module/plat_eq3ccu2/parameters/radio_mac

if [ -x /etc/piVCCU/pre-start.sh ]; then
  /etc/piVCCU/pre-start.sh
fi

sysctl -w kernel.sched_rt_runtime_us=-1

/usr/bin/lxc-start --lxcpath /var/lib/piVCCU --name lxc --pidfile /var/run/pivccu.pid --daemon

if [ -x /etc/piVCCU/post-start.sh ]; then
  /etc/piVCCU/post-start.sh
fi

