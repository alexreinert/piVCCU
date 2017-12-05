#!/bin/bash
if [ ! -e /dev/raw-uart ]; then
  dpkg-reconfigure pivccu-modules-dkms
  modprobe dw_apb_raw_uart || true
  modprobe pl011_raw_uart || true
fi
