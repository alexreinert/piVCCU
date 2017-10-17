#!/bin/sh

if [ ! -f /var/new_firmware.tar.gz ]; then
  exit
fi

cd /var

tar xzf new_firmware.tar.gz

if [ ! -x /var/update_script ]; then
  exit
fi

sed -i update_script -e "s/mount /: # mount/"

./update_script CCU2

