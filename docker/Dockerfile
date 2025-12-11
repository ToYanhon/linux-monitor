# =============================
# 1. Build stage: compile all C++ binaries
# =============================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# 更换为阿里云镜像源以加速下载
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 直接从 Ubuntu 22.04 官方源安装 BCC（jammy 已包含 bcc 包）
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        software-properties-common \
        git \
        ca-certificates && \
    add-apt-repository -y universe && \
    apt-get update

RUN apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        protobuf-compiler \
        libprotobuf-dev \
        libgrpc++-dev \
        libmysqlclient-dev \
        libbpf-dev \
        libelf-dev \
        zlib1g-dev && \
    rm -rf /var/lib/apt/lists/*

# 从源代码构建 grpc_cpp_plugin
RUN git clone --depth 1 --branch v1.30.2 https://github.com/grpc/grpc.git /tmp/grpc && \
    cd /tmp/grpc && \
    git submodule update --init && \
    mkdir -p cmake/build && \
    cd cmake/build && \
    cmake -DgRPC_INSTALL=ON \
          -DgRPC_BUILD_TESTS=OFF \
          -DCMAKE_BUILD_TYPE=Release \
          ../.. && \
    make grpc_cpp_plugin -j$(nproc) && \
    cp grpc_cpp_plugin /usr/bin/grpc_cpp_plugin && \
    cd / && \
    rm -rf /tmp/grpc

WORKDIR /app

# Copy source code
COPY . /app

# Skip kmod subdirectory when building inside container to avoid kernel headers dependency
RUN sed -i '/add_subdirectory( *kmod *)/d' CMakeLists.txt

# Configure and build (先删除可能存在的CMakeCache.txt)
RUN rm -rf build && \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j

# =============================
# 2. Runtime base image
# =============================
FROM ubuntu:22.04 AS runtime-base

ENV DEBIAN_FRONTEND=noninteractive

# 更换为阿里云镜像源以加速下载
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 直接从 Ubuntu 22.04 官方源安装 BCC 运行时库（jammy 已包含 bcc 包）
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        software-properties-common && \
    add-apt-repository -y universe && \
    apt-get update

RUN apt-get install -y --no-install-recommends \
        libprotobuf-dev \
        libgrpc++1 \
        libmysqlclient-dev \
        libbpf0 \
        libelf1 \
        zlib1g \
        ca-certificates && \
        # bpfcc-tools \
        # libbpfcc-dev \
        # linux-headers-$(uname -r) \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# =============================
# 3. server runtime image
# =============================
FROM runtime-base AS server-runtime
COPY --from=builder /app/build/server/server /usr/local/bin/server
ENTRYPOINT ["/usr/local/bin/server"]

# =============================
# 4. client runtime image
# =============================
FROM runtime-base AS client-runtime
COPY --from=builder /app/build/client/Linux-monitor /usr/local/bin/Linux-monitor
ENTRYPOINT ["/usr/local/bin/Linux-monitor"]

# =============================
# 5. performance_instance runtime image
# =============================
FROM runtime-base AS perf-runtime
COPY --from=builder /app/build/performance_instance/performance_instance /usr/local/bin/performance_instance
ENTRYPOINT ["/usr/local/bin/performance_instance"]
