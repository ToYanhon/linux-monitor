# Linux Monitor 项目说明与使用指南

## 概览
- 提供系统监控采集能力：CPU 负载/使用率、软中断、内存、网络、磁盘。
- 数据通过 Protobuf 聚合为 `monitor::proto::MonitorInfo`，由已实现的 `GrpcManager` gRPC 服务对外提供访问与推送。
- 用户态采集来源：
  - 内核模块导出的设备文件（`/dev/cpu_*_monitor`），通过 `mmap` 共享数据。
  - `/proc` 文件系统（如 `/proc/meminfo`、`/proc/net/dev`、`/proc/diskstats`）。
- 构建系统：CMake（≥3.20）统一编译；Protobuf/gRPC 代码由 CMake 自动生成；内核模块由 CMake 驱动 Kbuild 构建。

## 目录结构
- `proto/`：定义消息和服务，CMake 生成 `*.pb.h/cc`、`*.grpc.pb.h/cc` 并打包为静态库 `monitor_proto`。
- `monitor/`：用户态监控库，统一接口与各监控器实现：
  - 接口：`monitor/include/monitor/monitor_inter.hpp:7-13`。
  - 实现：位于 `monitor/src/`，采集 CPU、内存、网络、磁盘、软中断。
- `grpc/`：gRPC 客户端与服务端实现，分别打包为 `client` 与 `server_impl` 静态库。
- `client/`：采集并推送到服务端的可执行程序目标。
- `server/`：服务端可执行程序目标，监听并接收客户端采集数据。
- `performance_server/` 与 `performance_instance/`：从服务端拉取数据并写入 MySQL 的示例实现与可执行程序。
- `kmod/`：内核模块目录，向用户态提供设备数据源。
- `build/`：构建输出与生成的 Protobuf 头文件（位于 `build/proto`）。

## 构建系统
- 顶层 `CMakeLists.txt:1-21`：设置 C++20、开启 `CMAKE_EXPORT_COMPILE_COMMANDS`，加入子目录 `proto/`、`kmod/`、`monitor/`、`grpc/`、`client/`、`server/`、`performance_server/`、`performance_instance/`。
- `proto/CMakeLists.txt:1-24`：
  - `find_package(Protobuf CONFIG REQUIRED)` 与 `find_package(gRPC CONFIG REQUIRED)`。
  - `add_library(monitor_proto ${PROTO_FILES})` 并链接 `protobuf::libprotobuf`、`gRPC::grpc`、`gRPC::grpc++`。
  - `target_include_directories(monitor_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})` 暴露生成头文件目录。
  - 使用 `protobuf_generate` 生成 C++ 与 gRPC 源码，插件来自 `gRPC::grpc_cpp_plugin`。
- `grpc/client` 与 `grpc/server`：分别生成 `client` 与 `server_impl` 静态库，均链接 `monitor_proto`。
- `client/CMakeLists.txt:1-3`：编译可执行程序 `${PROJECT_NAME}`（即 `Linux-monitor`），链接 `monitor` 与 `client`。
- `server/CMakeLists.txt:1-4`：编译可执行程序 `server`，链接 `server_impl`。
- `performance_instance/CMakeLists.txt:1-4`：编译可执行程序 `performance_instance`，链接 `performance_server`。
- `monitor/CMakeLists.txt:1-38`：将 `monitor/src/*.cpp` 打包为静态库 `monitor`，`PUBLIC` 链接 `monitor_proto` 并暴露 `monitor/include`。
  - BCC 依赖：当前 CMake 要求安装 `libbcc-dev`，否则会 `FATAL_ERROR` 终止构建；如暂不需要 eBPF，可调整 CMake 逻辑或先安装依赖。

## Protobuf 模型与服务
- 聚合模型：`proto/monitor_info.proto:1-33`
  - `MonitorInfo` 包含：`name`、`soft_irq`、`cpu_load`、`cpu_stat`、`mem_info`、`net_info`、`disk_info`。
  - `MultiMonitorInfo`：用于批量返回多个实例。
  - `GrpcManager` 服务：`SetMonitorInfo(MonitorInfo)`、`GetMonitorInfo(google.protobuf.Empty)`。
- 子消息：
  - `CpuLoad`（`proto/cpu_load.proto:1-8`）：1/3/15 分钟负载。
  - `CpuStat`（`proto/cpu_stat.proto:1-14`）：每 CPU 使用率分解。
  - `SoftIrq`（`proto/cpu_soft_irq.proto:1-16`）：软中断计数。
  - `MemInfo`（`proto/mem_info.proto:1-25`）：多项内存统计与 `used_percent`。
  - `NetInfo`（`proto/net_info.proto:1-20`）：速率与错误/丢弃及其速率。
  - `DiskInfo`（`proto/disk_info.proto:1-25`）：IO 计数、速率、时延、利用率。

## 监控库设计与实现
- 核心接口：`monitor/include/monitor/monitor_inter.hpp:7-13`，定义 `UpdateOnce(monitor::proto::MonitorInfo*)` 与 `Stop()`。
- CPU 负载：`monitor/src/cpu_load_monitor.cpp:10-33`
  - 打开 `/dev/cpu_load_monitor` 并 `mmap` 读取固定点数据（结构见 `monitor/include/monitor/cpu_load_monitor.hpp:5-9`）。
  - 换算为 `float` 写入 `MonitorInfo.cpu_load`。
- CPU 使用率：`monitor/src/cpu_stat_monitor.cpp:6-112`
  - 打开 `/dev/cpu_stat_monitor` 并 `mmap` 读取每 CPU 统计（结构见 `monitor/include/monitor/cpu_stat_monitor.hpp:8-22`）。
  - 与上次采样缓存对比总时间/忙碌时间差，计算各百分比，写入 `MonitorInfo.cpu_stat`。
- 软中断：`monitor/src/cpu_softirq_monitor.cpp:5-42`
  - `mmap` `/dev/cpu_softirq_monitor` 读取 `softirq_stat`，逐 CPU 追加到 `MonitorInfo.soft_irq`。
- 内存：`monitor/src/mem_monitor.cpp:1-78`
  - 解析 `/proc/meminfo`，单位 KB 转 GB，计算 `used_percent`，填充 `MonitorInfo.mem_info`。
- 网络：`monitor/src/net_monitor.cpp:84-152`
  - 合并 eBPF 模拟获取的流量计数与 `/proc` 错误/丢弃计数，过滤虚拟接口（`is_virtual_interface`），计算 `KB/s` 与错误/丢弃速率，写入 `MonitorInfo.net_info`。
- 磁盘：`monitor/src/disk_monitor.cpp:5-73`
  - 解析 `/proc/diskstats`，跳过 `loop*`/`ram*`，计算读/写速率、IOPS、平均时延、利用率，写入 `MonitorInfo.disk_info`。

## 内核模块（数据源）
- `kmod/CMakeLists.txt:1-53`：自动探测内核版本与构建目录，校验内核头文件安装。
- 构建目标：在顶层构建后可直接执行 `cmake --build build --target modules -j` 触发 Kbuild。
- 设备文件：`/dev/cpu_load_monitor`、`/dev/cpu_stat_monitor`、`/dev/cpu_softirq_monitor`。

## 运行时数据流
- 流程：初始化 `MonitorInfo` → 依次调用各监控器 `UpdateOnce` → 聚合 Protobuf → 通过 gRPC 服务推送到服务器并可供拉取。

## 依赖与环境
- 基础：CMake ≥ 3.20、C++20 编译器、`protoc`、`grpc_cpp_plugin`。
- 生成头文件位置：`build/proto/*.pb.h` 与 `*.grpc.pb.h`；包含时使用无前缀形式（例如 `"monitor_info.pb.h"`）。
- 内核模块：需要匹配版本的内核头文件（`/lib/modules/$(uname -r)/build` 存在）。
- BCC（eBPF，可选/当前必需）：`libbcc-dev` 与对应 headers。

### 依赖安装（Ubuntu/Debian 示例）
- 基础构建工具：`sudo apt update && sudo apt install -y build-essential cmake`
- Protobuf/gRPC：`sudo apt install -y protobuf-compiler libprotobuf-dev libgrpc++-dev`
- MySQL 客户端库：`sudo apt install -y libmysqlclient-dev`
- BCC 与内核头：`sudo apt install -y libbcc-dev linux-headers-$(uname -r)`
- libbpf 依赖（用于网络监控）：
  - 系统 libbpf：`sudo apt install -y libbpf-dev libbpf0`
  - 如果遇到 BTF 错误（内核 6.8+），需要安装 libbpf 1.2.0：运行 `./install_libbpf.sh`
- 如使用非系统包的 gRPC/Protobuf 安装，请在 CMake 配置时设置 `Protobuf_DIR` 与 `gRPC_DIR`。

## 构建与运行
- 构建：`cmake -S . -B build`，`cmake --build build -j`。
- 构建内核模块：`cmake --build build --target modules -j`
- 加载内核模块：`sudo insmod kmod/cpu_stat_monitor.ko`、`sudo insmod kmod/cpu_load_monitor.ko`、`sudo insmod kmod/cpu_softirq_monitor.ko`
- 运行：确认 `/dev/*` 设备存在后启动服务端与客户端，数据将经 gRPC 推送与拉取。

### 组件与可执行文件
- 服务器（gRPC）：
  - 入口：`server/src/main.cpp:1-26`，监听 `0.0.0.0:50051`，注册 `GrpcManager` 服务。
  - 构建目标：`server`（链接 `grpc/server` 的 `server_impl`，依赖 `monitor_proto`）。
- 客户端（采集推送）：
  - 入口：`client/src/main.cpp:1-90`，周期性采集并调用 `RpcClient::SetMonitorInfo` 推送到服务器。
  - 构建目标：`Linux-monitor`（链接 `monitor` 与 `grpc/client` 的 `client`）。
- 数据入库实例：
  - 入口：`performance_instance/src/performance_instance.cpp:1-70`，使用 `PerformanceMonitorClient` 从服务器拉取并写入 MySQL。
  - 构建目标：`performance_instance`（链接 `performance_server`）。

### MySQL 初始化
- 脚本：`sql/init_server_performance.sql`，创建数据库 `monitor_db`、各指标表与视图，并设置清理任务。
- 初始化示例：
  - `mysql -u <user> -p`
  - `source <project_dir>/sql/init_server_performance.sql`

### 性能服务客户端配置
- 头文件：`performance_server/include/manager/performance_server.hpp:16-36`
- 默认 MySQL 配置：`host=localhost`、`user=root`、`password=your_password`、`database=monitor_db`、`port=3306`。
- 构建链接：手工指定 MySQL 头/库路径（见 `performance_server/CMakeLists.txt:1-10`）。
- 运行示例：`./build/performance_instance/performance_instance`（默认连接 `localhost:50051` 并每 60 秒拉取与入库）。

## 快速启动
1. 安装依赖
   - 基础依赖：`sudo apt update && sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev libgrpc++-dev libmysqlclient-dev libbcc-dev linux-headers-$(uname -r) libbpf-dev libbpf0`
   - 如果遇到 BTF 错误（内核 6.8+）：运行 `./install_libbpf.sh`

2. 构建
   - `cmake -S . -B build`
   - `cmake --build build -j`

3. 构建并加载内核模块
   - `cmake --build build --target modules -j`
   - `sudo insmod kmod/cpu_stat_monitor.ko`
   - `sudo insmod kmod/cpu_load_monitor.ko`
   - `sudo insmod kmod/cpu_softirq_monitor.ko`

4. 初始化数据库（MySQL，可选）
   - `mysql -u <user> -p`
   - `source <project_dir>/sql/init_server_performance.sql`

5. 启动项目
   - 服务器：`./build/server/server`
   - 客户端：`./build/client/Linux-monitor`
   - 入库实例（可选）：`./build/performance_instance/performance_instance`

6. 观察日志
   - 服务端将在 `SetMonitorInfo` 调用时输出各项指标（见 `grpc/server/src/server.cpp:1-124`）。

### 环境依赖
- Protobuf 与 gRPC（CMake 包）：如使用自定义安装，设置 `Protobuf_DIR`、`gRPC_DIR` 指向安装路径。
- MySQL Client 库：`/usr/include/mysql` 与 `/usr/lib/x86_64-linux-gnu/libmysqlclient.so`（可按需修改）。
- BCC（当前必需）：`libbcc-dev` 与对应 headers；未安装将导致 `monitor` 构建失败。

### 端口与网络
- 服务器默认监听 `50051`；客户端与性能实例默认连接 `localhost:50051`。
- 如需跨主机部署，修改客户端与性能实例的服务地址参数（`grpc/client/include/rpc/client.hpp:1-22`、`performance_instance/src/performance_instance.cpp:1-70`）。

## 常见问题与排查
- 编译时报 BCC 未找到：
  - 安装 `libbcc-dev`：`sudo apt install -y libbcc-dev`。
  - 或临时修改 `monitor/CMakeLists.txt:1-38` 以跳过 BCC 链接（不建议用于生产）。
- 内核头缺失：
  - 安装匹配版本的内核头文件：`sudo apt install -y linux-headers-$(uname -r)`。
- IntelliSense 找不到 `*.pb.h`：
  - 生成头在 `build/proto`，应使用无 `proto/` 前缀的包含（见 `monitor/include/monitor/monitor_inter.hpp:2-3`）。
  - `monitor` 需 `PUBLIC` 链接 `monitor_proto` 以传播包含路径（已在 `monitor/CMakeLists.txt` 配置）。
  - IDE 指向 `build/compile_commands.json`。
- gRPC 插件未找到：
  - 确认系统存在 `grpc_cpp_plugin`，并由 `proto/CMakeLists.txt:1-24` 获取其 LOCATION。
- 运行时报 BTF 错误（Unsupported BTF_KIND:19）：
  - 问题：系统 libbpf 0.5.0 与内核 6.8+ 的 BTF 格式不兼容。
  - 解决方案：安装 libbpf 1.2.0 到项目目录。
    1. 运行安装脚本：`chmod +x install_libbpf.sh && sudo ./install_libbpf.sh`
    2. 重新构建项目：`rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)`
    3. 详细指南见 `INSTALL_LIBBPF.md`
- 单位换算：
  - 内存 KB→GB；网络速率以 `KB/s`；磁盘扇区按 512 字节换算。

## 改进建议
- 指标单位统一与可视化：将速率统一为 `bytes/s`，内存统一为 `MiB/GiB`，并补充可视化前端示例。
- 健壮性：统一设备/文件打开失败与 `mmap` 失败的日志与重试策略；增强 `/proc` 解析兼容性与异常处理。
- 测试完善：为 `UpdateOnce` 引入单元测试（Mock `/proc` 与设备数据），验证速率与百分比计算逻辑。
- 配置化：将服务器地址与采样周期改为可配置项（环境变量/配置文件）。

## 结语
- 项目采用清晰的分层：内核模块产出原始指标，用户态监控器聚合并转 Protobuf，通过 gRPC 服务对外暴露与交换。
- 构建自动化良好；在 IDE 中使用生成的 `compile_commands.json` 可提升开发体验。
