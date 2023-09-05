#!/usr/bin/bash

source ./my-dpdk.sh

# Huge page config
dpdk-hugepages.py --clear
dpdk-hugepages.py --setup $HUGE_PAGE_SIZE
dpdk-hugepages.py --show

ip link set dev $ETH_INTERFACE down && \
modprobe uio && \
modprobe $PMD_MODULE && \
dpdk-devbind.py -b $PMD_MODULE $ETH_INTERFACE
dpdk-devbind.py -s



