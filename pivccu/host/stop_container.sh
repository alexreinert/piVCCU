#!/bin/bash
/usr/bin/lxc-stop --lxcpath /var/lib/piVCCU --name lxc

rmmod eq3_char_loop bcm2835_raw_uart || true
