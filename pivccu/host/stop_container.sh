#!/bin/bash

# stop container
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU/ --name lxc -- /etc/piVCCU/save-rega.tcl || true
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU/ --name lxc -- poweroff || true
/usr/bin/lxc-wait --lxcpath /var/lib/piVCCU/ --name lxc --state STOPPED --timeout 300 || /usr/bin/lxc-stop --lxcpath /var/lib/piVCCU/ --name lxc

# reset homematic board
if [ -d /sys/class/gpio/gpio18 ]
then
  echo 0 > /sys/class/gpio/gpio18/value
  sleep 0.1
  echo 1 > /sys/class/gpio/gpio18/value

  echo 18 > /sys/class/gpio/unexport
fi

# unload kernel modules
rmmod eq3_char_loop bcm2835_raw_uart || true

