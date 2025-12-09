#pragma once
#include "monitor/monitor_inter.hpp"
#include <cstdint>
#include <map>

namespace yanhon {
struct DiskInfo {
  std::string name;
  uint64_t reads, writes, sectors_read, sectors_written;
  uint64_t read_time_ms, write_time_ms, io_in_progress, io_time_ms,
      weighted_io_time_ms;
};
class DiskMonitor : public MonitorInter {
public:
  DiskMonitor() {}
  ~DiskMonitor() {}
  void UpdateOnce(monitor::proto::MonitorInfo *monitor_info) override;
  void Stop() override {}

private:
  std::map<std::string, DiskInfo> previous_disk_stats_;
  std::map<std::string, double> last_time;
};
} // namespace yanhon