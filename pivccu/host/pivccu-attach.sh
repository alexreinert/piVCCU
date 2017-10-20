#!/bin/bash
if [ $# -eq 0 ]; then
  /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU --name lxc
else
  /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU --name lxc -- $@
fi

