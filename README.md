# NSPKCore

Core library of the NSPK(Net-Speaker) framework

# Build guide

NOTE: Currently NSPKCore project is build through a docker container. This is
because the dependencies among TLDK and DPDK are not quiet stable and rely on old versions packages.

1. Create workspace directory:
  Lets assume the project directory is:
  ~/workspace/projects/
```
  $ mkdir NSPK
  $ cd NSPK
```
2. Initialize NSPKCore repo:
```
  $ git clone git@github.com:irakr/NSPKCore.git
  $ cd NSPKCore/
  $ git submodule init
  $ git submodule update
```
4. Now we run the actual build script.
   This script will install and run a linux container and start the build inside it.
```
   $ sudo -E ./build.sh --build
```
   After this you will be in the appropriate repo directory inside an linux container.

5. To clean the build run:
```
   $ sudo -E ./build.sh --clean
```

# Run guide (TBD)

1. Prepare DPDK ethernet device(s) and hugepage.

   - Edit ./scripts/dpdk-scripts/my-dpdk.sh according to your setup.
   - Run:
```
     $ sudo ./scripts/dpdk-scripts/dpdk-init.sh
```

1. Run nspk-core:
```
$ sudo -E ./build/nspk-core -l 1,2 -n 2
```
