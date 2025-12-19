// performance_monitor_impl.cpp
#include "manager/performance_server.hpp"
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace yanhon {

PerformanceMonitorClient::PerformanceMonitorClient(
    const std::string &server_address, const MySQLConfig &mysql_config)
    : mysql_conn_(nullptr), mysql_config_(mysql_config),
      monitoring_active_(false) {

  // 初始化gRPC客户端
  rpc_client_ = std::make_unique<RpcClient>(server_address);

  // 连接数据库
  if (!connectToDatabase()) {
    std::cerr << "Failed to connect to MySQL database" << std::endl;
  }
}

PerformanceMonitorClient::~PerformanceMonitorClient() {
  stopMonitoring();
  disconnectFromDatabase();
}

bool PerformanceMonitorClient::connectToDatabase() {
  mysql_conn_ = mysql_init(nullptr);
  if (!mysql_conn_) {
    std::cerr << "MySQL initialization failed" << std::endl;
    return false;
  }

  // 设置连接选项
  bool reconnect = true;
  mysql_options(mysql_conn_, MYSQL_OPT_RECONNECT, &reconnect);

  if (!mysql_real_connect(
          mysql_conn_, mysql_config_.host.c_str(), mysql_config_.user.c_str(),
          mysql_config_.password.c_str(), mysql_config_.database.c_str(),
          mysql_config_.port, nullptr, 0)) {
    std::cerr << "MySQL connection failed: " << mysql_error(mysql_conn_)
              << std::endl;
    mysql_close(mysql_conn_);
    mysql_conn_ = nullptr;
    return false;
  }

  // 设置字符集
  mysql_set_character_set(mysql_conn_, "utf8mb4");

  std::cout << "Connected to MySQL database successfully" << std::endl;
  return true;
}

void PerformanceMonitorClient::disconnectFromDatabase() {
  if (mysql_conn_) {
    mysql_close(mysql_conn_);
    mysql_conn_ = nullptr;
    std::cout << "Disconnected from MySQL database" << std::endl;
  }
}

bool PerformanceMonitorClient::executeQuery(const std::string &query) {
  if (!mysql_conn_) {
    std::cerr << "Database connection not established" << std::endl;
    return false;
  }

  if (mysql_query(mysql_conn_, query.c_str())) {
    std::cerr << "MySQL query failed: " << mysql_error(mysql_conn_)
              << std::endl;
    std::cerr << "Query: " << query << std::endl;
    return false;
  }
  return true;
}

bool PerformanceMonitorClient::fetchMonitorData() {
  try {
    monitor::proto::MultiMonitorInfo response;
    rpc_client_->GetMonitorInfo(&response);

    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      current_data_ = response;
      last_fetch_time_ = std::chrono::system_clock::now();
    }

    std::cout << "Fetched data from " << response.infos_size() << " hosts"
              << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to fetch monitor data: " << e.what() << std::endl;
    return false;
  }
}

// 指标计算函数实现
float PerformanceMonitorClient::calculateCPUStealPercent(
    const monitor::proto::CpuStat &cpu_stat) {
  float used = cpu_stat.usr_percent() + cpu_stat.system_percent() +
               cpu_stat.nice_percent() + cpu_stat.io_wait_percent() +
               cpu_stat.irq_percent() + cpu_stat.soft_irq_percent();
  return 100.0f - used - cpu_stat.idle_percent();
}

float PerformanceMonitorClient::calculateMemoryUsedGB(
    const monitor::proto::MemInfo &mem_info) {
  return mem_info.total() - mem_info.avail();
}

float PerformanceMonitorClient::calculateMemoryUsagePercent(
    const monitor::proto::MemInfo &mem_info) {
  if (mem_info.total() > 0) {
    return ((mem_info.total() - mem_info.avail()) / mem_info.total()) * 100.0f;
  }
  return 0.0f;
}

float PerformanceMonitorClient::calculateCacheTotalGB(
    const monitor::proto::MemInfo &mem_info) {
  return mem_info.cached() + mem_info.buffers() + mem_info.sreclaimable();
}

float PerformanceMonitorClient::calculateSwapPressure(
    const monitor::proto::MemInfo &mem_info) {
  if (mem_info.total() > 0) {
    return (mem_info.swap_cached() / mem_info.total()) * 100.0f;
  }
  return 0.0f;
}

float PerformanceMonitorClient::calculateDiskAvgQueueLength(
    const monitor::proto::DiskInfo &disk_info) {
  if (disk_info.io_time_ms() > 0) {
    return static_cast<float>(disk_info.weighted_io_time_ms()) /
           disk_info.io_time_ms();
  }
  return 0.0f;
}

float PerformanceMonitorClient::calculateDiskReadWriteRatio(
    const monitor::proto::DiskInfo &disk_info) {
  if (disk_info.writes() > 0) {
    return static_cast<float>(disk_info.reads()) / disk_info.writes();
  }
  return 0.0f;
}

float PerformanceMonitorClient::calculateTotalIOPS(
    const monitor::proto::DiskInfo &disk_info) {
  return disk_info.read_iops() + disk_info.write_iops();
}

float PerformanceMonitorClient::calculateTotalThroughputMBps(
    const monitor::proto::DiskInfo &disk_info) {
  return (disk_info.read_bytes_per_sec() + disk_info.write_bytes_per_sec()) /
         (1024.0f * 1024.0f);
}

float PerformanceMonitorClient::calculateNetworkAvgPacketSize(
    const monitor::proto::NetInfo &net_info) {
  float total_packets =
      net_info.send_packets_rate() + net_info.rcv_packets_rate();
  if (total_packets > 0) {
    float total_bytes = (net_info.send_rate() + net_info.rcv_rate()) *
                        1024.0f; // 假设send_rate单位是KB/s
    return total_bytes / total_packets;
  }
  return 0.0f;
}

float PerformanceMonitorClient::calculateNetworkTotalThroughput(
    const monitor::proto::NetInfo &net_info) {
  return net_info.send_rate() + net_info.rcv_rate();
}

float PerformanceMonitorClient::calculateNetworkTotalPackets(
    const monitor::proto::NetInfo &net_info) {
  return net_info.send_packets_rate() + net_info.rcv_packets_rate();
}

// 性能评分计算方法实现
float PerformanceMonitorClient::calculateCPUScore(
    const monitor::proto::MonitorInfo &info) {
  if (info.cpu_stat_size() == 0) {
    return 0.0f;
  }

  float total_cpu_score = 0.0f;
  int cpu_count = 0;

  // 计算每个CPU的得分
  for (int i = 0; i < info.cpu_stat_size(); ++i) {
    const auto &cpu_stat = info.cpu_stat(i);

    // CPU使用率得分：使用率越低得分越高（0-100%映射到100-0分）
    float cpu_usage = cpu_stat.cpu_percent();
    float cpu_usage_score = std::max(0.0f, 100.0f - cpu_usage);

    // 负载得分：负载越低得分越高
    float load_score = 0.0f;
    if (info.has_cpu_load()) {
      float load_avg = info.cpu_load().load_avg_1();
      // 假设每个CPU核心对应1.0的负载为正常
      // 这里简化处理：负载小于2.0得100分，大于10.0得0分，线性插值
      load_score = std::max(0.0f, 100.0f - (load_avg * 10.0f));
      load_score = std::min(100.0f, load_score);
    }

    // CPU综合得分：使用率权重70%，负载权重30%
    float cpu_score = (cpu_usage_score * 0.7f) + (load_score * 0.3f);
    total_cpu_score += cpu_score;
    cpu_count++;
  }

  return cpu_count > 0 ? total_cpu_score / cpu_count : 0.0f;
}

float PerformanceMonitorClient::calculateMemoryScore(
    const monitor::proto::MonitorInfo &info) {
  if (!info.has_mem_info()) {
    return 0.0f;
  }

  const auto &mem_info = info.mem_info();

  // 内存使用率得分：使用率越低得分越高
  float memory_usage = mem_info.used_percent();
  float usage_score = std::max(0.0f, 100.0f - memory_usage);

  // 可用内存得分：可用内存越多得分越高
  float available_gb = mem_info.avail();
  float available_score = 0.0f;
  if (mem_info.total() > 0) {
    float available_percent = (available_gb / mem_info.total()) * 100.0f;
    available_score =
        std::min(100.0f, available_percent * 2.0f); // 50%可用得100分
  }

  // 缓存效率得分：缓存越多得分越高（但有限制）
  float cache_gb = calculateCacheTotalGB(mem_info);
  float cache_score = 0.0f;
  if (mem_info.total() > 0) {
    float cache_percent = (cache_gb / mem_info.total()) * 100.0f;
    // 缓存占10-30%为最佳，得100分
    if (cache_percent >= 10.0f && cache_percent <= 30.0f) {
      cache_score = 100.0f;
    } else if (cache_percent < 10.0f) {
      cache_score = cache_percent * 10.0f; // 线性增长
    } else {
      cache_score = std::max(0.0f, 100.0f - (cache_percent - 30.0f) * 5.0f);
    }
  }

  // 内存综合得分：使用率权重50%，可用内存权重30%，缓存效率权重20%
  float memory_score =
      (usage_score * 0.5f) + (available_score * 0.3f) + (cache_score * 0.2f);
  return std::min(100.0f, memory_score);
}

float PerformanceMonitorClient::calculateDiskScore(
    const monitor::proto::MonitorInfo &info) {
  if (info.disk_info_size() == 0) {
    return 0.0f;
  }

  float total_disk_score = 0.0f;
  int disk_count = 0;

  for (int i = 0; i < info.disk_info_size(); ++i) {
    const auto &disk_info = info.disk_info(i);

    // 磁盘使用率得分：使用率越低得分越高
    float util_percent = disk_info.util_percent();
    float util_score = std::max(0.0f, 100.0f - util_percent);

    // I/O延迟得分：延迟越低得分越高
    float latency_score = 100.0f;
    float read_latency = disk_info.avg_read_latency_ms();
    float write_latency = disk_info.avg_write_latency_ms();

    if (read_latency > 0 || write_latency > 0) {
      float avg_latency = (read_latency + write_latency) / 2.0f;
      // 假设10ms以下为优秀，100ms以上为差
      latency_score = std::max(0.0f, 100.0f - (avg_latency * 2.0f));
    }

    // I/O队列长度得分：队列越短得分越高
    float queue_length = calculateDiskAvgQueueLength(disk_info);
    float queue_score = std::max(0.0f, 100.0f - (queue_length * 20.0f));

    // 磁盘综合得分：使用率权重40%，延迟权重30%，队列长度权重30%
    float disk_score =
        (util_score * 0.4f) + (latency_score * 0.3f) + (queue_score * 0.3f);
    total_disk_score += disk_score;
    disk_count++;
  }

  return disk_count > 0 ? total_disk_score / disk_count : 0.0f;
}

float PerformanceMonitorClient::calculateNetworkScore(
    const monitor::proto::MonitorInfo &info) {
  if (info.net_info_size() == 0) {
    return 0.0f;
  }

  float total_network_score = 0.0f;
  int network_count = 0;

  for (int i = 0; i < info.net_info_size(); ++i) {
    const auto &net_info = info.net_info(i);

    // 网络吞吐量得分：吞吐量适中为佳（既不过低也不过载）
    float throughput = calculateNetworkTotalThroughput(net_info);
    float throughput_score = 0.0f;

    // 假设100Mbps为理想值，超过1Gbps可能过载
    if (throughput <= 100.0f) {
      throughput_score = throughput; // 线性增长到100分
    } else {
      throughput_score = std::max(0.0f, 100.0f - (throughput - 100.0f) / 10.0f);
    }

    // 数据包大小得分：平均包大小适中为佳
    float avg_packet_size = calculateNetworkAvgPacketSize(net_info);
    float packet_size_score = 0.0f;

    // 假设理想包大小为1500字节（MTU）
    if (avg_packet_size > 0) {
      float diff = std::abs(avg_packet_size - 1500.0f);
      packet_size_score = std::max(0.0f, 100.0f - diff / 15.0f);
    }

    // 网络综合得分：吞吐量权重60%，包大小权重40%
    float network_score =
        (throughput_score * 0.6f) + (packet_size_score * 0.4f);
    total_network_score += network_score;
    network_count++;
  }

  return network_count > 0 ? total_network_score / network_count : 0.0f;
}

float PerformanceMonitorClient::calculateOverallPerformanceScore(
    const monitor::proto::MonitorInfo &info) {
  // 计算各维度得分
  float cpu_score = calculateCPUScore(info);
  float memory_score = calculateMemoryScore(info);
  float disk_score = calculateDiskScore(info);
  float network_score = calculateNetworkScore(info);

  // 加权综合得分：CPU 30%，内存 25%，磁盘 25%，网络 20%
  float overall_score = (cpu_score * 0.3f) + (memory_score * 0.25f) +
                        (disk_score * 0.25f) + (network_score * 0.2f);

  // 输出调试信息
  std::cout << "Performance Scores - CPU: " << cpu_score
            << ", Memory: " << memory_score << ", Disk: " << disk_score
            << ", Network: " << network_score << ", Overall: " << overall_score
            << std::endl;

  return std::min(100.0f, std::max(0.0f, overall_score));
}

bool PerformanceMonitorClient::ensureHostExists(const std::string &host_name) {
  std::string query =
      "SELECT host_id FROM host_info WHERE host_name = '" + host_name + "'";

  if (!executeQuery(query))
    return false;

  MYSQL_RES *result = mysql_store_result(mysql_conn_);
  if (!result) {
    // 主机不存在，插入新记录
    std::string host_id =
        "host_" + host_name + "_" + std::to_string(std::time(nullptr));
    query =
        "INSERT INTO host_info (host_id, host_name, created_at, updated_at) "
        "VALUES ('" +
        host_id + "', '" + host_name + "', NOW(), NOW())";
    return executeQuery(query);
  }

  mysql_free_result(result);
  return true;
}

std::string
PerformanceMonitorClient::getOrCreateHostId(const std::string &host_name) {
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = host_id_cache_.find(host_name);
    if (it != host_id_cache_.end()) {
      return it->second;
    }
  }

  // 查询数据库
  std::string query =
      "SELECT host_id FROM host_info WHERE host_name = '" + host_name + "'";

  if (executeQuery(query)) {
    MYSQL_RES *result = mysql_store_result(mysql_conn_);
    if (result) {
      MYSQL_ROW row = mysql_fetch_row(result);
      if (row) {
        std::string host_id = row[0];
        {
          std::lock_guard<std::mutex> lock(cache_mutex_);
          host_id_cache_[host_name] = host_id;
        }
        mysql_free_result(result);
        return host_id;
      }
      mysql_free_result(result);
    }
  }

  // 如果不存在，创建新记录
  std::string new_host_id =
      "host_" + host_name + "_" + std::to_string(std::time(nullptr));
  query = "INSERT INTO host_info (host_id, host_name, created_at, updated_at) "
          "VALUES ('" +
          new_host_id + "', '" + host_name + "', NOW(), NOW())";

  if (executeQuery(query)) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    host_id_cache_[host_name] = new_host_id;
    return new_host_id;
  }

  return "";
}

bool PerformanceMonitorClient::insertCPUMetrics(
    const monitor::proto::MonitorInfo &info, const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

  // 处理CPU统计
  for (int i = 0; i < info.cpu_stat_size(); ++i) {
    const auto &cpu_stat = info.cpu_stat(i);

    float steal_percent = calculateCPUStealPercent(cpu_stat);

    std::string query = "INSERT INTO cpu_metrics ("
                        "host_id, timestamp, cpu_name, cpu_percent, "
                        "usr_percent, system_percent, nice_percent, "
                        "idle_percent, io_wait_percent, irq_percent, "
                        "soft_irq_percent, load_avg_1, load_avg_3, load_avg_15"
                        ") VALUES ("
                        "'" +
                        host_id +
                        "', "
                        "'" +
                        time_str +
                        "', "
                        "'" +
                        cpu_stat.cpu_name() + "', " +
                        std::to_string(cpu_stat.cpu_percent()) + ", " +
                        std::to_string(cpu_stat.usr_percent()) + ", " +
                        std::to_string(cpu_stat.system_percent()) + ", " +
                        std::to_string(cpu_stat.nice_percent()) + ", " +
                        std::to_string(cpu_stat.idle_percent()) + ", " +
                        std::to_string(cpu_stat.io_wait_percent()) + ", " +
                        std::to_string(cpu_stat.irq_percent()) + ", " +
                        std::to_string(cpu_stat.soft_irq_percent()) + ", " +
                        std::to_string(info.cpu_load().load_avg_1()) + ", " +
                        std::to_string(info.cpu_load().load_avg_3()) + ", " +
                        std::to_string(info.cpu_load().load_avg_15()) + ")";

    if (!executeQuery(query)) {
      return false;
    }
  }

  return true;
}

bool PerformanceMonitorClient::insertSoftIrqMetrics(
    const monitor::proto::MonitorInfo &info, const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

  for (int i = 0; i < info.soft_irq_size(); ++i) {
    const auto &soft_irq = info.soft_irq(i);

    std::string query = "INSERT INTO soft_irq_metrics ("
                        "host_id, timestamp, cpu, hi, timer, net_tx, net_rx, "
                        "block, irq_poll, tasklet, sched, hrtimer, rcu"
                        ") VALUES ("
                        "'" +
                        host_id +
                        "', "
                        "'" +
                        time_str +
                        "', "
                        "'" +
                        soft_irq.cpu() + "', " + std::to_string(soft_irq.hi()) +
                        ", " + std::to_string(soft_irq.timer()) + ", " +
                        std::to_string(soft_irq.net_tx()) + ", " +
                        std::to_string(soft_irq.net_rx()) + ", " +
                        std::to_string(soft_irq.block()) + ", " +
                        std::to_string(soft_irq.irq_poll()) + ", " +
                        std::to_string(soft_irq.tasklet()) + ", " +
                        std::to_string(soft_irq.sched()) + ", " +
                        std::to_string(soft_irq.hrtimer()) + ", " +
                        std::to_string(soft_irq.rcu()) + ")";

    if (!executeQuery(query)) {
      return false;
    }
  }

  return true;
}

bool PerformanceMonitorClient::insertMemoryMetrics(
    const monitor::proto::MonitorInfo &info, const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

  const auto &mem_info = info.mem_info();

  std::string query = "INSERT INTO memory_metrics ("
                      "host_id, timestamp, total_gb, free_gb, available_gb, "
                      "buffers_gb, cached_gb, swap_cached_gb, active_gb, "
                      "inactive_gb, dirty_gb, writeback_gb, anon_pages_gb, "
                      "mapped_gb, used_percent"
                      ") VALUES ("
                      "'" +
                      host_id +
                      "', "
                      "'" +
                      time_str + "', " + std::to_string(mem_info.total()) +
                      ", " + std::to_string(mem_info.free()) + ", " +
                      std::to_string(mem_info.avail()) + ", " +
                      std::to_string(mem_info.buffers()) + ", " +
                      std::to_string(mem_info.cached()) + ", " +
                      std::to_string(mem_info.swap_cached()) + ", " +
                      std::to_string(mem_info.active()) + ", " +
                      std::to_string(mem_info.inactive()) + ", " +
                      std::to_string(mem_info.dirty()) + ", " +
                      std::to_string(mem_info.writeback()) + ", " +
                      std::to_string(mem_info.anon_pages()) + ", " +
                      std::to_string(mem_info.mapped()) + ", " +
                      std::to_string(mem_info.used_percent()) + ")";

  return executeQuery(query);
}

bool PerformanceMonitorClient::insertDiskMetrics(
    const monitor::proto::MonitorInfo &info, const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

  for (int i = 0; i < info.disk_info_size(); ++i) {
    const auto &disk_info = info.disk_info(i);

    std::string query =
        "INSERT INTO disk_metrics ("
        "host_id, timestamp, disk_name, `reads`, `writes`, "
        "sectors_read, sectors_written, read_time_ms, "
        "write_time_ms, io_in_progress, io_time_ms, "
        "weighted_io_time_ms, read_mbps, write_mbps, "
        "read_iops, write_iops, avg_read_latency_ms, "
        "avg_write_latency_ms, util_percent"
        ") VALUES ("
        "'" +
        host_id +
        "', "
        "'" +
        time_str +
        "', "
        "'" +
        disk_info.name() + "', " + std::to_string(disk_info.reads()) + ", " +
        std::to_string(disk_info.writes()) + ", " +
        std::to_string(disk_info.sectors_read()) + ", " +
        std::to_string(disk_info.sectors_written()) + ", " +
        std::to_string(disk_info.read_time_ms()) + ", " +
        std::to_string(disk_info.write_time_ms()) + ", " +
        std::to_string(disk_info.io_in_progress()) + ", " +
        std::to_string(disk_info.io_time_ms()) + ", " +
        std::to_string(disk_info.weighted_io_time_ms()) + ", " +
        std::to_string(disk_info.read_bytes_per_sec() / (1024.0f * 1024.0f)) +
        ", " +
        std::to_string(disk_info.write_bytes_per_sec() / (1024.0f * 1024.0f)) +
        ", " + std::to_string(disk_info.read_iops()) + ", " +
        std::to_string(disk_info.write_iops()) + ", " +
        std::to_string(disk_info.avg_read_latency_ms()) + ", " +
        std::to_string(disk_info.avg_write_latency_ms()) + ", " +
        std::to_string(disk_info.util_percent()) + ")";

    if (!executeQuery(query)) {
      return false;
    }
  }

  return true;
}

bool PerformanceMonitorClient::insertNetworkMetrics(
    const monitor::proto::MonitorInfo &info, const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);

  for (int i = 0; i < info.net_info_size(); ++i) {
    const auto &net_info = info.net_info(i);

    std::string query = "INSERT INTO network_metrics ("
                        "host_id, timestamp, interface_name, "
                        "send_rate_mbps, rcv_rate_mbps, "
                        "send_packets_pps, rcv_packets_pps"
                        ") VALUES ("
                        "'" +
                        host_id +
                        "', "
                        "'" +
                        time_str +
                        "', "
                        "'" +
                        net_info.name() + "', " +
                        std::to_string(net_info.send_rate()) + ", " +
                        std::to_string(net_info.rcv_rate()) + ", " +
                        std::to_string(net_info.send_packets_rate()) + ", " +
                        std::to_string(net_info.rcv_packets_rate()) + ")";

    if (!executeQuery(query)) {
      return false;
    }
  }

  return true;
}

bool PerformanceMonitorClient::updatePerformanceSummary(
    const std::string &host_id) {
  auto timestamp = std::chrono::system_clock::now();
  std::time_t time_t = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm = *std::localtime(&time_t);
  char time_str[100];
  std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:00:00", &tm);

  // 获取最新的监控数据来计算性能得分
  monitor::proto::MultiMonitorInfo data_copy;
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    data_copy = current_data_;
  }

  float performance_score = 0.0f;
  bool has_score = false;

  // 查找对应主机的监控信息
  for (int i = 0; i < data_copy.infos_size(); ++i) {
    const auto &monitor_info = data_copy.infos(i);

    // 获取主机名
    std::string host_name = monitor_info.name();
    std::string current_host_id = getOrCreateHostId(host_name);

    if (current_host_id == host_id) {
      // 计算性能得分
      performance_score = calculateOverallPerformanceScore(monitor_info);
      has_score = true;
      break;
    }
  }

  // 构建查询，包含性能得分
  std::string query =
      "INSERT INTO performance_summary ("
      "host_id, time_bucket, bucket_type, "
      "avg_cpu_percent, max_cpu_percent, "
      "avg_load_1, max_load_1, "
      "avg_memory_usage, max_memory_usage, "
      "min_available_memory_gb, "
      "max_disk_util, avg_disk_iops, "
      "peak_disk_throughput_mbps, "
      "avg_network_throughput_mbps, "
      "peak_network_throughput_mbps, "
      "performance_score"
      ") "
      "SELECT "
      "'" +
      host_id +
      "', "
      "'" +
      time_str +
      "', "
      "'hourly', "
      "AVG(cpu_percent), MAX(cpu_percent), "
      "AVG(load_avg_1), MAX(load_avg_1), "
      "AVG(used_percent), MAX(used_percent), "
      "MIN(available_gb), "
      "MAX(util_percent), AVG(read_iops + write_iops), "
      "MAX(read_mbps + write_mbps), "
      "AVG(send_rate_mbps + rcv_rate_mbps), "
      "MAX(send_rate_mbps + rcv_rate_mbps), " +
      (has_score ? std::to_string(performance_score) : "NULL") +
      " "
      "FROM cpu_metrics cm "
      "LEFT JOIN memory_metrics mm ON cm.host_id = mm.host_id "
      "AND mm.timestamp >= DATE_SUB('" +
      time_str +
      "', INTERVAL 1 HOUR) "
      "AND mm.timestamp <= '" +
      time_str +
      "' "
      "LEFT JOIN disk_metrics dm ON cm.host_id = dm.host_id "
      "AND dm.timestamp >= DATE_SUB('" +
      time_str +
      "', INTERVAL 1 HOUR) "
      "AND dm.timestamp <= '" +
      time_str +
      "' "
      "LEFT JOIN network_metrics nm ON cm.host_id = nm.host_id "
      "AND nm.timestamp >= DATE_SUB('" +
      time_str +
      "', INTERVAL 1 HOUR) "
      "AND nm.timestamp <= '" +
      time_str +
      "' "
      "WHERE cm.host_id = '" +
      host_id +
      "' "
      "AND cm.timestamp >= DATE_SUB('" +
      time_str +
      "', INTERVAL 1 HOUR) "
      "AND cm.timestamp <= '" +
      time_str +
      "' "
      "GROUP BY cm.host_id "
      "ON DUPLICATE KEY UPDATE "
      "avg_cpu_percent = VALUES(avg_cpu_percent), "
      "max_cpu_percent = VALUES(max_cpu_percent), "
      "avg_load_1 = VALUES(avg_load_1), "
      "max_load_1 = VALUES(max_load_1), "
      "avg_memory_usage = VALUES(avg_memory_usage), "
      "max_memory_usage = VALUES(max_memory_usage), "
      "min_available_memory_gb = VALUES(min_available_memory_gb), "
      "max_disk_util = VALUES(max_disk_util), "
      "avg_disk_iops = VALUES(avg_disk_iops), "
      "peak_disk_throughput_mbps = VALUES(peak_disk_throughput_mbps), "
      "avg_network_throughput_mbps = VALUES(avg_network_throughput_mbps), "
      "peak_network_throughput_mbps = VALUES(peak_network_throughput_mbps), "
      "performance_score = VALUES(performance_score)";

  return executeQuery(query);
}

bool PerformanceMonitorClient::storeToDatabase() {
  if (!mysql_conn_) {
    std::cerr << "Database connection not established" << std::endl;
    return false;
  }

  bool all_success = true;

  monitor::proto::MultiMonitorInfo data_copy;
  {
    std::lock_guard<std::mutex> lock(data_mutex_);
    data_copy = current_data_;
  }

  for (int i = 0; i < data_copy.infos_size(); ++i) {
    const auto &monitor_info = data_copy.infos(i);
    std::string host_name = monitor_info.name();

    // 获取或创建主机ID
    std::string host_id = getOrCreateHostId(host_name);
    if (host_id.empty()) {
      std::cerr << "Failed to get/create host ID for: " << host_name
                << std::endl;
      all_success = false;
      continue;
    }

    // 插入各项指标
    if (!insertCPUMetrics(monitor_info, host_id)) {
      std::cerr << "Failed to insert CPU metrics for: " << host_name
                << std::endl;
      all_success = false;
    }

    if (!insertSoftIrqMetrics(monitor_info, host_id)) {
      std::cerr << "Failed to insert SoftIRQ metrics for: " << host_name
                << std::endl;
      all_success = false;
    }

    if (!insertMemoryMetrics(monitor_info, host_id)) {
      std::cerr << "Failed to insert memory metrics for: " << host_name
                << std::endl;
      all_success = false;
    }

    if (!insertDiskMetrics(monitor_info, host_id)) {
      std::cerr << "Failed to insert disk metrics for: " << host_name
                << std::endl;
      all_success = false;
    }

    if (!insertNetworkMetrics(monitor_info, host_id)) {
      std::cerr << "Failed to insert network metrics for: " << host_name
                << std::endl;
      all_success = false;
    }

    // 更新性能摘要（每小时一次）
    auto now = std::chrono::system_clock::now();
    auto minutes =
        std::chrono::duration_cast<std::chrono::minutes>(now.time_since_epoch())
            .count();

    if (minutes % 60 == 0) { // 每小时整点更新
      if (!updatePerformanceSummary(host_id)) {
        std::cerr << "Failed to update performance summary for: " << host_name
                  << std::endl;
      }
    }
  }

  if (all_success) {
    std::cout << "Successfully stored data for " << data_copy.infos_size()
              << " hosts" << std::endl;
  }

  return all_success;
}

void PerformanceMonitorClient::calculateDerivedMetrics() {
  // 衍生指标在数据库层面通过计算列实现
  std::cout << "Derived metrics are calculated at database level" << std::endl;
}

void PerformanceMonitorClient::startMonitoring(int interval_seconds) {
  if (monitoring_active_) {
    std::cout << "Monitoring is already active" << std::endl;
    return;
  }

  monitoring_active_ = true;
  monitoring_thread_ = std::thread([this, interval_seconds]() {
    std::cout << "Monitoring thread started with interval " << interval_seconds
              << " seconds" << std::endl;

    while (monitoring_active_) {
      try {
        // 获取数据
        if (fetchMonitorData()) {
          // 存储到数据库
          if (!storeToDatabase()) {
            std::cerr << "Failed to store data to database" << std::endl;
          }
        } else {
          std::cerr << "Failed to fetch monitor data" << std::endl;
        }

        // 等待下一次采集
        for (int i = 0; i < interval_seconds && monitoring_active_; ++i) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }

      } catch (const std::exception &e) {
        std::cerr << "Monitoring error: " << e.what() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    }
    std::cout << "Monitoring thread stopped" << std::endl;
  });
}

void PerformanceMonitorClient::stopMonitoring() {
  if (!monitoring_active_)
    return;

  monitoring_active_ = false;
  if (monitoring_thread_.joinable()) {
    monitoring_thread_.join();
  }
  std::cout << "Monitoring stopped" << std::endl;
}

} // namespace yanhon
