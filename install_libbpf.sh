#!/bin/bash
# 安装和编译libbpf 1.2.0到项目目录

set -e

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBBPF_DIR="$PROJECT_ROOT/third_party/libbpf"
INSTALL_DIR="$PROJECT_ROOT/third_party/libbpf-install"

echo "项目根目录: $PROJECT_ROOT"
echo "libbpf源码目录: $LIBBPF_DIR"
echo "安装目录: $INSTALL_DIR"

# 安装依赖
echo "安装编译依赖..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    git \
    make \
    gcc \
    clang \
    libelf-dev \
    libz-dev \
    pkg-config

# 下载libbpf 1.2.0
echo "下载libbpf 1.2.0..."
if [ ! -d "$LIBBPF_DIR" ]; then
    git clone --depth 1 --branch v1.2.0 https://github.com/libbpf/libbpf.git "$LIBBPF_DIR"
else
    echo "libbpf目录已存在，跳过下载"
fi

# 编译libbpf
echo "编译libbpf..."
cd "$LIBBPF_DIR/src"
make -j$(nproc)

# 安装到本地目录
echo "安装libbpf到 $INSTALL_DIR..."
DESTDIR="$INSTALL_DIR" make install

echo "libbpf安装完成！"
echo "头文件位置: $INSTALL_DIR/usr/include"
echo "库文件位置: $INSTALL_DIR/usr/lib64"

# 更新CMakeLists.txt
echo "更新CMakeLists.txt..."
cd "$PROJECT_ROOT"
if [ -f "monitor/CMakeLists.txt" ]; then
    # 备份原文件
    cp monitor/CMakeLists.txt monitor/CMakeLists.txt.bak
    
    # 使用sed更新路径
    sed -i "s|/tmp/libbpf-install/usr|$INSTALL_DIR/usr|g" monitor/CMakeLists.txt
    echo "CMakeLists.txt已更新"
else
    echo "警告: monitor/CMakeLists.txt 未找到"
fi

echo "完成！现在可以重新构建项目:"
echo "cd $PROJECT_ROOT"
echo "rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)"
