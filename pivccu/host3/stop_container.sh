#!/bin/bash

# lxc-attach passes the caller's environment (including PATH) to processes
# started inside the container. On Debian Trixie (usrmerge) systemd's default
# service PATH lacks /bin and /sbin, but the CCU rootfs is not usrmerged:
# the busybox poweroff applet only exists as /sbin/poweroff. A bare "poweroff"
# is therefore not found inside the container, the container never shuts
# down, lxc-wait burns its 300s timeout and stopping the service (or
# rebooting the host) hangs for ~10 minutes. See also the same PATH fix in
# container3/start-hook.sh.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

if [ -e /etc/piVCCU3/pre-stop.sh ]; then
  /etc/piVCCU3/pre-stop.sh
fi

# stop container
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/save-rega.tcl || true
/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /sbin/poweroff || true
/usr/bin/lxc-wait --lxcpath /var/lib/piVCCU3/ --name lxc --state STOPPED --timeout 300 || /usr/bin/lxc-stop --lxcpath /var/lib/piVCCU3/ --name lxc

if [ -e /etc/piVCCU3/post-stop.sh ]; then
  /etc/piVCCU3/post-stop.sh
fi

rm -rf /tmp/pivccu-media
rm -rf /tmp/pivccu-var

# unload kernel modules
rmmod eq3_char_loop || true
rmmod fake_hmrf || true
rmmod dummy_rx8130 || true
rmmod rpi_rf_mod_led || true
rmmod hb_rf_eth || true

