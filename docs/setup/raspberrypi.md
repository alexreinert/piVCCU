### Prequisites

* Raspberry Pi 2 or 3
* Raspbian Stretch or Jessie
* Properly installed HM-MOD-RPI-PCB

### Installation
0. Create full backup of your SD card
1. Add the public key of the repository
   ```bash
   wget -q -O - https://www.pivccu.de/piVCCU/public.key | sudo apt-key add -
   ```

2. Add the package repository
   ```bash
   sudo bash -c 'echo "deb https://www.pivccu.de/piVCCU stable main" >> /etc/apt/sources.list.d/pivccu.list'
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
      sudo ip link show | cut -d' ' -f2 | cut -d: -f1 | grep -e '^e.*'
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
   * To use Wireless LAN, please take a look [here](wlan.md)

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

