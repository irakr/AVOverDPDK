#!/usr/bin/bash

NSPK_WORKSPACE="/workspace/NSPK"
RTE_TARGET=x86_64-native-linuxapp-gcc

if ! docker images | awk '{print $1}' | grep nspk-dev; then
    echo "NSPK dev docker image does not exist. Creating one..."
    docker build -t nspk-dev:1.0 .
fi

if ! docker ps -a | awk '{print $2}' | grep nspk-dev; then
    # Start the container
    docker run -d -it \
        -e RTE_SDK=$NSPK_WORKSPACE/deps/dpdk \
        -e RTE_TARGET=x86_64-native-linuxapp-gcc \
        -v "$(pwd)":$NSPK_WORKSPACE \
        --name nspk-dev nspk-dev:1.0
fi

# Shorthand for my docker exec's
function docker_exec()
{
    echo "docker exec -it -w $1 nspk-dev ${@:2}"
    docker exec -it -w $1 nspk-dev ${@:2}
}

# Some initialization work
docker_exec $NSPK_WORKSPACE  git config --global --add safe.directory $NSPK_WORKSPACE
docker_exec $NSPK_WORKSPACE  git config --global --add safe.directory $NSPK_WORKSPACE/deps/dpdk
docker_exec $NSPK_WORKSPACE  git config --global --add safe.directory $NSPK_WORKSPACE/deps/tldk

# Build DPDK
echo "######### Building DPDK ##########"
docker_exec $NSPK_WORKSPACE/deps/dpdk make config T=$RTE_TARGET
docker_exec $NSPK_WORKSPACE/deps/dpdk make -j4
docker_exec $NSPK_WORKSPACE/deps/dpdk make install T=$RTE_TARGET

# Build TLDK
echo "######### Building TLDK ##########"
docker_exec $NSPK_WORKSPACE/deps/tldk make all

# Login to workspace
docker_exec $NSPK_WORKSPACE bash
