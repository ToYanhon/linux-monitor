#include "monitor/cpu_load_monitor.hpp"
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

#ifndef FIXED_1
#define FSHIFT 11
#define FIXED_1 (1 << FSHIFT)
#endif

namespace yanhon {
void CpuLoadMonitor::UpdateOnce(monitor::proto::MonitorInfo *monitor_info) {
  int fd = open("/dev/cpu_load_monitor", O_RDONLY);
  if (fd < 0) {
    return;
  }

  size_t data_size = sizeof(struct cpu_load);
  void *addr = mmap(nullptr, data_size, PROT_READ, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    close(fd);
    return;
  }

  struct cpu_load info;
  memcpy(&info, addr, data_size);

  auto cpu_load_msg = monitor_info->mutable_cpu_load();
  cpu_load_msg->set_load_avg_1((float)info.load_avg_1 / FIXED_1);
  cpu_load_msg->set_load_avg_3((float)info.load_avg_3 / FIXED_1);
  cpu_load_msg->set_load_avg_15((float)info.load_avg_15 / FIXED_1);

  munmap(addr, data_size);
  close(fd);
}
} // namespace yanhon
