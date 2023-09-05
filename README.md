# NSPKCore

Core library of the NSPK(Net-Speaker) framework

# Build guide

1. Create workspace directory:
  $ mkdir NSPK
  $ cd NSPK

2. Clone NSPKCore repo:
  $ git clone git@github.com:irakr/NSPKCore.git

3. Clone DPDK repo:
  $ git clone https://github.com/DPDK/dpdk.git

4. Build DPDK:
  $ sudo apt install meson build-essential python3-pyelftools libnuma-dev pkgconf
  $ cd dpdk/
  $ meson -Dexamples=all build             # This include examples too.
  $ ninja -C build
  $ cd build/
  $ sudo ninja install

5. Build NSPKCore:
  $ export DPDK_REPO=<Path-to-DPDK-repo>
  $ make [shared|static]

6. Binary will available at build/

# Run guide

1. Prepare DPDK ethernet device(s) and hugepage.

   - Edit ./scripts/dpdk-scripts/my-dpdk.sh according to your setup.
   - Run:
     $ sudo ./scripts/dpdk-scripts/dpdk-init.sh

2. Run nspk-core:
   - $ sudo -E ./build/nspk-core -l 1,2 -n 2

