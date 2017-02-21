#!/bin/bash
modinfo generic_raw_uart &> /dev/null
if [ ! $? -eq 0 ]; then
  PKG_VER=`dpkg -s pivccu-modules-dkms | grep '^Version: ' | cut -d' ' -f2`

  if [ -e /usr/src/linux-headers-`uname -r` ]; then
    cd /usr/src/linux-headers-`uname -r`

    if [ `grep -c "^source \"net/wireguard/Kconfig\"" net/Kconfig` -gt 0 ]; then
      mkdir -p net/wireguard
      touch net/wireguard/Kconfig
      touch net/wireguard/Makefile
    fi

    make scripts
  fi

  dkms install -m pivccu -v $PKG_VER -k `uname -r` || true

  for i in {1..120}; do
    udevadm settle -t 5 -E /dev/raw-uart && udevadm trigger && udevadm settle -t 5 -E /dev/raw-uart
    if [ -e /dev/raw-uart ]; then
      break
    fi
  done
fi

