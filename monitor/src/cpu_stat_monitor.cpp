#include "monitor/cpu_stat_monitor.hpp"
#include <fcntl.h>
#include <sys/mman.h>

namespace yanhon {
void CpuStatMonitor::UpdateOnce(monitor::proto::MonitorInfo *monitor_info) {
  int fd = open("/dev/cpu_stat_monitor", O_RDONLY);
  if (fd < 0) {
    std::cerr << "Failed to open /dev/cpu_stat_monitor: " << strerror(errno)
              << std::endl;
    return;
  }

  size_t stat_count = sysconf(_SC_NPROCESSORS_ONLN);
  // std::cout << "DEBUG: CPU count = " << stat_count << std::endl;
  size_t stat_size = sizeof(struct cpu_stat) * stat_count;
  void *addr = mmap(nullptr, stat_size, PROT_READ, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "mmap failed: " << strerror(errno) << std::endl;
    close(fd);
    return;
  }

  struct cpu_stat *stats = static_cast<struct cpu_stat *>(addr);

  for (int i = 0; i < stat_count; ++i) {
    if (stats[i].cpu_name[0] == '\0') {
      break;
    }
    auto it = cpu_stat_map_.find(stats[i].cpu_name);
    auto cpu_stat_msg = monitor_info->add_cpu_stat();

    if (it != cpu_stat_map_.end()) {
      struct cpu_stat old = it->second;
      // user字段不包含nice时间，两者都是用户态时间但分开统计
      float new_cpu_total_time =
          stats[i].user + stats[i].system + stats[i].idle + stats[i].nice +
          stats[i].io_wait + stats[i].irq + stats[i].soft_irq + stats[i].steal;
      float old_cpu_total_time = old.user + old.system + old.idle + old.nice +
                                 old.io_wait + old.irq + old.soft_irq +
                                 old.steal;
      float new_cpu_busy_time = stats[i].user + stats[i].system +
                                stats[i].nice + stats[i].irq +
                                stats[i].soft_irq + stats[i].steal;
      float old_cpu_busy_time =
          old.user + old.system + old.nice + old.irq + old.soft_irq + old.steal;
      if (new_cpu_total_time - old_cpu_total_time > 0) {
        float cpu_percent = (new_cpu_busy_time - old_cpu_busy_time) /
                            (new_cpu_total_time - old_cpu_total_time) * 100.00;
        float cpu_user_percent = (stats[i].user - old.user) /
                                 (new_cpu_total_time - old_cpu_total_time) *
                                 100.00;
        float cpu_system_percent = (stats[i].system - old.system) /
                                   (new_cpu_total_time - old_cpu_total_time) *
                                   100.00;
        float cpu_nice_percent = (stats[i].nice - old.nice) /
                                 (new_cpu_total_time - old_cpu_total_time) *
                                 100.00;
        float cpu_idle_percent = (stats[i].idle - old.idle) /
                                 (new_cpu_total_time - old_cpu_total_time) *
                                 100.00;
        float cpu_io_wait_percent = (stats[i].io_wait - old.io_wait) /
                                    (new_cpu_total_time - old_cpu_total_time) *
                                    100.00;
        float cpu_irq_percent = (stats[i].irq - old.irq) /
                                (new_cpu_total_time - old_cpu_total_time) *
                                100.00;
        float cpu_soft_irq_percent = (stats[i].soft_irq - old.soft_irq) /
                                     (new_cpu_total_time - old_cpu_total_time) *
                                     100.00;
        cpu_stat_msg->set_cpu_name(stats[i].cpu_name);
        cpu_stat_msg->set_cpu_percent(cpu_percent);
        cpu_stat_msg->set_usr_percent(cpu_user_percent);
        cpu_stat_msg->set_system_percent(cpu_system_percent);
        cpu_stat_msg->set_nice_percent(cpu_nice_percent);
        cpu_stat_msg->set_idle_percent(cpu_idle_percent);
        cpu_stat_msg->set_io_wait_percent(cpu_io_wait_percent);
        cpu_stat_msg->set_irq_percent(cpu_irq_percent);
        cpu_stat_msg->set_soft_irq_percent(cpu_soft_irq_percent);

      } else {
        // 避免除以零的情况，设置为0
        cpu_stat_msg->set_cpu_name(stats[i].cpu_name);
        cpu_stat_msg->set_cpu_percent(0.0);
        cpu_stat_msg->set_usr_percent(0.0);
        cpu_stat_msg->set_system_percent(0.0);
        cpu_stat_msg->set_nice_percent(0.0);
        cpu_stat_msg->set_idle_percent(0.0);
        cpu_stat_msg->set_io_wait_percent(0.0);
        cpu_stat_msg->set_irq_percent(0.0);
        cpu_stat_msg->set_soft_irq_percent(0.0);
      }

    } else {
      // 第一次采集数据，无法计算百分比，全部设置为0
      cpu_stat_msg->set_cpu_name(stats[i].cpu_name);
      cpu_stat_msg->set_cpu_percent(0.0);
      cpu_stat_msg->set_usr_percent(0.0);
      cpu_stat_msg->set_system_percent(0.0);
      cpu_stat_msg->set_nice_percent(0.0);
      cpu_stat_msg->set_idle_percent(0.0);
      cpu_stat_msg->set_io_wait_percent(0.0);
      cpu_stat_msg->set_irq_percent(0.0);
      cpu_stat_msg->set_soft_irq_percent(0.0);
    }
    cpu_stat_map_[stats[i].cpu_name] = stats[i];
  }
  munmap(addr, stat_size);
  close(fd);
}

} // namespace yanhon