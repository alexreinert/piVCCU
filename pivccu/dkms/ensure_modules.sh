#!/bin/bash
function throw {
  echo $1
  exit 1
}

function run {
  echo -n "$1 ... "
  shift
  ERR=`$* 2>&1` && RC=$? || RC=$?
  if [ $RC -eq 0 ]; then
    echo -e "\033[0;32mDone\033[0;0m"
  else
    echo -e "\033[1;91mFAILED\033[0;0m"
    echo "$ERR"
    exit 1
  fi
}

KERNEL_VER=`uname -r`

function check_for_headers {
  if [ ! -e /usr/src/linux-headers-$KERNEL_VER ]; then
    throw "No headers found for current active kernel $KERNEL_VER"
  fi
}

function prepare_headers {
  cd /usr/src/linux-headers-$KERNEL_VER
  if [ ! -x scripts/recordmcount ]; then
    if [ `grep -c "^source \"net/wireguard/Kconfig\"" net/Kconfig` -gt 0 ]; then
      mkdir -p net/wireguard
      touch net/wireguard/Kconfig
      touch net/wireguard/Makefile
    fi

    make scripts

    if [ ! -x scripts/recordmcount ]; then
      make EXTRAVERSION="-`uname -r | cut -d- -f2-`" prepare0
      cd /usr/src/linux-headers-$KERNEL_VER/scripts
      make recordmcount
    fi
  fi
}

function reload_udev {
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
}

modinfo generic_raw_uart &> /dev/null && RC=$? || RC=$?
if [ ! $RC -eq 0 ]; then
  PKG_VER=`dpkg -s pivccu-modules-dkms | grep '^Version: ' | cut -d' ' -f2`

  DKMS_STATUS=`dkms status -m pivccu -v $PKG_VER -k $KERNEL_VER`
  if [ ! -z "$DKMS_STATUS" ]; then
    run "Remove DKMS package" dkms remove -m pivccu -v $PKG_VER -k `uname -r`
  fi

  run "Check kernel headers" check_for_headers

  run "Prepare kernel headers" prepare_headers

  run "Install DKMS package" dkms install -m pivccu -v $PKG_VER -k $KERNEL_VER

  run "Try to load fresh build modules" modprobe generic_raw_uart

  run "Reload udev" reload_udev
fi

