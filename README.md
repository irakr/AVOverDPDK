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

3. Configuration environment variables:
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
  export TARGET_IP=<ip-of-vm>
  export TARGET_USR=<user-in-vm>
```
  This tells build script to use these IP and username in the ssh command to communicate with the target VM.

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

6. To deploy binaries to VM:
```
   $ sudo -E ./build.sh --deploy
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
