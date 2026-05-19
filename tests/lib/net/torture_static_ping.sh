#!/bin/sh
# torture_static_ping.sh — drive a complete static-IP configuration
# via /sbin/ifconfig, then verify it actually feeds the IP stack by
# pinging the QEMU SLIRP gateway via /tmp/torture_inet.

echo "--- reconfigure eth0 with new address ---"
/sbin/ifconfig eth0 192.0.2.10 netmask 255.255.255.0
/sbin/ifconfig eth0 gateway 192.0.2.1
/sbin/ifconfig eth0
echo "--- restore default config via ifconfig ---"
/sbin/ifconfig eth0 10.0.2.15 netmask 255.255.255.0
/sbin/ifconfig eth0 gateway 10.0.2.2
/sbin/ifconfig eth0
echo "--- ping via the restored config ---"
/tmp/torture_inet ; echo "rc=$?"
echo "torture_static_ping: DONE"
