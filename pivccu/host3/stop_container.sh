#!/bin/bash

if [ -e /etc/piVCCU3/pre-stop.sh ]; then
  /etc/piVCCU3/pre-stop.sh
fi

# stop container
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/save-rega.tcl || true
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- poweroff || true
/usr/bin/lxc-wait --lxcpath /var/lib/piVCCU3/ --name lxc --state STOPPED --timeout 300 || /usr/bin/lxc-stop --lxcpath /var/lib/piVCCU3/ --name lxc

if [ -e /etc/piVCCU3/post-stop.sh ]; then
  /etc/piVCCU3/post-stop.sh
fi

rm -rf /tmp/pivccu-media
rm -rf /tmp/pivccu-var

# unload kernel modules
rmmod eq3_char_loop || true
rmmod fake_hmrf || true

