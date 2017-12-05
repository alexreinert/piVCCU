#!/bin/bash
modinfo generic_raw_uart &> /dev/null
if [ ! $? -eq 0 ]; then
  dpkg-reconfigure pivccu-modules-dkms
  modprobe dw_apb_raw_uart || true
  modprobe pl011_raw_uart || true
fi
