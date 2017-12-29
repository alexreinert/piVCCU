### Prequisites

* Orange Pi One, Orange Pi 2, Orange Pi Lite, Orange Pi Plus, Orange Pi Plus 2, Orange Pi Plus 2E, Orange Pi PC, Orane Pi PC Plus
* Armbian using Mainline kernel
* Properly installed HM-MOD-RPI-PCB

### :warning: WARNING
The Orange Pi Plus 2E, the Orange Pi One and the Orange Pi Lite have a rotated GPIO socket.
A normal soldered radio module should lead away from the board.

Please ensure for all models, that you attach the radio module to the right pins in the right direction.

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

3. Install the kernel headers
   ```bash
   sudo apt install linux-headers-next-sunxi
   ```

4. Verify, that your kernel image and your kernel headers at the same version
   ```bash
   sudo dpkg -s linux-headers-next-sunxi | grep Source
   sudo dpkg -s linux-image-next-sunxi | grep Source
   ```

5. Install the neccessary device tree patches
   ```bash
   sudo apt install pivccu-devicetree-armbian
   ```

6. Install the neccessary kernel modules for the low level communication with the HM-MOD-RPI-PCB
   ```bash
   sudo apt install pivccu-modules-dkms
   ```

7. Add network bridge (if you are using wifi please refer to the debian documentation how to configure the network and the bridge)
   * Verify, that *eth0* is the name of your primary network interface:
      ```bash
      sudo ip link show | cut -d' ' -f2 | cut -d: -f1 | grep -e '^e.*'
      ```

   * Update your config. (Replace *eth0* if necessary)
      ```bash
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

8. Reboot the system
   ```bash
   sudo reboot
   ```

9. Install CCU2 container
   ```bash
   sudo apt install pivccu
   ```

10. Start using your new virtualized CCU2, you can get the IP of the container using
   ```bash
   sudo pivccu-info
   ```

