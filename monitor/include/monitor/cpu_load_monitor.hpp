#pragma once
#include "monitor/monitor_inter.hpp"

namespace yanhon {
struct cpu_load {
  unsigned long load_avg_1;
  unsigned long load_avg_3;
  unsigned long load_avg_15;
};

class CpuLoadMonitor : public MonitorInter {
public:
  CpuLoadMonitor() {}
  ~CpuLoadMonitor() {}
  void UpdateOnce(monitor::proto::MonitorInfo *monitor_info);
  void Stop() override {}
};
} // namespace yanhon