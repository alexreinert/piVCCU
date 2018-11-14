### Prequisites

* Armbian
* At least kernel 4.14 (Mainline is prefered)
* The HM-MOD-RPI-PCB and the RPI-RF-MOD are only working on supported platforms

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
   sudo apt install `dpkg --get-selections | grep 'linux-image-' | grep '\sinstall' | sed -e 's/linux-image-\([a-z0-9-]\+\).*/linux-headers-\1/'`
   ```

4. Install the neccessary device tree patches (You can skip this step, if you do not use the HM-MOD-RPI-PCB or RPI-RF-MOD)
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
      sudo nmcli connection show --active
      ```

   * Update your config. (Replace *eth0* if necessary)
      ```bash
      sudo apt install bridge-utils
      sudo nmcli connection add ifname br0 type bridge con-name br0
      sudo nmcli connection add type bridge-slave ifname eth0 master br0
      ```
   * You can use an static IP address, too:
      ```bash
      nmcli connection modify br0 ipv4.addresses "<address>/<prefix>" ipv4.gateway "<gateway>" ipv4.dns "<dns1>,<dns2>" ipv4.method "manual"
      ```
      Replace <address>, <prefix>, <gateway>, <dns1>, <dns2> with your settings.
      <prefix> ist the subnet prefix (e.g. 24 for netmask 255.255.255.0)

7. Reboot the system
   ```bash
   sudo reboot
   ```

8. Install CCU container
   * To use CCC2 firmware
      ```bash
      sudo apt install pivccu
      ```
   * To use CCU3 firmware
      ```bash
      sudo apt install pivccu3
      ```

9. Start using your new virtualized CCU, you can get the IP of the container using
   ```bash
   sudo pivccu-info
   ```

