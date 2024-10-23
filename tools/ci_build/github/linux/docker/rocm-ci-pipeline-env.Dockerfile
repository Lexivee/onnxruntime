# Refer to https://github.com/RadeonOpenCompute/ROCm-docker/blob/master/dev/Dockerfile-ubuntu-22.04-complete
FROM ubuntu:22.04

ARG ROCM_VERSION=6.1.3
ARG AMDGPU_VERSION=${ROCM_VERSION}
ARG APT_PREF='Package: *\nPin: release o=repo.radeon.com\nPin-Priority: 600'
ARG PYTHON_VERSION=3.10

CMD ["/bin/bash"]

RUN echo "$APT_PREF" > /etc/apt/preferences.d/rocm-pin-600

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates curl libnuma-dev gnupg && \
    curl -sL https://repo.radeon.com/rocm/rocm.gpg.key | apt-key add -   &&\
    printf "deb [arch=amd64] https://repo.radeon.com/rocm/apt/$ROCM_VERSION/ jammy main" | tee /etc/apt/sources.list.d/rocm.list   && \
    printf "deb [arch=amd64] https://repo.radeon.com/amdgpu/$AMDGPU_VERSION/ubuntu jammy main" | tee /etc/apt/sources.list.d/amdgpu.list   && \
    apt-get update && apt-get install -y --no-install-recommends \
    sudo \
    libelf1 \
    kmod \
    file \
    python3 \
    python3-pip \
    python${PYTHON_VERSION}-dev \
    python${PYTHON_VERSION}-venv \
    rocm-dev \
    rocm-libs \
    git \
    build-essential && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN groupadd -g 109 render

# Upgrade to meet security requirements
RUN apt-get update -y && apt-get upgrade -y && apt-get autoremove -y && \
    apt-get install  -y locales cifs-utils wget half libnuma-dev lsb-release && \
    apt-get clean -y

RUN locale-gen en_US.UTF-8
RUN update-locale LANG=en_US.UTF-8
ENV LC_ALL C.UTF-8
ENV LANG C.UTF-8

WORKDIR /stage

# Cmake
ENV CMAKE_VERSION=3.30.1
RUN cd /usr/local && \
    wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz && \
    tar -zxf /usr/local/cmake-3.30.1-Linux-x86_64.tar.gz --strip=1 -C /usr

# ccache
RUN mkdir -p /tmp/ccache && \
    cd /tmp/ccache && \
    wget -q -O - https://github.com/ccache/ccache/releases/download/v4.7.4/ccache-4.7.4-linux-x86_64.tar.xz | tar --strip 1 -J -xf - && \
    cp /tmp/ccache/ccache /usr/bin && \
    rm -rf /tmp/ccache

# Set up virtual environment for Python and install dependencies
WORKDIR /ort
COPY scripts/requirements.txt /ort/
RUN python3 -m venv /ort/env && . /ort/env/bin/activate && \
    pip install --upgrade pip && \
    pip install -r /ort/requirements.txt && \
    pip install psutil ml_dtypes pytest-xdist pytest-rerunfailures scipy numpy==1.26.4

ENV LD_LIBRARY_PATH=/opt/rocm/lib:/usr/lib/x86_64-linux-gnu
ENV PATH=/opt/rocm/bin:/usr/bin:/bin:/usr/sbin:/usr/local/bin

# Install Cupy to decrease CPU utilization
# Note that the version of Cupy requires numpy < 1.27
RUN git clone https://github.com/ROCm/cupy && cd cupy && \
    git checkout 2f5409233679582df9f917c75714f129574efc2c && \
    export CUPY_INSTALL_USE_HIP=1 && \
    export ROCM_HOME=/opt/rocm && \
    export HCC_AMDGPU_TARGET=gfx906,gfx908,gfx90a && \
    git submodule update --init && \
    . /ort/env/bin/activate; \
    pip install -e . --no-cache-dir -vvvv
