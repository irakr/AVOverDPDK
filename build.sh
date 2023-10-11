#!/usr/bin/bash

###########################################################
# This script loads this workspace into a linux container
# and performs the build there.
#
# Options:
# --build: Creates a temporary docker image and a container
#          and builds the project inside it.
# --clean: Clean the docker temporary container and image.
# --deploy: Deploy the binaries to remote machine,
#           typically an ubuntu VM.
###########################################################

USAGE=(
"Usage:\n"
"./setup-dev.sh --build|--clean|--deploy\n"
"--build: Creates a temporary docker image and a container\n"
"         and builds the project inside it.\n"
"--clean: Clean the docker temporary container and image.\n"
"--deploy: Deploy the binaries to remote machine, typically an ubuntu VM."
)

# NSPK_BUILD_ENV may be "host" or "docker".
NSPK_BUILD_ENV="host"

# Env NSPK_VM_BUILD:
# Value 1, for output binaries for ubuntu VM.
# Value 0, for output binaries for ubuntu host.

NSPK_ROOT=/nspk
if [[ "$NSPK_BUILD_ENV" = "docker" ]]; then
    NSPK_WORKSPACE="${NSPK_ROOT}/workspace/NSPKCore"
elif [[ "$NSPK_BUILD_ENV" = "host" ]]; then
    NSPK_WORKSPACE="${PWD}"
else
    echo "ERROR: Invalid NSPK_BUILD_ENV." >/dev/stderr
    exit 1
fi
NSPK_INSTALL_PREFIX="/usr/local"
NSPK_CONT_INSTALL_DIR="$NSPK_INSTALL_PREFIX"
RTE_TARGET="x86_64-native-linuxapp-gcc"
IMAGE_NAME="nspk-dev"
IMAGE_VERSION="0.1"
CONTAINER_NAME="nspk-dev"

function run_cmd()
{
    _cmd="${@}"
    echo "$_cmd"
    $_cmd
    if [[ $? -ne 0 ]]; then
        echo "Command failed($?)"
        exit 1
    fi
}

function remote_exec()
{
    _cmd="${@}"
    echo "$_cmd"
    ssh $TARGET_USR@$TARGET_IP "$_cmd"
    if [[ $? -ne 0 ]]; then
        echo "Command failed($?)"
        exit 1
    fi
}

function docker_exec()
{
    echo docker exec -w $1 -it $CONTAINER_NAME bash -c \"$_cmd\"
    docker exec -w $1 -it $CONTAINER_NAME bash -c "$_cmd"
    if [[ $? -ne 0 ]]; then
        echo "cmd_exec command failed($?)"
        echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" >/dev/stderr
        echo "Error(${_ret}): Build has failed. Container still exists." >/dev/stderr
        echo "Please debug the issue manually by accessing the container using below command:" >/dev/stderr
        echo "docker exec -w $NSPK_WORKSPACE -it $CONTAINER_NAME bash" >/dev/stderr
        echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" >/dev/stderr
        exit 1
    fi
}

# Either run in a docker container
# or in the host, based on the NSPK_BUILD_ENV flag.
function cmd_exec()
{
    _cmd="${@:2}"
    if [[ "$NSPK_BUILD_ENV" = "docker" ]]; then
        docker_exec $1 $_cmd
    elif [[ "$NSPK_BUILD_ENV" = "host" ]]; then
        cd $1
        run_cmd $_cmd
    else
        echo "ERROR: NSPK_BUILD_ENV = $NSPK_BUILD_ENV, which is not valid." >/dev/stderr
        exit 1
    fi
}

function _build()
{
    echo "Preparing workspace..."

    if [[ "$NSPK_BUILD_ENV" = "docker" ]]; then
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
                -v $NSPK_INSTALL_PREFIX:$NSPK_INSTALL_PREFIX \
                --name $CONTAINER_NAME $IMAGE_NAME:$IMAGE_VERSION
        fi

        # Some git config when in docker container.
        docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE
        docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/dpdk
        docker_exec $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/tldk
    fi

    # Build dependency DPDK
    echo "######### Building DPDK ##########"

    cmd_exec $NSPK_WORKSPACE/deps/dpdk meson -Dexamples=all build --prefix=$NSPK_INSTALL_PREFIX
    cmd_exec $NSPK_WORKSPACE/deps/dpdk ninja -C build
    cmd_exec $NSPK_WORKSPACE/deps/dpdk/build ninja install

    # Build dependency TLDK
    echo "######### Building TLDK ##########"
    cmd_exec $NSPK_WORKSPACE/deps/tldk make V=1 all

    # Building NSPKCore
    echo "######### Building NSPKCore ##########"
    # make all
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
        echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" >/dev/stderr
        echo "Error(${_ret}): Build has failed. Container still exists." >/dev/stderr
        echo "Please debug the issue manually by accessing the container using below command:" >/dev/stderr
        echo "docker exec -w $NSPK_WORKSPACE -it $CONTAINER_NAME bash" >/dev/stderr
        echo "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" >/dev/stderr
        return $_ret
    fi

    # Make it accessible outside the container.
    if [ ! -z $MY_USER ]; then
        chown -R $MY_USER:$MY_USER build 2>/dev/null
        chown -R $MY_USER:$MY_USER deps/dpdk/build 2>/dev/null
        chown -R $MY_USER:$MY_USER deps/dpdk/$RTE_TARGET 2>/dev/null
        chown -R $MY_USER:$MY_USER deps/tldk/build 2>/dev/null
        chown -R $MY_USER:$MY_USER deps/tldk/$RTE_TARGET 2>/dev/null
    else
        echo "WARNING: MY_USER environment variable not set. Build directories' owner will be root."
    fi

    _clean
    echo "Build successfully completed. Cleaning up temporary workspace container..."
}

function deploy()
{
    run_cmd rsync --progress -avz $NSPK_INSTALL_PREFIX/ ${TARGET_USR}@${TARGET_IP}:$NSPK_INSTALL_PREFIX
}

function clean()
{
    cont_up=$(docker ps -a | grep "nspk-dev" | grep -o "Up" 2>/dev/null)

    if [[ "$NSPK_BUILD_ENV" = "docker" ]] && [[ "$cont_up" = "Up" ]]; then
        cmd_exec $NSPK_WORKSPACE rm -rf build 2>/dev/null
        cmd_exec $NSPK_WORKSPACE/deps/dpdk rm -rf build 2>/dev/null
        cmd_exec $NSPK_WORKSPACE/deps/dpdk rm -rf $RTE_TARGET 2>/dev/null
        cmd_exec $NSPK_WORKSPACE/deps/dpdk rm -rf examples/**/$RTE_TARGET 2>/dev/null
        cmd_exec $NSPK_WORKSPACE/deps/tldk rm -rf build 2>/dev/null
        cmd_exec $NSPK_WORKSPACE/deps/tldk rm -rf $RTE_TARGET 2>/dev/null
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
        cmd_exec $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/share/dpdk/
    else
        run_cmd rm -rf build
        run_cmd rm -rf deps/dpdk/build
        run_cmd rm -rf deps/dpdk/$RTE_TARGET
        run_cmd rm -rf deps/dpdk/examples/**/$RTE_TARGET
        run_cmd rm -rf deps/tldk/build
        run_cmd rm -rf $RTE_TARGET
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
        run_cmd rm -rf $NSPK_INSTALL_PREFIX/share/dpdk
        if [[ "$NSPK_VM_BUILD" = "1" ]]; then
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/bin/testpmd
            remote_exec rm -rf $NSPK_INSTALL_PREFIX/share/dpdk
        fi
    fi

    _clean
}

# Check docker installed or not
if ! docker -v 2>/dev/null; then
    echo "!!! Docker not found in your system. Please install docker first. !!!"
    exit 1
fi

# Parse args
VALID_ARGS=$(getopt -o bcrd:: --long build,clean,rebuild,deploy:: -- "$@")
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
    -d | --deploy)
        echo "Deploying NSPKCore to target server ${TARGET_IP}..."
        deploy
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
