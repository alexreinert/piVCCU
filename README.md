# piVCCU

piVCCU is a project to install the original Homematic CCU2 firmware inside a virtualized container (lxc) on ARM based single board computers.

### Goals
* Option to run CCU2 and other software parallel on one device
* Usage of original CCU2 firmware (and not OCCU)
* As compatible as possible with original CCU2
* Full Homematic and Homematic IP support on all supported platforms
* Support for backup/restore between piVCCU and original CCU2 without modification
* Easy to install and update with apt
* Support not only on Raspberry

### Donations [![Donate](https://img.shields.io/badge/donate-PayPal-green.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KJ3UWNDMXLJKU)
Keeping this project running is very expensive, e.g. I have to buy a lot of different test devices. If you like to support this project, please consider sending me a donation via [PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KJ3UWNDMXLJKU).

### Prequisites

* Supported Single Board Computer
  * Raspberry Pi 2 or 3 running Raspbian Jessie or Stretch
  * Asus Tinkerboard running Armbian with Mainline kernel
  * Orange Pi One, 2, Lite, Plus, Plus 2, Plus 2E, PC, PC Plus running Armbian with Mainline kernel (Experimental)

    :warning: WARNING: Some models of the Orange Pi have a rotated GPIO socket. See [Manual installation instructions](docs/setup/orangepi.md) for more information.
* Properly installed HM-MOD-RPI-PCB

### Pre-prepared sd card images
You can find pre-prepared sd card images [here](https://www.pivccu.de/images).
They are identical to the original distribution lite or server images but have piVCCU already installed like it is described below.
Login to Raspbian based images using user 'pi' and password 'raspberry'.
Login to Armbian based images using user 'root' and password '1234'.

### Manual installation
* [Raspberry Pi](docs/setup/raspberrypi.md)
* [Asus Tinkerboard](docs/setup/tinkerboard.md)
* [Orange Pi One, Plus, Plus 2 and Plus 2E](docs/setup/orangepi.md)

### Updating piVCCU to latest version
Use the normal apt based update mechanism:
```bash
sudo apt update && sudo apt upgrade
```

### Migration from other systems
* Original CCU2

   Just restore a normal system backup using the CCU web interface
   
* RaspberryMatic
   1. Restore a normal system backup using the CCU web interface
   2. Reinstall all addons using the CCU web interface

* YAHM
   1. Create full backup of your SD card
   2. Create system backup using CCU web interface
   3. Remove YAHM on the host (or use a plain new sd card image)
      ```bash
      sudo lxc-stop -n yahm
      sudo rm -f /etc/bash_completion.d/yahm_completion
      sudo rm -f /etc/init.d/hm-mod-rpi-pcb

      sudo rm -rf /opt/YAHM
      sudo rm -rf /var/lib/lxc/yahm

      sudo sed -i /boot/config.txt -e '/dtoverlay=pi3-miniuart-bt/d'
      sudo sed -i /boot/config.txt -e '/dtoverlay=pi3-miniuart-bt-overlay/d'
      sudo sed -i /boot/config.txt -e '/enable_uart=1/d'
      sudo sed -i /boot/config.txt -e '/force_turbo=1/d'

      sudo sed -i /etc/modules -e '/#*eq3_char_loop/d'
      sudo sed -i /etc/modules -e '/#*bcm2835_raw_uart/d'
      ```
   4. Install piVCCU as described above
   5. Restore the system backup using the CCU web interface
   6. Remove YAHM specific configuration stuff (this needs to done, even if you used a new sd card image and after every restore of a YAHM backup)
      ```bash
      sudo systemctl stop pivccu.service

      sudo rm -f /var/lib/piVCCU/userfs/etc/config/no-coprocessor-update
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/rfd.conf -e 's/Improved Coprocessor Initialization = false/Improved Coprocessor Initialization = true/'
      if [ `grep -c '^Improved Coprocessor Initialization' /var/lib/piVCCU/userfs/etc/config/rfd.conf` -eq 0 ]; then sudo sed -i /var/lib/piVCCU/userfs/etc/config/rfd.conf -e 's/\(^Replacemap File.*\)/\1\nImproved Coprocessor Initialization = true\n/'; fi
      if [ `grep -c '\^[Interface 0\]' /var/lib/piVCCU/userfs/etc/config/rfd.conf` -eq 0 ]; then sudo bash -c "echo -e \"\n[Interface 0]\nType = CCU2\nComPortFile = /dev/mmd_bidcos\n#AccessFile = /dev/null\n#ResetFile = /dev/ccu2-ic200\" >> /var/lib/piVCCU/userfs/etc/config/rfd.conf"; fi
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/multimacd.conf -e 's/bcm2835-raw-uart/mxs_auart_raw.0/'
      if [ `grep -c '<name>HmIP-RF</name>' /var/lib/piVCCU/userfs/etc/config/InterfacesList.xml` -eq 0 ]; then sudo bash -c "sed -i /var/lib/piVCCU/userfs/etc/config/InterfacesList.xml -e 's/\(<\/interfaces>\)/\t<ipc>\n\t\t<name>HmIP-RF<\/name>\n\t\t<url>xmlrpc:\/\/127.0.0.1:2010<\/url>\n\t\t<info>HmIP-RF<\/info>\n\t<\/ipc>\n\1/'"; fi
      sudo systemctl start pivccu.service
      ```
   7. If you used YAHM without HmIP (and only then), remove the HmIP keys to avoid migrating duplicate keys (this needs to done, even if you used a new sd card image and after every restore of a YAHM backup)
      ```bash
      sudo systemctl stop pivccu.service
      rm -rf /var/lib/piVCCU/userfs/etc/config/crRFD/data/*
      sudo systemctl start pivccu.service
      ```
   8. If you used YAHM without radio module, you should check your interface assignments of the LAN Gateways in the control panel
      
### Using CUxD and USB devices
1. You can find available devices on the host using
   ```bash
   sudo pivccu-device listavailable
   ```
2. Create a hook script on the host
   ```bash
   bash -c 'echo "#!/bin/bash" > /etc/piVCCU/post-start.sh'
   sudo chmod +x /etc/piVCCU/post-start.sh
   ```
3. For each device add an entry to this hook file, e.g. here for ```/dev/ttyUSB0```
   ```bash
   bash -c 'echo "pivccu-device add /dev/ttyUSB0" >> /etc/piVCCU/post-start.sh'
   ```
4. The devices will now be available inside the container, just use them like it is described in the CUxD documentation

### Build packages by your own
If you like to build the .deb package by yourself
* Use Ubuntu 16.04 as build system
* Install prequisites *__tbd__*
* Clone source
* create_*.sh are the scripts to build the deb packages
* Deploy the .deb files to an apt repository e.g. using reprepro

### License
piVCCU itself – the source files found in this git repository – are licensed under the conditions of the [Apache License 2.0](https://opensource.org/licenses/Apache-2.0).
The kernel module source files (folder kernel) and the generated kernel .deb files (raspberrypi-kernel-pivccu) licensed under the [GPLv2](http://www.gnu.org/licenses/gpl-2.0.html) license instead.
The generated CCU container .deb files (pivccu) are containing the original CCU2 firmware, containing multiple different licenses. Please refer to [eQ-3](http://www.eq-3.com) for more information.

### Acknowledgement
The base idea of piVCCU is inspired by [YAHM](https://github.com/leonsio/YAHM/) and [lxccu](https://www.lxccu.com/).

