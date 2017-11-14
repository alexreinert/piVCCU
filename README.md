# piVCCU

piVCCU is a project to install the original Homematic CCU2 firmware inside a virtualized container (lxc) on a Raspberry Pi running Raspbian Jessie or Stretch, Armbian 5.x (Asus TinkerBoard) or Tinker OS 2.x

### Prequisites

* Raspberry Pi 2 or 3 or Asus TinkerBoard
* HM-MOD-RPI-PCB
* Raspbian Stretch or Jessie

### Installation
0. Create full backup of your SD card
1. Add the public key of the repository
   ```bash
   wget -q -O - http://alexreinert.github.io/piVCCU/public.key | sudo apt-key add -
   ```

2. Add the package repository
   ```bash
   sudo bash -c 'echo "deb http://alexreinert.github.io/piVCCU stable main" >> /etc/apt/sources.list'
   sudo apt update
   ```
   Instead of `stable` you can also use the `testing` tree, but be aware testing sometimes means not that stable.

3. Install the neccessary kernel modules for the low level communication with the HM-MOD-RPI-PCB
      ```bash
      sudo apt install pivccu-modules-raspberrypi
      ```

4. Enable UART GPIO pins (only on Raspberry Pi 3)
   * Option 1: Disabled bluetooth (prefered)
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=pi3-disable-bt
      EOT'
      sudo systemctl disable hciuart.service
      ```

   * Option 2: Bluetooth attached to mini uart
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=pi3-miniuart-bt
      enable_uart=1
      force_turbo=1
      EOT'
      ```

5. Disable serial console in command line
      ```bash
      sudo sed -i /boot/cmdline.txt -e "s/console=serial0,[0-9]\+ //"
      sudo sed -i /boot/cmdline.txt -e "s/console=ttyAMA0,[0-9]\+ //"
      ```

6. Add network bridge (if you are using wifi please refer to the debian documentation how to configure the network and the bridge)
   * Verify, that *eth0* is the name of your primary network interface:
      ```bash
      sudo ifconfig
      ```
   * Update your config. (Replace *eth0* if necessary)
      ```bash
      sudo apt remove dhcpcd5
      sudo apt install bridge-utils
      sudo bash -c 'cat << EOT > /etc/network/interfaces
      source-directory /etc/network/interfaces.d

      auto lo
      iface lo inet loopback
   
      iface eth0 inet manual
   
      auto br0
      iface br0 inet dhcp
        bridge_ports eth0
      EOT'
      ```
   * You can use an static IP address, too. In that case use instead:
      ```bash
      sudo apt remove dhcpcd5
      sudo apt install bridge-utils
      sudo bash -c 'cat << EOT > /etc/network/interfaces
      source-directory /etc/network/interfaces.d

      auto lo
      iface lo inet loopback
   
      iface eth0 inet manual
   
      auto br0
      iface br0 inet static
        bridge_ports eth0
        address <address>
        netmask <netmask>
        gateway <gateway>
        dns-nameservers <dns1> <dns2>
      EOT'
      ```

7. Reboot the system
   ```bash
   sudo reboot
   ```

8. Install CCU2 container
   ```bash
   sudo apt install pivccu
   ```

9. Start using your new virtualized CCU2, you can get the IP of the container using
   ```bash
   sudo pivccu-info
   ```

### Migrating from custom kernel to original Raspbian kernel with custom modules and device tree overlay
0. Create full backup of your SD card
1. Upgrade to latest pivccu package
   ```bash
   sudo apt update
   sudo apt upgrade
   ```
2. Verify, that at least Version 2.29.23-12 is installed
   ```bash
   sudo dpkg -s pivccu | grep 'Version'
   ```
3. Install original Raspbian kernel and additional custom kernel modules
   ```bash
   sudo apt install pivccu-modules-raspberrypi raspberrypi-kernel
   ```
   (In this step, the two packages should be get installed and the package raspberrypi-kernel-pivccu should be get removed)
4. Reboot the system
   ```bash
   sudo reboot
   ```

### Migration from other systems
* Original CCU2

   Just restore a normal system backup using the CCU web interface
   
* RaspberryMatic
   1. Restore a normal system backup using the CCU web interface
   2. Reinstall all addons using the CCU web interface

* YAHM
   0. Create full backup of your SD card
   1. Create system backup using CCU web interface
   2. Remove YAHM
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
   3. Install piVCCU as described above
   4. Restore the system backup using the CCU web interface
   5. Remove YAHM specific configuration stuff
      ```bash
      sudo systemctl stop pivccu.service

      sudo rm -f /var/lib/piVCCU/userfs/etc/config/no-coprocessor-update
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/rfd.conf -e 's/Improved Coprocessor Initialization = false/Improved Coprocessor Initialization = true/'
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/multimacd.conf -e 's/bcm2835-raw-uart/mxs_auart_raw.0/'

      sudo systemctl start pivccu.service
      ```
      
### Using CUxD and USB devices
1. Create a hook script
   ```bash
   echo '#!/bin/bash' | sudo tee -a /etc/piVCCU/post-start.sh
   sudo chmod +x /etc/piVCCU/post-start.sh
   ```
2. For each device add an entry to this hook file, e.g. here for ```/dev/ttyUSB0```
   ```bash
   echo 'pivccu-device add /dev/ttyUSB0' | sudo tee -a /etc/piVCCU/post-start.sh
   ```
3. The devices will now be available inside the container, just use them like it is described in the CUxD documentation

### Build packages by your own
If you like to build the .deb package by yourself
* Use Ubuntu 16.04 as build system
* Install prequisites *__tbd__*
* Clone source
* create_kernel.sh builds the custom kernel package
* create_pivccu.sh builds the container package
* Deploy the .deb files to an apt repository e.g. using reprepro

### Donations [![Donate](https://img.shields.io/badge/donate-PayPal-green.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KJ3UWNDMXLJKU)
Please consider sending me a donation to not only help me to compensate for expenses regarding piVCCU, but also to keep my general development motivation on a high level. So if you want to donate some money please feel free to send me money via [PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=KJ3UWNDMXLJKU).

### License
piVCCU itself – the source files found in this git repository – are licensed under the conditions of the [Apache License 2.0](https://opensource.org/licenses/Apache-2.0).
The kernel module source files (folder kernel) and the generated kernel .deb files (raspberrypi-kernel-pivccu) licensed under the [GPLv2](http://www.gnu.org/licenses/gpl-2.0.html) license instead.
The generated CCU container .deb files (pivccu) are containing the original CCU2 firmware, containing multiple different licenses. Please refer to [eQ-3](http://www.eq-3.com) for more information.

### Acknowledgement
The base idea of piVCCU is inspired by [YAHM](https://github.com/leonsio/YAHM/) and [lxccu](https://www.lxccu.com/).

