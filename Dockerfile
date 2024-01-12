###########################################################
# Copyright(c) 2023 Irak Rigia
###########################################################
FROM ubuntu
LABEL maintainer="tarakrigia@gmail.com"
ARG DEBIAN_FRONTEND=noninteractive
RUN apt -y update && apt -y install iproute2 iputils-ping tcpdump net-tools binutils gdb nmap vim git nano make
RUN apt -y install gcc-9 g++-9 file meson python3-pyelftools libnuma-dev pkgconf bash-completion linux-headers-generic \
    libssl-dev zlib1g zlib1g-dev libelf-dev

# Setup gcc 9 which is compatible to our DPDK and TLDK versions.
# RUN update-alternatives --remove-all gcc
# RUN update-alternatives --remove-all g++
# RUN update-alternatives --remove-all cc
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-9 60
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 60
RUN update-alternatives --set cc /usr/bin/gcc
RUN update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 60
RUN update-alternatives --set c++ /usr/bin/g++
