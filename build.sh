#!/usr/bin/bash

###########################################################
# This script loads this workspace into a linux container
# and performs the build there.
#
# Options:
# --build: Creates a temporary docker image and a container
#          and builds the project inside it.
# --clean: Clean the docker temporary container and image.
###########################################################

USAGE=(
"Usage:\n"
"./setup-dev.sh --build|--clean\n"
"--build: Creates a temporary docker image and a container\n"
"         and builds the project inside it.\n"
"--clean: Clean the docker temporary container and image."
)

NSPK_ROOT=/nspk
NSPK_WORKSPACE="${NSPK_ROOT}/workspace/NSPK"
NSPK_INSTALL_DIR=${NSPK_ROOT}/usr/local
INSTALL_DIR="/usr/local"
RTE_TARGET="x86_64-native-linuxapp-gcc"
IMAGE_NAME="nspk-dev"
IMAGE_VERSION="0.1"
CONTAINER_NAME="nspk-dev"

# Shorthand for my docker exec's
function docker_exec()
{
    if [ $# -lt 2 ]; then
        cmd="docker exec -it $CONTAINER_NAME ${@:2}"
    else
        cmd="docker exec -it -w $1 $CONTAINER_NAME ${@:2}"
    fi

    echo $cmd
    $cmd
}

function _build()
{
    echo "Creating workspace..."

    if ! docker images | awk '{print $1}' | grep $IMAGE_NAME; then
        echo "NSPK dev docker image does not exist. Creating one..."
        docker build -t $IMAGE_NAME:$IMAGE_VERSION .
    fi

    if ! docker ps -a | awk '{print $2}' | grep $CONTAINER_NAME; then
        # Start the container
        docker run -d -it \
            -e RTE_SDK=$NSPK_WORKSPACE/deps/dpdk \
            -e RTE_TARGET=x86_64-native-linuxapp-gcc \
            -v "$(pwd)":$NSPK_WORKSPACE \
            -v $INSTALL_DIR:$NSPK_INSTALL_DIR \
            --name $CONTAINER_NAME $IMAGE_NAME:$IMAGE_VERSION
    fi

    # Some initialization work
    docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE
    docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/dpdk
    docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/tldk

    # Build dependency DPDK
    echo "######### Building DPDK ##########"
    docker_exec $NSPK_WORKSPACE/deps/dpdk make config T=$RTE_TARGET
    docker_exec $NSPK_WORKSPACE/deps/dpdk make -j4
    docker_exec $NSPK_WORKSPACE/deps/dpdk make examples T=$RTE_TARGET
    docker_exec $NSPK_WORKSPACE/deps/dpdk make install T=$RTE_TARGET DESTDIR=$NSPK_INSTALL_DIR

    # Build dependency TLDK
    # echo "######### Building TLDK ##########"
    # docker_exec $NSPK_WORKSPACE/deps/tldk make all

    # Building NSPKCore
    # TODO
}

function _clean()
{
    docker stop $CONTAINER_NAME 2>/dev/null
    docker rm $CONTAINER_NAME 2>/dev/null
    docker image rm $IMAGE_NAME:$IMAGE_VERSION 2>/dev/null
}

function build()
{
    _build
    _ret=$?
    if [[ $_ret -ne 0 ]]; then
        echo "Error(${_ret}): Build has failed. Container still exists. Please debug the issue manually" >/dev/stderr
        echo "docker exec -w $NSPK_WORKSPACE -it $CONTAINER_NAME bash" >/dev/stderr
        return $_ret
    fi

    # Make it accessible outside the container.
    chown -R $USER:$USER build 2>/dev/null
    chown -R $USER:$USER deps/dpdk/build 2>/dev/null
    chown -R $USER:$USER deps/tldk/build 2>/dev/null

    # _clean
    echo "Build successfully completed. Cleaning up temporary workspace container..."
}

function clean()
{
    docker_exec $NSPK_WORKSPACE rm -rf build 2>/dev/null
    docker_exec $NSPK_WORKSPACE/deps/dpdk rm -rf build 2>/dev/null
    docker_exec $NSPK_WORKSPACE/deps/tldk rm -rf build 2>/dev/null
    _clean
}

# Parse args
VALID_ARGS=$(getopt -o bcr:: --long build,clean,rebuild:: -- "$@")
if [[ $? -ne 0 ]]; then
    echo -e "${USAGE[@]}"
    exit 1;
fi

eval set -- "$VALID_ARGS"
while [ : ]; do
  case "$1" in
    -b | --build)
        echo "Building NSPKCore..."
        build
        ret=$?
        break
        # shift
        ;;
    -c | --clean)
        echo "Cleaning NSPKCore..."
        clean
        ret=$?
        break
        # shift
        ;;
    -r | --rebuild)
        echo "Rebuilding NSPKCore..."
        clean
        ret=$?
        build
        ret=$?
        break
        # shift
        ;;
    --) shift; 
        break 
        ;;
  esac
done

exit $ret