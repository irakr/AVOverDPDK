#!/usr/bin/bash

source ./my-dpdk.sh

# Huge page config
./bak/dpdk-hugepages.py --clear
./bak/dpdk-hugepages.py --setup $HUGE_PAGE_SIZE
./bak/dpdk-hugepages.py --show

ip link set dev $ETH_INTERFACE down && \
modprobe uio && \
modprobe $PMD_MODULE && \
dpdk-devbind.py -b $PMD_MODULE $ETH_INTERFACE
dpdk-devbind.py -s



