# libbpf 1.2.0 安装指南

## 问题描述
Linux-monitor 项目在运行时报错：
```
Unsupported BTF_KIND:19
failed to find valid kernel BTF
failed to load object 'net_monitor_bpf'
```
这是因为系统自带的 libbpf 0.5.0 版本与内核 6.8 的新 BTF 格式不兼容。

## 解决方案
编译安装 libbpf 1.2.0 到项目目录，并修改项目配置使用新版本。

## 安装步骤

### 方法一：使用安装脚本（推荐）

1. 运行安装脚本：
```bash
chmod +x install_libbpf.sh
sudo ./install_libbpf.sh
```

2. 脚本会自动：
   - 安装编译依赖
   - 下载 libbpf 1.2.0 源码到 `third_party/libbpf/`
   - 编译 libbpf
   - 安装到 `third_party/libbpf-install/`
   - 更新 `monitor/CMakeLists.txt` 使用新路径

3. 重新构建项目：
```bash
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 方法二：手动安装

1. 安装依赖：
```bash
sudo apt-get update
sudo apt-get install -y build-essential git make gcc clang libelf-dev libz-dev pkg-config
```

2. 下载并编译 libbpf：
```bash
# 创建目录
mkdir -p third_party
cd third_party

# 下载 libbpf 1.2.0
git clone --depth 1 --branch v1.2.0 https://github.com/libbpf/libbpf.git

# 编译
cd libbpf/src
make -j$(nproc)

# 安装到项目目录
DESTDIR="../../libbpf-install" make install
cd ../..
```

3. 验证安装：
```bash
ls -la libbpf-install/usr/include/bpf/
ls -la libbpf-install/usr/lib64/
```

4. 项目已配置为自动使用 `third_party/libbpf-install/usr` 目录下的 libbpf。

## 验证安装

1. 检查 CMake 配置：
```bash
cd build
cmake .. | grep "Using local libbpf"
```
应该看到：`Using local libbpf from /home/yanhon/linux-monitor/third_party/libbpf-install/usr`

2. 构建项目：
```bash
make -j$(nproc)
```

3. 运行测试：
```bash
sudo ./build/client/Linux-monitor
```

## 故障排除

### 1. 找不到 libbpf.a
如果构建时出现链接错误，检查库文件位置：
```bash
find third_party/libbpf-install -name "*.a" -o -name "*.so"
```
根据实际位置更新 `monitor/CMakeLists.txt` 中的路径。

### 2. 权限问题
确保有 sudo 权限安装依赖。

### 3. 网络问题
如果 git clone 失败，可以手动下载：
```bash
wget https://github.com/libbpf/libbpf/archive/refs/tags/v1.2.0.tar.gz
tar -xzf v1.2.0.tar.gz
mv libbpf-1.2.0 third_party/libbpf
```

## 恢复系统 libbpf
如果需要恢复使用系统 libbpf，修改 `monitor/CMakeLists.txt`：
```cmake
option(USE_LOCAL_LIBBPF "Use locally compiled libbpf" OFF)
```
然后重新构建项目。

## 参考
- libbpf 项目：https://github.com/libbpf/libbpf
- BTF 格式说明：https://www.kernel.org/doc/html/latest/bpf/btf.html
