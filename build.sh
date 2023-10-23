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
if [ -z $NSPK_BUILD_ENV ]; then
    NSPK_BUILD_ENV="host"
fi

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
    local _cmd="${@:2}"
    echo "$_cmd"
    $_cmd
    if [[ $? -ne 0 ]] && [[ $1 -ne 0 ]]; then
        echo "Command failed($?)"
        exit 1
    fi
}

function remote_exec()
{
    local _cmd="${@:2}"
    echo "$_cmd"
    ssh $TARGET_USR@$TARGET_IP "$_cmd"
    if [[ $? -ne 0 ]] && [[ $1 -ne 0 ]]; then
        echo "Command failed($?)"
        exit 1
    fi
}

# Check whether docker is installed.
docker_initialized=0

function docker_exec()
{
    # Check docker installed or not
    if [[ $docker_initialized -eq 0 ]]; then
        if ! docker -v 2>/dev/null; then
            echo "!!! Docker not found in your system. Please install docker first. !!!"
            exit 1
        fi
        docker_initialized=1
    fi

    local _cmd="${@:3}"
    echo docker exec -w $2 -it $CONTAINER_NAME bash -c \"$_cmd\"
    docker exec -w $2 -it $CONTAINER_NAME bash -c "$_cmd"
    if [[ $? -ne 0 ]] && [[ $1 -ne 0 ]]; then
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
    local _cmd="${@:3}"
    if [[ "$NSPK_BUILD_ENV" = "docker" ]]; then
        docker_exec $1 $2 $_cmd
    elif [[ "$NSPK_BUILD_ENV" = "host" ]]; then
        cd $2 && \
        run_cmd $1 $_cmd && \
        cd -
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
        docker_exec 0 $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE
        docker_exec 0 $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/dpdk
        docker_exec 0 $NSPK_WORKSPACE git config --global --add safe.directory $NSPK_WORKSPACE/deps/tldk
    fi

    # Build dependency DPDK
    echo "######### Building DPDK ##########"
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        cmd_exec 1 $NSPK_WORKSPACE cp -f $NSPK_WORKSPACE/deps/tmp/dpdk/meson.build $NSPK_WORKSPACE/deps/dpdk/config/x86/meson.build
    fi
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk meson -Dexamples=all build --prefix=$NSPK_INSTALL_PREFIX
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk ninja -C build
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk/build ninja install
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk git checkout config/x86/meson.build
    fi

    # Build dependency TLDK
    echo "######### Building TLDK ##########"
    cmd_exec 1 $NSPK_WORKSPACE/deps/tldk make V=1 all

    # Building NSPKCore
    echo "######### Building NSPKCore ##########"
    cmd_exec 1 $NSPK_WORKSPACE make V=1 all
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
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_INSTALL_PREFIX/ ${TARGET_USR}@${TARGET_IP}:$NSPK_INSTALL_PREFIX
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_WORKSPACE/deps/tldk/${RTE_TARGET}/lib/ ${TARGET_USR}@${TARGET_IP}:$NSPK_INSTALL_PREFIX/lib
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_WORKSPACE/build/ ${TARGET_USR}@${TARGET_IP}:$NSPK_INSTALL_PREFIX/bin
}

function deploy_examples()
{
    run_cmd 1 rsync --progress -avz $NSPK_WORKSPACE/deps/tldk/${RTE_TARGET}/app/l4fwd* ${TARGET_USR}@${TARGET_IP}:$NSPK_INSTALL_PREFIX/bin
}

function clean()
{
    cont_up=$(docker ps -a | grep "nspk-dev" | grep -o "Up" 2>/dev/null)

    if [[ "$NSPK_BUILD_ENV" = "docker" ]] && [[ "$cont_up" = "Up" ]]; then
        cmd_exec 1 $NSPK_WORKSPACE rm -rf build 2>/dev/null
        if [ -d $NSPK_WORKSPACE/deps/dpdk/build ]; then
            cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk/build ninja uninstall 2>/dev/null
        fi
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf build 2>/dev/null
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf $RTE_TARGET 2>/dev/null
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf examples/**/$RTE_TARGET 2>/dev/null
        if [[ "$NSPK_VM_BUILD" = "1" ]]; then
            cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk git checkout config/x86/meson.build
        fi
        cmd_exec 0 $NSPK_WORKSPACE/deps/tldk make clean 2>/dev/null
        cmd_exec 1 $NSPK_WORKSPACE/deps/tldk rm -rf build 2>/dev/null
        cmd_exec 1 $NSPK_WORKSPACE/deps/tldk rm -rf $RTE_TARGET 2>/dev/null
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/testpmd
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/testfib
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/testsad
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/testbbdev
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/l4fwd*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/nspk*
        cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/share/dpdk/
    else
        run_cmd 1 rm -rf build
        if [ -d $NSPK_WORKSPACE/deps/dpdk/build ]; then
            run_cmd 0 cd $NSPK_WORKSPACE/deps/dpdk/build && ninja uninstall && cd $NSPK_WORKSPACE
        fi
        run_cmd 1 rm -rf deps/dpdk/build
        run_cmd 1 rm -rf deps/dpdk/$RTE_TARGET
        run_cmd 1 rm -rf deps/dpdk/examples/**/$RTE_TARGET
        if [[ "$NSPK_VM_BUILD" = "1" ]]; then
            cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk git checkout config/x86/meson.build
        fi
        run_cmd 0 cd deps/tldk && make clean && cd $NSPK_WORKSPACE
        run_cmd 1 rm -rf deps/tldk/build
        run_cmd 1 rm -rf deps/tldk/$RTE_TARGET
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testpmd
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testfib
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testsad
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testbbdev
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/l4fwd*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/bin/nspk*
        run_cmd 1 rm -rf $NSPK_INSTALL_PREFIX/share/dpdk
        if [[ "$NSPK_VM_BUILD" = "1" ]]; then
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/lib/libtle_*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testpmd
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testfib
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testsad
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/testbbdev
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/l4fwd*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/bin/nspk*
            remote_exec 1 rm -rf $NSPK_INSTALL_PREFIX/share/dpdk
        fi
    fi

    _clean
}

# Parse args
VALID_ARGS=$(getopt -o bcrdea:: --long build,clean,rebuild,deploy,deploy-examples,deploy-all:: -- "$@")
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
    -e | --deploy-examples)
        echo "Deploying examples to target server ${TARGET_IP}..."
        deploy_examples
        ret=$?
        break
        # shift
        ;;
    -e | --deploy-all)
        echo "Deploying NSPKCore and examples to target server ${TARGET_IP}..."
        deploy && \
        deploy_examples
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
