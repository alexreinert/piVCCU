#!/bin/bash

set -e

if [ $EUID != 0 ]; then
  echo "Please run as root"
  exit
fi

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
  echo "pivccu-backup <backupdir>"
  exit 1
fi

CURDIR=`pwd`

BACKUPPATH="$1/pivccu3_`dpkg -s pivccu3 | grep '^Version: ' | cut -d' ' -f2`_`date '+%Y-%m-%d_%H-%M-%S'`.sbk"
BACKUPPATH=`realpath $BACKUPPATH`

if [ `/usr/bin/lxc-info --lxcpath /var/lib/piVCCU3/ --name lxc --state --no-humanize` != "STOPPED" ]; then
  /usr/bin/lxc-attach --lxcpath /var/lib/piVCCU3/ --name lxc -- /etc/piVCCU3/save-rega.tcl
fi

TMPDIR=`mktemp -d`

mkdir -p $TMPDIR/root
mount --bind /var/lib/piVCCU3/rootfs $TMPDIR/root
mount --bind /var/lib/piVCCU3/userfs $TMPDIR/root/usr/local

cd $TMPDIR/root
tar czf $TMPDIR/usr_local.tar.gz usr/local

chroot $TMPDIR/root crypttool -s -t 1 < $TMPDIR/usr_local.tar.gz > $TMPDIR/signature
chroot $TMPDIR/root crypttool -g -t 1 > $TMPDIR/key_index
cp $TMPDIR/root/boot/VERSION $TMPDIR/firmware_version

cd $TMPDIR
umount $TMPDIR/root/usr/local
umount $TMPDIR/root

tar cf $BACKUPPATH usr_local.tar.gz signature firmware_version key_index

cd $CURDIR
rm -rf $TMPDIR

echo "Backup written to $BACKUPPATH"
