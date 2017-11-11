#!/bin/bash

if [ -e /etc/piVCCU/pre-stop.sh ]; then
  /etc/piVCCU/pre-stop.sh
fi

# stop container
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU/ --name lxc -- /etc/piVCCU/save-rega.tcl || true
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU/ --name lxc -- poweroff || true
/usr/bin/lxc-wait --lxcpath /var/lib/piVCCU/ --name lxc --state STOPPED --timeout 300 || /usr/bin/lxc-stop --lxcpath /var/lib/piVCCU/ --name lxc

if [ -e /etc/piVCCU/post-stop.sh ]; then
  /etc/piVCCU/post-stop.sh
fi

# unload kernel modules
rmmod plat_eq3ccu2 || true
rmmod eq3_char_loop || true
