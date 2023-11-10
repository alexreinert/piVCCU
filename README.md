# piVCCU&reg;

piVCCU is a project to install the original Homematic CCU3 firmware inside a virtualized container (lxc) on ARM based single board computers.

### Goals
* Option to run CCU3 and other software parallel on one device
* Usage of original CCU3 firmware (and not OCCU)
* As compatible as possible with original CCU3
* Full Homematic and Homematic IP support on all supported platforms (if RF hardware supports it)
* Support for backup/restore between piVCCU and original CCU3 without modification
* Easy to install and update with apt
* Support not only on Raspberry
* Support for 
  * HM-MOD-RPI-PCB (HmRF+HmIP),
  * RPI-RF-MOD (HmRF+HmIP, Pushbutton is not supported)
  * HmIP-RFUSB (HmRF+HmIP)
  * HmIP-RFUSB-TK (HmIP only)
  * HM-LGW-O-TW-W-EU (HmRF only)
  * [HB-RF-USB](https://github.com/alexreinert/PCB/tree/master/HB-RF-USB) (HmRF+HmIP)
  * [HB-RF-USB-2](https://github.com/alexreinert/PCB/tree/master/HB-RF-USB-2) (HmRF+HmIP)
  * [HB-RF-ETH](https://github.com/alexreinert/PCB/tree/master/HB-RF-ETH) (HmRF+HmIP)

### Donations [<img src="https://ko-fi.com/img/githubbutton_sm.svg" height="20" alt="Support me on Ko-fi">](https://ko-fi.com/alexreinert) [<img src="https://img.shields.io/badge/donate-PayPal-green.svg" height="20" alt="Donate via Paypal">](https://www.paypal.com/donate/?cmd=_s-xclick&hosted_button_id=4PW43VJ2DZ7R2)
Keeping this project running is very expensive, e.g. I have to buy a lot of different test devices. If you like to support this project, please consider sending me a donation via [Ko-fi](https://ko-fi.com/alexreinert), [PayPal](https://www.paypal.com/donate/?cmd=_s-xclick&hosted_button_id=4PW43VJ2DZ7R2) or you can send me a gift from my [Amazon wishlist](https://www.amazon.de/gp/registry/wishlist/3NNUQIQO20AAP/ref=nav_wishlist_lists_1).

### Prequisites
* Debian or Ubuntu based distribution
* armhf or arm64 architecture (x64 is not supported, images with mixed armhf binaries and arm64 kernel are not supported)
* At least kernel 4.14

### Prequisites for HM-MOD-RPI-PCB and RPI-RF-MOD on GPIO header
* Supported Single Board Computer
  * Raspberry Pi 2B/3B/3B+/4B/5B running Raspberry Pi OS Bullseye or Bookworm
  * Asus Tinkerboard running Armbian with Mainline kernel
  * Asus Tinkerboard S running Armbian with Mainline kernel
  * Banana Pi M1 running Armbian with Mainline kernel (LEDs of RPI-RF-MOD not supported due to incompatible GPIO pin header)
  * Banana Pi Pro running Armbian with Mainline kernel
  * Libre Computer AML-S905X-CC (Le Potato) running Armbian with Mainline kernel
  * Odroid C2 running Armbian with Mainline kernel (LEDs of RPI-RF-MOD not supported due to incompatible GPIO pin header)
  * Odroid C4 running Armbian with Mainline kernel (Experimental, LEDs of RPI-RF-MOD not supported due to incompatible GPIO pin header)
  * Orange Pi Zero, Zero Plus, R1 running Armbian with Mainline kernel (LEDs of RPI-RF-MOD not supported due to incompatible GPIO pin header)
  * Orange Pi One, 2, Lite, Plus, Plus 2, Plus 2E, PC, PC Plus running Armbian with Mainline kernel

    :warning: WARNING: Some models of the Orange Pi have a rotated GPIO socket. Please ensure the correct position of Pin 1!
  * NanoPC T4 running Armbian with Mainline kernel

    :warning: WARNING: Do not connect RPI-RF-MOD to a power source. Do connect the NanoPC to a power source only.
  * NanoPi M4 running Armbian with Mainline kernel
  * Rock Pi 4 running Armbian with Mainline kernel
  * Rock64 running Armbian with Mainline kernel (Experimental, LEDs of RPI-RF-MOD not supported due to incompatible GPIO pin header)
  * RockPro64 running Armbian with Mainline kernel

    :warning: WARNING: Do not connect RPI-RF-MOD to a power source. Do connect the RockPro64 to a power source only.
* Properly installed HM-MOD-RPI-PCB or RPI-RF-MOD

### Pre-prepared sd card images
You can find pre-prepared sd card images [here](https://www.pivccu.de/images).
The images are configured to use the HM-MOD-RPI-PCB or RPI-RF-MOD. If you like to use an other radio mode, please see below how to switch it.
They are identical to the original distribution lite or server images but have piVCCU already installed like it is described below.
Login to Raspbian based images using user 'pi' and password 'raspberry'.
Login to Armbian based images using user 'root' and password '1234'.

### Manual installation
* [Raspberry Pi](docs/setup/raspberrypi.md)
* [Armbian](docs/setup/armbian.md)
* [Other OS](docs/setup/otheros.md)

### Updating piVCCU to latest version
Use the normal apt based update mechanism:
```bash
sudo apt update && sudo apt upgrade
```

### Supported Radio modes
* HM-MOD-RPI-PCB and RPI-RF-MOD
  * HmRF and HmIP is supported
  * Works only on Raspbian and Armbian and only on supported hardware platforms
* HmIP-RFUSB
  * Only HmIP is supported. You can add support for HmRF using a external HM-LGW-O-TW-W-EU
* Fake emulation
  * Software emulation of the HM-MOD-RPI-PCB
  * You can add support for (real) HmRF using a external HM-LGW-O-TW-W-EU
* To switch between radio modes use the following command:
  ```bash
  sudo dpkg-reconfigure pivccu3
  ```

### Backup
Starting with version 2.31.25-23 there is the tool pivccu-backup to create CCU2 compatible backups (inside the host).
Be aware, that this is only a backup of the CCU, no settings of the host are saved.

To restore a backup file use the WebUI of the CCU.

### Migrating from piVCCU (CCU2 firmware) to piVCCU3 (CCU3 firmware)
1. Create a full backup of your SD card
2. Create a CCU backup using the CCU web interface
3. Update the apt repositories
   ```bash
   sudo apt update
   ```
4. Remove the CCU2 firmware package
   ```bash
   sudo apt remove pivccu
   ```
5. Install the CCU3 firmware package
   ```bash
   sudo apt install pivccu3
   ```
6. Restore your CCU backup using the CCU web interface
7. Reinstall all Addons using the CCU3/RaspberryMatic versions
8. As the CCU3 firmware does a cherry picking of files beeing restored, you maybe need to restore some files by yourself (e.g. CUxD settings files).
9. If you used hook scripts or a customized lxc config, you need to apply your changes in the new directory /etc/piVCCU3 by yourself.
10. After successful migration you can delete the old piVCCU (CCU2 firmware) data using
   ```bash
   sudo apt purge pivccu
   ```

### Migration from other systems
* Original CCU2
  Just restore a normal system backup using the CCU web interface.
  When changing to piVCCU3 you need to reinstall all Addons using the CCU3/RaspberryMatic versions. As the CCU3 firmware does a cherry picking of files beeing restored, you maybe need to restore some files by yourself (e.g. CUxD settings files).

* Original CCU3
  You can only migrate to piVCCU3.
  Just restore a normal system backup using the CCU web interface.
   
* RaspberryMatic
   1. Restore a normal system backup using the CCU web interface
   2. Reinstall all addons using the CCU web interface
   3. If you previously used YAHM, please follow the instructions for removing YAHM specific configuration stuff below

* YAHM
   * Migrate to piVCCU (CCU2 firmware)
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
         sudo rm -rf /var/lib/piVCCU/userfs/etc/config/crRFD/data/*
         sudo systemctl start pivccu.service
         ```
      8. If you used YAHM without radio module, you should check your interface assignments of the LAN Gateways in the control panel
   * Migrate to piVCCU3 (CCU3 firmware)
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
      4. Install piVCCU3 as described above
      5. Restore the system backup using the CCU web interface
      6. Remove YAHM specific configuration stuff (this needs to done, even if you used a new sd card image and after every restore of a YAHM backup)
         ```bash
         sudo systemctl stop pivccu.service

         sudo sed -i /var/lib/piVCCU3/userfs/etc/config/rfd.conf -e 's/Improved Coprocessor Initialization = false/Improved Coprocessor Initialization = true/'
         if [ `grep -c '^Improved Coprocessor Initialization' /var/lib/piVCCU3/userfs/etc/config/rfd.conf` -eq 0 ]; then sudo sed -i /var/lib/piVCCU3/userfs/etc/config/rfd.conf -e 's/\(^Replacemap File.*\)/\1\nImproved Coprocessor Initialization = true\n/'; fi
         if [ `grep -c '\^[Interface 0\]' /var/lib/piVCCU3/userfs/etc/config/rfd.conf` -eq 0 ]; then sudo bash -c "echo -e \"\n[Interface 0]\nType = CCU2\nComPortFile = /dev/mmd_bidcos\n#AccessFile = /dev/null\n#ResetFile = /dev/ccu2-ic200\" >> /var/lib/piVCCU3/userfs/etc/config/rfd.conf"; fi
         if [ `grep -c '<name>HmIP-RF</name>' /var/lib/piVCCU3/userfs/etc/config/InterfacesList.xml` -eq 0 ]; then sudo bash -c "sed -i /var/lib/piVCCU3/userfs/etc/config/InterfacesList.xml -e 's/\(<\/interfaces>\)/\t<ipc>\n\t\t<name>HmIP-RF<\/name>\n\t\t<url>xmlrpc:\/\/127.0.0.1:2010<\/url>\n\t\t<info>HmIP-RF<\/info>\n\t<\/ipc>\n\1/'"; fi
         sudo systemctl start pivccu.service
         ```
      7. If you used YAHM without HmIP (and only then), remove the HmIP keys to avoid migrating duplicate keys (this needs to done, even if you used a new sd card image and after every restore of a YAHM backup)
         ```bash
         sudo systemctl stop pivccu.service
         sudo rm -rf /var/lib/piVCCU3/userfs/etc/config/crRFD/data/*
         sudo systemctl start pivccu.service
         ```
      8. As the CCU3 firmware does a cherry picking of files beeing restored, you maybe need to restore some files by yourself (e.g. CUxD settings files).
      9. If you used YAHM without radio module, you should check your interface assignments of the LAN Gateways in the control panel
      
### Using USB devices inside container (e.g. for CUxD)
You can configure the USB devices using the installer. You can change it later using
```bash
sudo dpkg-reconfigure pivccu3
```

### Build packages by your own
If you like to build the .deb package by yourself
* Use Debian Bullseye as build system
* Install prequisites: device-tree-compiler build-essential crossbuild-essential-arm64 crossbuild-essential-armhf crossbuild-essential-i386 fuse2fs fuse
* Clone source
* create_*.sh are the scripts to build the deb packages
* Deploy the .deb files to an apt repository e.g. using reprepro

### License
piVCCU itself – the source files found in this git repository – are licensed under the conditions of the [Apache License 2.0](https://opensource.org/licenses/Apache-2.0).
The kernel module source files (folder kernel) and the generated kernel .deb files (raspberrypi-kernel-pivccu) licensed under the [GPLv2](http://www.gnu.org/licenses/gpl-2.0.html) license instead.
The generated CCU container .deb files (pivccu) are containing the original CCU3 firmware, containing multiple different licenses. Please refer to [eQ-3](http://www.eq-3.com) for more information.

### Acknowledgement
The base idea of piVCCU is inspired by [YAHM](https://github.com/leonsio/YAHM/) and [lxccu](https://www.lxccu.com/).

