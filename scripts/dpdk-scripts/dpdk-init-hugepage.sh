#!/usr/bin/bash

source ./my-dpdk.sh

dpdk-hugepages.py --clear
dpdk-hugepages.py --setup $HUGE_PAGE_SIZE
dpdk-hugepages.py --show

