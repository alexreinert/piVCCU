#!/bin/bash
modinfo generic_raw_uart &> /dev/null
if [ ! $? -eq 0 ]; then
  dpkg-reconfigure pivccu-modules-dkms
  udevadm trigger || true
fi
