# piVCCU

piVCCU is a project to install the Homematic CCU2 on an Raspberry Pi running Raspbian Jessie or Stretch.

# Prequisites

* Raspberry Pi 2 or 3 (only 3 is tested right now)
* EQ3 HM-Mod-RPI-PCB
* Raspbian Jessie or Stretch

# Installation

1. Add the public key of the repository
   ```bash
   wget -q -O - http://alexreinert.github.io/piVCCU/public.key | sudo apt-key add -
   ```

2. Add the package repository
   ```bash
   sudo bash -c 'echo "deb http://alexreinert.github.io/piVCCU stable main" >> /etc/apt/sources.list'
   sudo apt update
   ```
   Instead of `stable` you can also use the `testing` branch.

3. Install custom kernel with the neccessary kernel modules for Homematic
   ```bash
   sudo apt install raspberrypi-kernel-homematic
   ```

4. Disable serial console and bluetooth

   _TBD_

5. Add network bridge

   _TBD_

6. Reboot the system

7. Install CCU container
   ```bash
   sudo apt install pivccu
   ```

8. Start using your new virtualized CCU2, you can get the IP using
   ```bash
   sudo pivccu-info
   ```

# Build packages
If you like to build the .deb package by yourself
* Install prequisites
* Clone source
* create_kernel.sh builds the custom kernel package
* create_pivccu.sh builds the container package
