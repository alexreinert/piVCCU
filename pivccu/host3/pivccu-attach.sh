#!/bin/bash
if [ $EUID != 0 ]; then
  echo "Please run as root"
  exit 1
fi

if [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --state --no-humanize` == "STOPPED" ]; then
  echo "piVCCU is not running, cannot attach."
  exit 1 
fi

if [ $# -eq 0 ]; then
  /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3 --name lxc
else
  /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3 --name lxc -- $@
fi

