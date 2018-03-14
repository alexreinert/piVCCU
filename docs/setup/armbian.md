### Prequisites

* Armbian
* At least kernel 4.4 (Mainline is prefered)
* The HM-MOD-RPI-PCB only works on supported platforms

### Installation
0. Create full backup of your SD card
1. Add the public key of the repository
   ```bash
   wget -q -O - https://www.pivccu.de/piVCCU/public.key | sudo apt-key add -
   ```

2. Add the package repository
   ```bash
   sudo bash -c 'echo "deb https://www.pivccu.de/piVCCU stable main" > /etc/apt/sources.list.d/pivccu.list'
   sudo apt update
   ```
   Instead of `stable` you can also use the `testing` tree, but be aware testing sometimes means not that stable.

3. Install the matching kernel headers
   ```bash
   sudo apt install `dpkg --get-selections | grep 'linux-image-' | grep '\sinstall' | sed -e 's/linux-image-\([a-z-]\+\).*/linux-headers-\1/'`
   ```

4. Install the neccessary device tree patches (You can skip this step, if you do not use the HM-MOD-RPI-PCB)
   ```bash
   sudo apt install pivccu-devicetree-armbian
   ```

5. Install the neccessary kernel modules
   ```bash
   sudo apt install pivccu-modules-dkms
   ```

6. Add network bridge (if you are using wifi please refer to the debian documentation how to configure the network and the bridge)
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

