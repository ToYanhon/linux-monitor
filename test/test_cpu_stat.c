#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/cpu_stat_monitor"
#define MAX_CPU 128

// 与内核模块中相同的结构体定义
struct cpu_stat {
  char cpu_name[16];
  unsigned long long user;
  unsigned long long system;
  unsigned long long idle;
  unsigned long long nice;
  unsigned long long io_wait;
  unsigned long long irq;
  unsigned long long soft_irq;
  unsigned long long steal;
  unsigned long long guest;
  unsigned long long guest_nice;
  unsigned long long total;
  unsigned long long timestamp;
};

// 测试mmap功能
void test_mmap_method(void) {
  int fd;
  struct cpu_stat *stats;
  struct stat st;
  int nr_cpus;
  int i;

  printf("=== Testing mmap method ===\n");

  // 打开设备文件
  fd = open(DEVICE_PATH, O_RDONLY);
  if (fd < 0) {
    perror("Failed to open device");
    return;
  }

  // 获取设备文件状态
  if (fstat(fd, &st) < 0) {
    perror("Failed to stat device");
    close(fd);
    return;
  }

  // 映射内存
  stats = (struct cpu_stat *)mmap(NULL, sizeof(struct cpu_stat) * MAX_CPU,
                                  PROT_READ, MAP_SHARED, fd, 0);
  if (stats == MAP_FAILED) {
    perror("Failed to mmap device");
    close(fd);
    return;
  }

  // 获取CPU数量（实际在线的）
  nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  printf("System has %d online CPUs\n", nr_cpus);

  // 打印CPU统计信息
  for (i = 0; i < nr_cpus && i < MAX_CPU; i++) {
    struct cpu_stat *cpu = &stats[i];
    unsigned long long used = cpu->user + cpu->nice + cpu->system;
    double usage = 0.0;

    if (cpu->total > 0) {
      usage = (double)used * 100.0 / cpu->total;
    }

    printf("CPU %s:\n", cpu->cpu_name);
    printf("  User:     %llu\n", cpu->user);
    printf("  Nice:     %llu\n", cpu->nice);
    printf("  System:   %llu\n", cpu->system);
    printf("  Idle:     %llu\n", cpu->idle);
    printf("  IOWait:   %llu\n", cpu->io_wait);
    printf("  IRQ:      %llu\n", cpu->irq);
    printf("  SoftIRQ:  %llu\n", cpu->soft_irq);
    printf("  Total:    %llu\n", cpu->total);
    printf("  Usage:    %.2f%%\n", usage);
    printf("  Timestamp:%llu\n", cpu->timestamp);
    printf("\n");
  }

  // 测试实时更新
  printf("Testing real-time updates (waiting 3 seconds)...\n");
  sleep(3);

  printf("After 3 seconds:\n");
  for (i = 0; i < nr_cpus && i < MAX_CPU; i++) {
    struct cpu_stat *cpu = &stats[i];
    printf("%s: Total=%llu, Timestamp=%llu\n", cpu->cpu_name, cpu->total,
           cpu->timestamp);
  }

  // 取消映射
  munmap(stats, sizeof(struct cpu_stat) * MAX_CPU);
  close(fd);
}

// // 测试read功能
// void test_read_method(void) {
//   int fd;
//   struct cpu_stat stats[MAX_CPU];
//   ssize_t bytes_read;
//   int nr_cpus;
//   int i;

//   printf("\n=== Testing read method ===\n");

//   fd = open(DEVICE_PATH, O_RDONLY);
//   if (fd < 0) {
//     perror("Failed to open device");
//     return;
//   }

//   bytes_read = read(fd, stats, sizeof(stats));
//   if (bytes_read < 0) {
//     perror("Failed to read device");
//     close(fd);
//     return;
//   }

//   nr_cpus = bytes_read / sizeof(struct cpu_stat);
//   printf("Read %ld bytes, %d CPU records\n", bytes_read, nr_cpus);

//   for (i = 0; i < nr_cpus; i++) {
//     struct cpu_stat *cpu = &stats[i];
//     printf("CPU %d: user=%llu, system=%llu, idle=%llu\n", i, cpu->user,
//            cpu->system, cpu->idle);
//   }

//   close(fd);
// }

int main(int argc, char *argv[]) {
  printf("CPU Stat Monitor Test Program\n");
  printf("==============================\n\n");

  // 检查设备是否存在
  if (access(DEVICE_PATH, F_OK) != 0) {
    printf("Device %s not found. Make sure the kernel module is loaded.\n",
           DEVICE_PATH);
    printf("Try: sudo insmod cpu_stat_module.ko\n");
    return 1;
  }

  // 测试mmap方法
  test_mmap_method();

  // 测试read方法
  test_read_method();

  printf("\n=== Test completed successfully ===\n");
  return 0;
}