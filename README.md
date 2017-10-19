# piVCCU

piVCCU is a project to install the original Homematic CCU2 firmware inside a virtualized container (lxc) on a Raspberry Pi running Raspbian Jessie or Stretch.

### Prequisites

* Raspberry Pi 2 or 3 (Zero and Zero W should work too, but they are untested.)
* HM-MOD-RPI-PCB
* Raspbian Stretch or Jessie

### Installation

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

3. Install custom kernel with the neccessary kernel modules for Homematic
   ```bash
   sudo apt install raspberrypi-kernel-homematic
   ```

4. Enable UART GPIO pins (only on Raspberry Pi 3)
   * Option 1: Disabled bluetooth (prefered)
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=pi3_disable_bt
      EOT'
      sudo systemctl disable hciuart.service
      ```

   * Option 2: Bluetooth attached to mini uart
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=pi3_miniuart_bt
      enable_uart=1
      force_turbo=1
      EOT'
      ```

5. Disable serial console in command line
      ```bash
      sudo sed -i cmdline.txt -e "s/console=serial0,[0-9]\+ //"
      sudo sed -i cmdline.txt -e "s/console=ttyAMA0,[0-9]\+ //"
      ```

6. Add network bridge (if you are using wifi please refer to the debian documentation how to configure the network and the bridge)
   * Verify, that *eth0* is the name of your primary network interface:
      ```bash
      sudo ifconfig
      ```
   * Update your config. (Replace *eth0* if necessary)
      ```bash
      sudo apt-get purge dhcpcd5
      sudo bash -c 'cat << EOT > /etc/network/interfaces
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
      sudo apt-get purge dhcpcd5
      sudo bash -c 'cat << EOT > /etc/network/interfaces
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

### Migration from other systems
1. Original CCU2 or RaspberryMatic

   Just restore a normal system backup *__(to be verified)__*

2. YAHM
   1. Create full backup of your SD card
   2. Create system backup using CCU webinterface
   3. Remove YAHM *__tbd__*
   4. Install piVCCU as described above
   5. Remove YAHM specific configuration stuff
      ```bash
      sudo systemctl stop pivccu.service

      sudo rm -f /var/lib/piVCCU/userfs/etc/config/no-coprocessor-update
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/rfd.conf -e 's/Improved Coprocessor Initialization = false/Improved Coprocessor Initialization = true/'
      sudo sed -i /var/lib/piVCCU/userfs/etc/config/multimacd.conf -e 's/bcm2835-raw-uart/mxs_auart_raw.0/'

      sudo systemctl start pivccu.service
      ```
      
### Using CUxD and USB devices
*__tbd__*

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
The generated kernel .deb files (raspberrypi-kernel-homematic) are containing the Linux kernel with modification for the Raspberry Pi and additional kernel modules for Homematic, which are all licensed under the [GPLv2](http://www.gnu.org/licenses/gpl-2.0.html) license instead.
The generated CCU container .deb files (pivccu) are containing the original CCU2 firmware, containing multiple different licenses. Please refer to [eQ-3](http://www.eq-3.com) for more information.
