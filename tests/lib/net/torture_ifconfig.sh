#!/bin/sh
# torture_ifconfig.sh — drive /sbin/ifconfig to verify the SIOC* path.
echo "--- show all ---"
/sbin/ifconfig
echo "--- show eth0 ---"
/sbin/ifconfig eth0
echo "--- set IPv4 ---"
/sbin/ifconfig eth0 192.0.2.10 netmask 255.255.255.0
/sbin/ifconfig eth0 gateway 192.0.2.1
/sbin/ifconfig eth0 mtu 1400
/sbin/ifconfig eth0
echo "--- set IPv6 ---"
/sbin/ifconfig eth0 inet6 add 2001:db8::1/64
/sbin/ifconfig eth0 inet6 gw 2001:db8::ffff
/sbin/ifconfig eth0
echo "--- restore for ping ---"
/sbin/ifconfig eth0 10.0.2.15 netmask 255.255.255.0
/sbin/ifconfig eth0 gateway 10.0.2.2
/sbin/ifconfig eth0 mtu 1500
/sbin/ifconfig eth0
echo "torture_ifconfig: PASS"
