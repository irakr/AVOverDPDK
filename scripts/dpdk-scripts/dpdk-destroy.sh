#!/usr/bin/bash

source ./my-dpdk.sh

echo "Removing hugepages"
dpdk-hugepages.py --clear

echo "Unbinding net devices from DPDK"
dpdk-devbind.py -u $ETH_DEV

echo "Removing PMD modules: uio, vfio-pci, etc"
rmmod uio $PMD_MODULE

echo "Bringing up net interfaces enp0s9 and enp0s17 to default kernel mode."
ip link set dev $ETH_INTERFACE up

echo "DONE"
