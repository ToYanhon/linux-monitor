#pragma once

#include "monitor/monitor_inter.hpp"
#include <string>
#include <unordered_map>

namespace yanhon {
using u64 = unsigned long long;
struct cpu_stat {
  char cpu_name[16];
  u64 user;
  u64 system;
  u64 idle;
  u64 nice;
  u64 io_wait;
  u64 irq;
  u64 soft_irq;
  u64 steal;
  u64 guest;
  u64 guest_nice;
  u64 total;
  u64 timestamp; // 添加时间戳
};

class CpuStatMonitor : public MonitorInter {

public:
  CpuStatMonitor() {}
  ~CpuStatMonitor() {}
  void UpdateOnce(monitor::proto::MonitorInfo *monitor_info) override;
  void Stop() {}

private:
  std::unordered_map<std::string, struct cpu_stat> cpu_stat_map_;
};
} // namespace yanhon