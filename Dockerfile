FROM ubuntu:20.04
LABEL maintainer="irakrigia92@gmail.com"
ARG DEBIAN_FRONTEND=noninteractive
RUN apt -y update && apt -y install iproute2 iputils-ping tcpdump net-tools binutils gdb nmap vim git nano make
RUN apt -y install gcc g++ meson python3-pyelftools libnuma-dev pkgconf bash-completion linux-headers-generic

# Setup gcc 6 which is compatible to our DPDK and TLDK versions.
# RUN update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-6 60

