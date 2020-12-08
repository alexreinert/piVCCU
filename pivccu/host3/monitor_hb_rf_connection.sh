#!/bin/bash

echo -n "Waiting for CCU startup "
while true; do
  if [ -e /tmp/pivccu-var/status/startupFinished ] && [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --state --no-humanize` == "RUNNING" ]; then
    echo " Done."
    break
  fi
  echo -n "."
  sleep 1
done

/usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/wait_sysvar_creation.tcl || true

STATE=`cat /sys/class/hb-rf-eth/hb-rf-eth/is_connected`

while true; do
  if [ "$STATE" -eq "1" ]; then
    echo "HB-RF-ETH is (re-)connected"
    /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/set_hb_rf_eth_connection_dp.tcl false || true
  else
    echo "HB-RF-ETH is not connected anymore"
    /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/set_hb_rf_eth_connection_dp.tcl true || true
  fi

  STATE=`wait_sysfs_notify /sys/class/hb-rf-eth/hb-rf-eth/is_connected`
  if [ $? != 0 ]; then
    exit
  fi
done

