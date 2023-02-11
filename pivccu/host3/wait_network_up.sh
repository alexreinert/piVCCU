#!/bin/bash
INTERFACES=`ifquery --list --exclude lo --allow auto | xargs`

if [ -n "$INTERFACES" ]; then
  exit 0
fi

while ! ifquery --state $INTERFACES >/dev/null; do
  sleep 1
done

for i in $INTERFACES; do
  while [ -e /run/network/ifup-$i.pid ]; do
    sleep 0.2
  done
done

