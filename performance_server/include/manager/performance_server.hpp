// performance_monitor_client.hpp
#pragma once

#include "rpc/client.hpp"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace yanhon {

class PerformanceMonitorClient {
public:
  struct MySQLConfig {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    int port;

    // 使用构造函数而不是成员默认初始化
    MySQLConfig()
        : host("localhost"), user("root"), password("your_password"),
          database("monitor_db"), port(3306) {}

    MySQLConfig(const std::string &h, const std::string &u,
                const std::string &p, const std::string &db, int pt)
        : host(h), user(u), password(p), database(db), port(pt) {}
  };

  PerformanceMonitorClient(
      const std::string &server_address = "localhost:50051",
      const MySQLConfig &mysql_config = MySQLConfig());

  ~PerformanceMonitorClient();

  // 从服务器获取监控数据
  bool fetchMonitorData();

  // 计算衍生性能指标
  void calculateDerivedMetrics();

  // 存储数据到MySQL
  bool storeToDatabase();

  // 定期监控任务
  void startMonitoring(int interval_seconds = 60);
  void stopMonitoring();

private:
  // 数据库连接管理
  bool connectToDatabase();
  void disconnectFromDatabase();
  bool executeQuery(const std::string &query);

  // 主机信息管理
  bool ensureHostExists(const std::string &host_name);
  std::string getOrCreateHostId(const std::string &host_name);

  // 指标计算函数
  float calculateCPUStealPercent(const monitor::proto::CpuStat &cpu_stat);
  float calculateMemoryUsedGB(const monitor::proto::MemInfo &mem_info);
  float calculateMemoryUsagePercent(const monitor::proto::MemInfo &mem_info);
  float calculateCacheTotalGB(const monitor::proto::MemInfo &mem_info);
  float calculateSwapPressure(const monitor::proto::MemInfo &mem_info);

  // 磁盘指标计算
  float calculateDiskAvgQueueLength(const monitor::proto::DiskInfo &disk_info);
  float calculateDiskReadWriteRatio(const monitor::proto::DiskInfo &disk_info);
  float calculateTotalIOPS(const monitor::proto::DiskInfo &disk_info);
  float calculateTotalThroughputMBps(const monitor::proto::DiskInfo &disk_info);

  // 网络指标计算
  float calculateNetworkAvgPacketSize(const monitor::proto::NetInfo &net_info);
  float
  calculateNetworkTotalThroughput(const monitor::proto::NetInfo &net_info);
  float calculateNetworkTotalPackets(const monitor::proto::NetInfo &net_info);

  // 性能评分计算
  float calculateCPUScore(const monitor::proto::MonitorInfo &info);
  float calculateMemoryScore(const monitor::proto::MonitorInfo &info);
  float calculateDiskScore(const monitor::proto::MonitorInfo &info);
  float calculateNetworkScore(const monitor::proto::MonitorInfo &info);
  float
  calculateOverallPerformanceScore(const monitor::proto::MonitorInfo &info);

  // 插入数据到各个表
  bool insertHostInfo(const monitor::proto::MonitorInfo &info);
  bool insertCPUMetrics(const monitor::proto::MonitorInfo &info,
                        const std::string &host_id);
  bool insertSoftIrqMetrics(const monitor::proto::MonitorInfo &info,
                            const std::string &host_id);
  bool insertMemoryMetrics(const monitor::proto::MonitorInfo &info,
                           const std::string &host_id);
  bool insertDiskMetrics(const monitor::proto::MonitorInfo &info,
                         const std::string &host_id);
  bool insertNetworkMetrics(const monitor::proto::MonitorInfo &info,
                            const std::string &host_id);
  bool updatePerformanceSummary(const std::string &host_id);

private:
  std::unique_ptr<RpcClient> rpc_client_;
  MYSQL *mysql_conn_;
  MySQLConfig mysql_config_;

  monitor::proto::MultiMonitorInfo current_data_;
  std::chrono::system_clock::time_point last_fetch_time_;

  bool monitoring_active_;
  std::thread monitoring_thread_;
  std::mutex data_mutex_;
  std::condition_variable monitoring_cv_;

  // 缓存主机ID映射
  std::unordered_map<std::string, std::string> host_id_cache_;
  std::mutex cache_mutex_;
};

} // namespace yanhon
