FROM localhost/ubuntu:24.04-amd64

ARG DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt install -y git gdisk dosfstools build-essential \
                                  libncurses-dev gawk flex bison openssl libssl-dev tree \
                                  dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf \
                                  device-tree-compiler xz-utils devscripts ccache debhelper asciidoc \
                                  bc rsync cpio python3-dev wget gcc-riscv64-linux-gnu

WORKDIR /root
RUN wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2025.05.16/riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.05.16-nightly.tar.xz && \
    tar -xvf riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.05.16-nightly.tar.xz -C /opt && \
    rm riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.05.16-nightly.tar.xz

ENV PATH="/opt/riscv/bin:${PATH}"
ENV CROSS_COMPILE="riscv64-unknown-linux-gnu-"
ENV ARCH="riscv"
