## AVOverDPDK

Enabling RTP, RTSP and other network streaming through kernel bypass using DPDK, TLDK and FFmpeg.

##### NOTE: NSPKCore is only supported by x86_64.

## Build guide

##### There are 2 modes of building NSPKCore, one through a docker container and one in the host itself.
 
1. Install dependencies:
   ```
   $ sudo apt install make meson ninja-build pkg-config build-essentials \
     libass-dev libfreetype6-dev libgnutls28-dev libmp3lame-dev libtool \
     libva-dev libvdpau-dev libvorbis-dev libsdl2-dev libxcb1-dev \
     libxcb-shm0-dev libxcb-xfixes0-dev libnuma-dev \
     texinfo wget yasm nasm zlib1g-dev
   $ sudo apt install ffmpeg libavcodec-dev libavdevice-dev libavformat-dev libavutil-dev \
      libavfilter-dev libswscale-dev libpostproc-dev libfdk-aac-dev
   ```

2. Create workspace directory:
  Lets assume the project directory is:
  ~/workspace/projects/
   ```
   $ mkdir NSPK
   $ cd NSPK
   ```

3. Initialize NSPKCore repo:
   ```
   $ git clone git@github.com:irakr/NSPKCore.git
   $ cd NSPKCore/
   $ git submodule init
   $ git submodule update
   ```

4. Configuration environment variables:
   ```
   $ export MY_USER=$USER
   ```
   This is your original username which will be used to chown the output build directories after completion.
   ```
   $ export NSPK_BUILD_ENV="docker"
   or
   $ export NSPK_BUILD_ENV="host"
   ```
   This tells the build script to either build the project in a docker container or in the host machine itself.
   ```
   $ export NSPK_VM_BUILD=1
   or
   $ export NSPK_VM_BUILD=0
   ```
   This tells the build script to generate the binaries for the ubuntu VM target(value == 1).
   This is required because the of the difference in CPU flags and some other differences in hardware features.
   ```
   export NSPK_TARGET_IP=<ip-of-vm>
   export NSPK_TARGET_USER=<user-in-vm>
   ```
   This tells build script to use these IP and username in the ssh command to communicate with the target VM.
   NSPK_TARGET_IP can be **localhost** if the current host itself is the target machine.

5. Now we run the actual build script.
   This script will install and run a linux container and start the build inside it.
   ```
   $ sudo -E ./build.sh --build
   ```
   After this you will be in the appropriate repo directory inside an linux container.

6. To clean the build run:
   ```
   $ sudo -E ./build.sh --clean
   ```

7. To deploy binaries to VM:
   ```
   $ sudo -E ./build.sh --deploy
   ```

## Run guide (TBD)

1. Prepare DPDK ethernet device(s) and hugepage.

   - Edit ./scripts/dpdk-scripts/my-dpdk.sh according to your setup.
   - Run:
   ```
    $ sudo ./scripts/dpdk-scripts/dpdk-init.sh
   ```

2. Configure TLDK fe.cfg and be.cfg:
   - Example: UDP TX only mode:
     **fe.cfg: (Each line represents a single stream)**
     ```
     lcore=2,op=tx,laddr=10.0.0.1,lport=9500,raddr=10.0.0.10,rport=9500,txlen=512
     lcore=1,op=tx,laddr=10.0.0.1,lport=9501,raddr=10.0.0.10,rport=9501,txlen=512
     ```
     **be.cfg:**
     ```
     port=0,masklen=24,addr=10.0.0.10,mac=9e:a2:32:d2:85:5f
     ```

3. Run nspk-core:
   ```
   $ sudo nspk-core -l 1,2 -- --promisc --rbufs 0x100 --sbufs 0x100 --streams 2 --fecfg ./fe.cfg --becfg ./be.cfg -U port=0,lcore=2,   ipv4=10.0.0.1
   ```
