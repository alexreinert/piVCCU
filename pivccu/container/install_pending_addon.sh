#!/bin/sh

if [ ! -f /var/new_firmware.tar.gz ]; then
  exit
fi

cd /var

tar xzf new_firmware.tar.gz

if [ ! -x /var/update_script ]; then
  exit
fi

sed -i update_script -e "s/mount -t ubifs ubi1:user \(.*\)/mount --bind \/usr\/local \1/"
sed -i update_script -e "s/\(mount --bind \/usr\/local \/usr\/local.*\)/: # \1/"
sed -i update_script -e "s/\(mount \/usr\/local.*\)/: # \1/"

./update_script CCU2

