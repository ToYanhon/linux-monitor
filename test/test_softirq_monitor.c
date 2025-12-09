#include <errno.h>
#include <fcntl.h>
#include <stdint.h> // 添加标准整数类型头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_CPU 128
#define MAX_CPU_NAME 16

// 必须与内核结构体完全一致
struct softirq_stat {
  char cpu_name[MAX_CPU_NAME];
  uint32_t hi;
  uint32_t timer;
  uint32_t net_tx;
  uint32_t net_rx;
  uint32_t block;
  uint32_t irq_poll;
  uint32_t tasklet;
  uint32_t sched;
  uint32_t hrtimer;
  uint32_t rcu;
} __attribute__((packed));

void print_header(void) {
  printf("%-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
         "CPU", "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "IRQ_POLL",
         "TASKLET", "SCHED", "HRTIMER", "RCU");
  printf("---------------------------------------------------------------------"
         "---------------------------------------------------\n");
}

void print_stats(struct softirq_stat *stats, const char *title) {
  printf("\n=== %s ===\n", title);
  print_header();

  for (int i = 0; i < MAX_CPU; i++) {
    // 检查CPU名称是否有效
    if (stats[i].cpu_name[0] == '\0' ||
        strncmp(stats[i].cpu_name, "cpu", 3) != 0) {
      continue;
    }

    printf("%-10s %-10u %-10u %-10u %-10u %-10u %-10u %-10u %-10u "
           "%-10u %-10u\n",
           stats[i].cpu_name, (uint32_t)stats[i].hi, (uint32_t)stats[i].timer,
           (uint32_t)stats[i].net_tx, (uint32_t)stats[i].net_rx,
           (uint32_t)stats[i].block, (uint32_t)stats[i].irq_poll,
           (uint32_t)stats[i].tasklet, (uint32_t)stats[i].sched,
           (uint32_t)stats[i].hrtimer, (uint32_t)stats[i].rcu);
  }
}

void calculate_delta(struct softirq_stat *prev, struct softirq_stat *curr,
                     struct softirq_stat *delta, int cpu) {
  strncpy(delta[cpu].cpu_name, curr[cpu].cpu_name, MAX_CPU_NAME);
  delta[cpu].hi = curr[cpu].hi - prev[cpu].hi;
  delta[cpu].timer = curr[cpu].timer - prev[cpu].timer;
  delta[cpu].net_tx = curr[cpu].net_tx - prev[cpu].net_tx;
  delta[cpu].net_rx = curr[cpu].net_rx - prev[cpu].net_rx;
  delta[cpu].block = curr[cpu].block - prev[cpu].block;
  delta[cpu].irq_poll = curr[cpu].irq_poll - prev[cpu].irq_poll;
  delta[cpu].tasklet = curr[cpu].tasklet - prev[cpu].tasklet;
  delta[cpu].sched = curr[cpu].sched - prev[cpu].sched;
  delta[cpu].hrtimer = curr[cpu].hrtimer - prev[cpu].hrtimer;
  delta[cpu].rcu = curr[cpu].rcu - prev[cpu].rcu;
}

int main(int argc, char *argv[]) {
  int fd;
  struct softirq_stat *mapped_stats;
  struct softirq_stat *prev_stats;
  int iterations = 10;
  int interval = 2;
  size_t buffer_size;

  printf("CPU SoftIRQ Monitor Test Program\n");
  printf("================================\n\n");

  // 打开设备文件
  fd = open("/dev/cpu_softirq_monitor", O_RDONLY);
  if (fd < 0) {
    perror("Failed to open /dev/cpu_softirq_monitor");
    printf("Try: sudo insmod cpu_softirq_monitor.ko\n");
    return 1;
  }

  buffer_size = sizeof(struct softirq_stat) * MAX_CPU;
  printf("Struct size: %lu bytes\n", sizeof(struct softirq_stat));
  printf("Total buffer size: %lu bytes\n", buffer_size);

  // 映射内存
  mapped_stats = mmap(NULL, buffer_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapped_stats == MAP_FAILED) {
    perror("mmap failed");
    close(fd);
    return 1;
  }

  printf("Memory mapped successfully at %p\n\n", mapped_stats);

  // 分配空间存储前一次的快照
  prev_stats = malloc(buffer_size);
  if (!prev_stats) {
    perror("malloc failed");
    munmap(mapped_stats, buffer_size);
    close(fd);
    return 1;
  }

  // 初始快照
  memcpy(prev_stats, mapped_stats, buffer_size);
  print_stats(prev_stats, "Initial State");

  for (int i = 0; i < iterations; i++) {
    sleep(interval);

    // 计算增量
    struct softirq_stat delta[MAX_CPU];
    memset(delta, 0, buffer_size);

    for (int j = 0; j < MAX_CPU; j++) {
      if (mapped_stats[j].cpu_name[0] == '\0')
        continue;
      calculate_delta(prev_stats, mapped_stats, delta, j);
    }

    char title[64];
    snprintf(title, sizeof(title),
             "Incremental Stats (Last %d seconds) - Iteration %d", interval,
             i + 1);
    print_stats(delta, title);

    // 保存当前快照
    memcpy(prev_stats, mapped_stats, buffer_size);

    // 验证数据
    printf("\nData validation for cpu0:\n");
    if (mapped_stats[0].cpu_name[0] != '\0') {
      printf("  cpu_name: %s\n", mapped_stats[0].cpu_name);
      printf("  timer: %lu (prev: %lu, delta: %lu)\n",
             (unsigned long)mapped_stats[0].timer,
             (unsigned long)prev_stats[0].timer, (unsigned long)delta[0].timer);
    }
  }

  // 清理
  free(prev_stats);
  munmap(mapped_stats, buffer_size);
  close(fd);

  printf("\nTest completed successfully\n");
  return 0;
}