#include "monitor/cpu_softirq_monitor.hpp"
#include <fcntl.h>
#include <sys/mman.h>

namespace yanhon {
void CpuSoftIrqMonitor::UpdateOnce(monitor::proto::MonitorInfo *monitor_info) {
  int fd = open("/dev/cpu_softirq_monitor", O_RDONLY);
  if (fd < 0)
    return;

  size_t stat_count = 128; // 假设最多128个CPU
  size_t stat_size = sizeof(struct softirq_stat) * stat_count;
  void *addr = mmap(nullptr, stat_size, PROT_READ, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    close(fd);
    return;
  }

  struct softirq_stat *stats = static_cast<struct softirq_stat *>(addr);

  for (size_t i = 0; i < stat_count; ++i) {
    if (stats[i].cpu_name[0] == '\0') {
      break;
    }
    auto one_softirq_msg = monitor_info->add_soft_irq();
    one_softirq_msg->set_cpu(stats[i].cpu_name);
    one_softirq_msg->set_hi(stats[i].hi);
    one_softirq_msg->set_timer(stats[i].timer);
    one_softirq_msg->set_net_tx(stats[i].net_tx);
    one_softirq_msg->set_net_rx(stats[i].net_rx);
    one_softirq_msg->set_block(stats[i].block);
    one_softirq_msg->set_irq_poll(stats[i].irq_poll);
    one_softirq_msg->set_tasklet(stats[i].tasklet);
    one_softirq_msg->set_sched(stats[i].sched);
    one_softirq_msg->set_hrtimer(stats[i].hrtimer);
    one_softirq_msg->set_rcu(stats[i].rcu);
  }

  munmap(addr, stat_size);
  close(fd);
}
} // namespace yanhon