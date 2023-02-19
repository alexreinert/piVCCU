#!/bin/bash
echo -n "Waiting for CCU startup "
sleep 5
while true; do
  if [ -e /tmp/pivccu-var/status/startupFinished ] && [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --state --no-humanize` == "RUNNING" ]; then
    echo " Done."
    break
  fi
  echo -n "."
  sleep 1
done
