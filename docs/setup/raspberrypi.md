### Prequisites

* Raspberry Pi 2B/3B/3B+/4B
* Raspberry Pi OS Buster or Bullseye (32 bit image or 64 bit image; the mixed mode 32 bit image with 64 bit kernel is not supported)

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

3. Install the neccessary kernel modules
   ```bash
   sudo apt install build-essential bison flex libssl-dev
   sudo apt install raspberrypi-kernel-headers pivccu-modules-dkms
   ```

4. If you are using a HB-RF-ETH, install the neccessary support package
   ```bash
   sudo apt install hb-rf-eth
   ```

5. Install the neccessary device tree patches (You can skip this step, if you do not use the HM-MOD-RPI-PCB or RPI-RF-MOD on GPIO header, for the HB-RF-USB(-2) and HB-RF-ETH this step is not neccessary)
   ```bash
   sudo apt install pivccu-modules-raspberrypi
   ```

6. Enable UART GPIO pins (not required on Raspberry Pi 2) (You can skip this step, if you do not use the HM-MOD-RPI-PCB or RPI-RF-MOD on GPIO header, for the HB-RF-USB this step is not neccessary)
   * Option 1: Disabled bluetooth (prefered)
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=disable-bt
      EOT'
      systemctl is-active --quiet hciuart.service && sudo systemctl disable hciuart.service
      ```

   * Option 2: Bluetooth attached to mini uart
      ```bash
      sudo bash -c 'cat << EOT >> /boot/config.txt
      dtoverlay=miniuart-bt
      enable_uart=1
      force_turbo=1
      core_freq=250
      EOT'
      ```

7. Disable serial console in command line (You can skip this step, if you do not use the HM-MOD-RPI-PCB or RPI-RF-MOD on GPIO header, for the HB-RF-USB this step is not neccessary)
   ```bash
   sudo sed -i /boot/cmdline.txt -e "s/console=\(serial0\|ttyAMA0\),[0-9]\+ //"
   ```

8. Add network bridge (if you are using wifi please refer to the debian documentation how to configure the network and the bridge)
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

9. Check systemd settings

   Some systemd services like pivccu-rpi-modules.service are depending on network-online.target to ensure that the network is *really* online.
   But in Raspberry Pi OS this is not configured out of the box.
   Required settings are depending on the network configuration method.

   For details look [here](https://www.freedesktop.org/wiki/Software/systemd/NetworkTarget/#cutthecraphowdoimakesurethatmyservicestartsafterthenetworkisreallyonline).

   If network configuration is done in /etc/network/interfaces check:
   ```bash
   systemctl is-enabled ifupdown-wait-online.service
   ```
   If it´s ```disabled``` enable it:
   ```bash
   sudo systemctl enable ifupdown-wait-online.service
   ```

   Edit the configuration file ```/etc/default/networking```:
   ```properties
   WAIT_ONLINE_METHOD=ping
   WAIT_ONLINE_ADDRESS=8.8.8.8
   ```

   ```bash
   sudo sed -i -r "s/^.*(WAIT_ONLINE_METHOD=).*/\1ping/;s/^.*(WAIT_ONLINE_ADDRESS=).*/\18.8.8.8/" /etc/default/networking
   ```

10. Reboot the system
   ```bash
   sudo reboot
   ```

11. Install CCU container
   ```bash
   sudo apt install pivccu3
   ```

11. Start using your new virtualized CCU, you can get the IP of the container using
   ```bash
   sudo pivccu-info
   ```

### Using rpi-update
The stable kernel for the Raspberry Pi is distributed via apt. Normally you should use this kernel. But still there are some reasons to use rpi-update to install the latest (unstable) kernel.
Since version 2.0.7 the pivccu-modules-raspberrypi does support kernels installed by rpi-update. Be aware, that piVCCU start after the reboot with a new kernel will take approx. 15 minutes.

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

