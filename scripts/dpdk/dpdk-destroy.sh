#!/usr/bin/bash

source ./my-dpdk.sh

echo "Removing hugepages"
dpdk-hugepages.py --clear

echo "Unbinding net device $ETH_DEV from DPDK"
dpdk-devbind.py -u $ETH_DEV

echo "Removing PMD modules: uio and $PMD_MODULE"
rmmod $PMD_MODULE
rmmod uio

echo "Bind the net device back to original kernel net driver $KERN_NET_DEV"
modprobe $KERN_NET_DRV
dpdk-devbind.py -b $KERN_NET_DRV $ETH_DEV

echo "Bringing up net interface $ETH_INTERFACE to default kernel mode."
# Allow sometime for the kernel module to initialize the device.
sleep 3
ip link set dev $ETH_INTERFACE up

echo "DONE"
