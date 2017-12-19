WLAN is not as stable as a wired network connections, there are connections drops sometimes. Please consider using a wired network connection.

For technical reasons, it is not possible to create a linux network bridge using a WLAN interface.
Because of that, you need a bridge without a physical interface and to use port forwading to access the virtual CCU. To configure that, you need to do the following steps.

1. Configure Interfaces
   ```bash
   sudo apt install bridge-utils
   sudo bash -c 'cat << EOT > /etc/network/interfaces
   source-directory /etc/network/interfaces.d

   auto lo
   iface lo inet loopback

   auto wlan0
   iface wlan0 inet dhcp
     wpa-ssid     <PUT_YOUR_SSID_HERE>
     wpa-psk      <PUT_YOUR_WLAN_KEY_HERE>

   auto br0
   iface br0 inet static
     bridge_ports none
     bridge_fd    0
     address      192.168.253.1
     netmask      255.255.255.0
   EOT'
   ```

2. Configure (private) static IP for CCU (this needs to be done after each restore, too)
   ```bash
   sudo systemctl stop pivccu
   sudo bash -c 'cat << EOT > /var/lib/piVCCU/userfs/etc/config/netconfig
   HOSTNAME=homematic-ccu2
   MODE=MANUAL
   CURRENT_IP=192.168.253.2
   CURRENT_NETMASK=255.255.255.0
   CURRENT_GATEWAY=192.168.253.1
   CURRENT_NAMESERVER1=8.8.4.4
   CURRENT_NAMESERVER2=8.8.8.8
   IP=192.168.253.2
   NETMASK=255.255.255.0
   GATEWAY=192.168.253.1
   NAMESERVER1=8.8.4.4
   NAMESERVER2=8.8.8.8
   CRYPT=0
   EOT'
   ```

3. Add IF UP Hook for port forwarding
   ```bash
   sudo bash -c 'cat << EOT > /etc/network/if-up.d/pivccu
   #!/bin/sh

   HOST_IF=wlan0
   BRIDGE=br0
   HOST_IP=192.168.253.1
   CCU_IP=192.168.253.2

   if [ "$IFACE" = "$BRIDGE" ]; then
     echo 1 > /proc/sys/net/ipv4/ip_forward
     iptables -A FORWARD -i $IFACE -s ${HOST_IP}/24 -m conntrack --ctstate NEW -j ACCEPT
     iptables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
     iptables -A POSTROUTING -t nat -j MASQUERADE

     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 80 -j DNAT --to-destination ${CCU_IP}:80
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 1999 -j DNAT --to-destination ${CCU_IP}:1999
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 2000 -j DNAT --to-destination ${CCU_IP}:2000
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 2001 -j DNAT --to-destination ${CCU_IP}:2001
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 2002 -j DNAT --to-destination ${CCU_IP}:2002
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 2010 -j DNAT --to-destination ${CCU_IP}:2010
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 8181 -j DNAT --to-destination ${CCU_IP}:8181
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 8183 -j DNAT --to-destination ${CCU_IP}:8183
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 8700 -j DNAT --to-destination ${CCU_IP}:8700
     iptables -t nat -A PREROUTING -p tcp -i $HOST_IF --dport 8701 -j DNAT --to-destination ${CCU_IP}:8701
   fi
   EOT'
   sudo chmod +x /etc/network/if-up.d/pivccu

4. Reboot

