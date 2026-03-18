FROM localhost/ubuntu:24.04-riscv

ARG DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

RUN apt update && apt-get install -y --no-install-recommends libgcrypt-dev \
    build-essential \
    ca-certificates \
    git \
    python3-pip \
    python-is-python3 \
    wget \
    libglib2.0-dev \
    libpixman-1-dev \
    meson \
    ninja-build \
    yq \
    pkg-config \
    libevent-dev \
    libssl-dev \
    libslirp-dev \
    python3-tomli \
    gcc-14 \
    g++-14

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 10 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 10

WORKDIR /root
