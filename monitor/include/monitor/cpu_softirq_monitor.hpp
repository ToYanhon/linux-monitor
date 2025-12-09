#pragma once
#include "monitor/monitor_inter.hpp"
#include <chrono>
#include <string>
#include <unordered_map>

namespace yanhon {
using __u32 = unsigned int;
struct softirq_stat {
  char cpu_name[16];
  __u32 hi;
  __u32 timer;
  __u32 net_tx;
  __u32 net_rx;
  __u32 block;
  __u32 irq_poll;
  __u32 tasklet;
  __u32 sched;
  __u32 hrtimer;
  __u32 rcu;
} __attribute__((packed));

class CpuSoftIrqMonitor : public MonitorInter {

public:
  CpuSoftIrqMonitor() {}
  ~CpuSoftIrqMonitor() {}
  void UpdateOnce(monitor::proto::MonitorInfo *monitor_info) override;
  void Stop() override {}
};
} // namespace yanhon