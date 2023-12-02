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

# Flags that show each sub-modules build process is done or not
FFMPEG_BUILD_FLAG=$NSPK_WORKSPACE/.ffmpeg.done
TLDK_BUILD_FLAG=$NSPK_WORKSPACE/.tldk.done
DPDK_BUILD_FLAG=$NSPK_WORKSPACE/.dpdk.done

function run_cmd()
{
    local severity=$1
    local _cmd="${@:2}"
    echo "$_cmd"
    $_cmd
    if [[ $? -ne 0 ]] && [[ $severity -gt 0 ]]; then
        echo "Command failed($?)" >/dev/stderr
        exit 1
    fi
}

function remote_exec()
{
    local _cmd="${@:2}"
    echo "$_cmd"
    ssh $NSPK_TARGET_USER@$NSPK_TARGET_IP "$_cmd"
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

    # Create the build directory early since even submodule compilation uses this
    if [ ! -d $NSPK_WORKSPACE/build ]; then
        cmd_exec 1 $NSPK_WORKSPACE mkdir $NSPK_WORKSPACE/build
    fi

    ######################################
    # Build dependency DPDK
    ######################################
    echo "######### Building DPDK ##########"
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        cmd_exec 1 $NSPK_WORKSPACE cp -f $NSPK_WORKSPACE/deps/tmp/dpdk/meson.build $NSPK_WORKSPACE/deps/dpdk/config/x86/meson.build
    fi
    if [[ ! -d $NSPK_WORKSPACE/build ]] || [[ ! -f $DPDK_BUILD_FLAG ]]; then
        echo "********* DPDK reconfigure  ***********"
        cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk/ rm -rf build
        cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk/ mkdir build
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk meson -Dexamples=all build --prefix=$NSPK_INSTALL_PREFIX
    else
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk meson -Dexamples=all build --prefix=$NSPK_INSTALL_PREFIX
    fi
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk ninja -C build
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk/build ninja install
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk git checkout config/x86/meson.build
    fi
    cmd_exec 1 $NSPK_WORKSPACE touch $DPDK_BUILD_FLAG

    ######################################
    # Build dependency TLDK
    ######################################
    echo "######### Building TLDK ##########"
    if [[ ! -d $NSPK_WORKSPACE/build ]] || [[ ! -f $TLDK_BUILD_FLAG ]]; then
        cmd_exec 0 $NSPK_WORKSPACE/deps/tldk make V=1 clean
    fi
    cmd_exec 1 $NSPK_WORKSPACE/deps/tldk make V=1 all -j8
    cmd_exec 1 $NSPK_WORKSPACE touch $TLDK_BUILD_FLAG

    ######################################
    # Build ffmpeg and libav*
    ######################################
    echo "######### Building FFMPEG and libav* ##########"
    export PKG_CONFIG_PATH="$NSPK_INSTALL_PREFIX/lib/pkgconfig"

    # Reconfigure FFmpeg build if not built earlier.
    if [[ ! -d $NSPK_WORKSPACE/build ]] || [[ ! -f $FFMPEG_BUILD_FLAG ]]; then
        cmd_exec 1 $NSPK_WORKSPACE/deps/FFmpeg \
            ./configure \
            --prefix="$NSPK_INSTALL_PREFIX" \
            --enable-shared \
            --extra-cflags="-I$NSPK_INSTALL_PREFIX/include" \
            --extra-ldflags="-L$NSPK_INSTALL_PREFIX/lib" \
            --extra-libs=-lpthread --extra-libs=-lm \
            --ld="g++" \
            --bindir="$NSPK_INSTALL_PREFIX/bin" \
            --enable-gpl \
            --enable-gnutls \
            --enable-libaom \
            --enable-libass \
            --enable-libfdk-aac \
            --enable-libfreetype \
            --enable-libmp3lame \
            --enable-libopus \
            --enable-libdav1d \
            --enable-libvorbis \
            --enable-libx264 \
            --enable-libx265 \
            --enable-nonfree \
            --disable-optimizations --extra-cflags="-fPIC" --extra-cflags="-gstabs+" --extra-cflags=-fno-omit-frame-pointer --enable-debug=3 --extra-cflags=-fno-inline --disable-stripping
    fi

    cmd_exec 1 $NSPK_WORKSPACE/deps/FFmpeg make V=1 -j8
    cmd_exec 1 $NSPK_WORKSPACE/deps/FFmpeg make install
    cmd_exec 1 $NSPK_WORKSPACE touch $FFMPEG_BUILD_FLAG

    ######################################
    # Building NSPKCore
    ######################################
    echo "######### Building NSPKCore ##########"
    cmd_exec 1 $NSPK_WORKSPACE make V=1 all
}

function _clean()
{
    if [[ "$NSPK_BUILD_ENV" != "docker" ]]; then
        return
    fi

    echo "Cleaning up temporary container workspace."
    cont_up=$(docker ps -a | grep "nspk-dev" | grep -o "Up" 2>/dev/null)
    if [[ "$cont_up" = "Up" ]]; then
        docker stop $CONTAINER_NAME 2>/dev/null
    fi
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
    echo "Build successfully completed."
}

function deploy()
{
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_INSTALL_PREFIX/ ${NSPK_TARGET_USER}@${NSPK_TARGET_IP}:$NSPK_INSTALL_PREFIX
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_WORKSPACE/deps/tldk/${RTE_TARGET}/lib/ ${NSPK_TARGET_USER}@${NSPK_TARGET_IP}:$NSPK_INSTALL_PREFIX/lib
    run_cmd 1 rsync --progress --copy-dirlinks -avz $NSPK_WORKSPACE/build/ ${NSPK_TARGET_USER}@${NSPK_TARGET_IP}:$NSPK_INSTALL_PREFIX/bin
}

function deploy_examples()
{
    run_cmd 1 rsync --progress -avz $NSPK_WORKSPACE/deps/tldk/${RTE_TARGET}/app/l4fwd* ${NSPK_TARGET_USER}@${NSPK_TARGET_IP}:$NSPK_INSTALL_PREFIX/bin
}

function clean()
{
    cont_up=$(docker ps -a | grep "nspk-dev" | grep -o "Up" 2>/dev/null)

    # NSPKCore cleanup
    cmd_exec 1 $NSPK_WORKSPACE rm -rf build 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/nspk*
    
    # FFmpeg cleanup
    cmd_exec 1 $NSPK_WORKSPACE rm -f $FFMPEG_BUILD_FLAG
    cmd_exec 1 $NSPK_WORKSPACE rm -rf build
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libavcodec*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libavdevice*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libavformat*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libavfilter*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libavutil*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libswresample*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libswscale*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libpostproc*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libavcodec*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libavdevice*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libavformat*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libavfilter*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libavutil*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libswresample*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libswscale*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/include/libpostproc*

    # TLDK cleanup
    cmd_exec 1 $NSPK_WORKSPACE rm -f $TLDK_BUILD_FLAG
    cmd_exec 0 $NSPK_WORKSPACE/deps/tldk make clean 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE/deps/tldk rm -rf build 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE/deps/tldk rm -rf $RTE_TARGET 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/lib/libtle_*
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/bin/l4fwd*

    # DPDK cleanup
    cmd_exec 1 $NSPK_WORKSPACE rm -f $DPDK_BUILD_FLAG
    if [ -d $NSPK_WORKSPACE/deps/dpdk/build ]; then
        cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk/build ninja uninstall 2>/dev/null
    fi
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf build 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf $RTE_TARGET 2>/dev/null
    cmd_exec 1 $NSPK_WORKSPACE/deps/dpdk rm -rf examples/**/$RTE_TARGET 2>/dev/null
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        cmd_exec 0 $NSPK_WORKSPACE/deps/dpdk git checkout config/x86/meson.build
    fi
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
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_INSTALL_PREFIX/share/dpdk/

    # Cleanup binaries from remote deploy machine.
    if [[ "$NSPK_VM_BUILD" = "1" ]]; then
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/nspk*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libtle_*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/l4fwd*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/rte_*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/dpdk/
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/generic/rte_*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/librte_*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/librte_*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/dpdk
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/x86_64-linux-gnu/pkgconfig/libdpdk*.pc
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/sbin/dpdk-devbind
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/dpdk-*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/testpmd
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/testfib
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/testsad
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/bin/testbbdev
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/share/dpdk
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libavcodec*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libavdevice*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libavformat*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libavfilter*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libavutil*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libswresample*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libswscale*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/lib/libpostproc*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libavcodec*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libavdevice*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libavformat*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libavfilter*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libavutil*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libswresample*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libswscale*
        remote_exec 0 rm -rf $NSPK_INSTALL_PREFIX/include/libpostproc*
    fi

    # Complete the cleanup
    cmd_exec 1 $NSPK_WORKSPACE rm -rf $NSPK_WORKSPACE/build

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
        echo "Deploying NSPKCore to target server ${NSPK_TARGET_IP}..."
        deploy
        ret=$?
        break
        # shift
        ;;
    -e | --deploy-examples)
        echo "Deploying examples to target server ${NSPK_TARGET_IP}..."
        deploy_examples
        ret=$?
        break
        # shift
        ;;
    -e | --deploy-all)
        echo "Deploying NSPKCore and examples to target server ${NSPK_TARGET_IP}..."
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
