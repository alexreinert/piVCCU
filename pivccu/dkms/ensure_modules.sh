#!/bin/bash
modinfo generic_raw_uart &> /dev/null
if [ ! $? -eq 0 ]; then
  PKG_VER=`dpkg -s pivccu-modules-dkms | grep '^Version: ' | cut -d' ' -f2`

  if [ -e /usr/src/linux-headers-`uname -r` ]; then
    cd /usr/src/linux-headers-`uname -r`

    if [ -e scripts/basic/fixdep ]; then
      scripts/basic/fixdep >/dev/null 2>&1
      if [ $? -eq 126 ]; then
        make scripts || true
      fi
    fi

    if [ ! -x scripts/recordmcount ]; then
      if [ `grep -c "^source \"net/wireguard/Kconfig\"" net/Kconfig` -gt 0 ]; then
        mkdir -p net/wireguard
        touch net/wireguard/Kconfig
        touch net/wireguard/Makefile
      fi

      make scripts

      if [ ! -x scripts/recordmcount ]; then
        make EXTRAVERSION="-`uname -r | cut -d- -f2-`" prepare0
	cd /usr/src/linux-headers-`uname -r`/scripts
	make recordmcount
      fi
    fi
  fi

  if [[ ! "$(dkms status -m pivccu -v $PKG_VER -k `uname -r`)" =~ "installed" ]]; then
    dkms install -m pivccu -v $PKG_VER -k `uname -r` || true
  else
    if [ "$(uname -s)" == "Linux" ] ; then
      if [[ -f /boot/System.map-`uname -r` ]]; then
        depmod -a "$(uname -r)" -F "/boot/System.map-$(uname -r)"
      else
        depmod -a "$(uname -r)"
      fi
    fi
  fi

  modinfo generic_raw_uart &> /dev/null
  if [ $? -eq 0 ]; then
    if [ -e /etc/default/hb_rf_eth ]; then
      . /etc/default/hb_rf_eth
    fi
    if [ -z "$HB_RF_ETH_ADDRESS" ]; then
      for i in {1..12}; do
        if [ -e /dev/raw-uart ]; then
          break
        fi
        udevadm settle -t 5 -E /dev/raw-uart || true
        udevadm trigger -c add || true
        udevadm trigger || true
        sleep 5
      done
    fi
  fi
fi

