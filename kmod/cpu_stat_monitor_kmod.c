#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#error "This module requires Linux kernel version 5.6 or later"
#endif

#define MAX_CPU 128
#define DEVICE_NAME "cpu_stat_monitor"
#define UPDATE_INTERVAL_NS 1000000000L // 1秒 = 1000000000 纳秒

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yanhon");
MODULE_DESCRIPTION("CPU Stat Monitor Module with mmap support");

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

static struct cpu_stat *g_cpu_stats = NULL;
static struct hrtimer cpu_stat_timer;
static ktime_t ktime_interval;
static DEFINE_MUTEX(stat_lock); // 添加互斥锁保护数据

static void update_cpu_stats(void) {
  int cpu;
  struct kernel_cpustat *kcs;

  mutex_lock(&stat_lock);

  for_each_online_cpu(cpu) {
    kcs = &kcpustat_cpu(cpu);

    /**
     * Copy CPU statistics from kernel CPU stat structure to global CPU stats
     * array
     *
     * This code block copies all CPU time accounting metrics from the kernel's
     * cpu_stat structure (kcs) to the corresponding fields in the global
     * g_cpu_stats array for the specified CPU index.
     *
     * The following metrics are captured:
     * - user: Time spent in user mode
     * - nice: Time spent in user mode with low priority (nice)
     * - system: Time spent in kernel mode
     * - idle: Time spent idle
     * - io_wait: Time spent waiting for I/O to complete
     * - irq: Time spent servicing hardware interrupts
     * - soft_irq: Time spent servicing software interrupts
     * - steal: Time spent in other operating systems (for virtualized
     * environments)
     * - guest: Time spent running a virtual CPU for a guest OS
     * - guest_nice: Time spent running a virtual CPU for a guest OS with low
     * priority
     *
     * @param cpu Index of the CPU core in the g_cpu_stats array
     * @param kcs Pointer to the kernel cpu_stat structure containing current
     * CPU metrics
     */
    g_cpu_stats[cpu].user = kcs->cpustat[CPUTIME_USER];
    g_cpu_stats[cpu].nice = kcs->cpustat[CPUTIME_NICE];
    g_cpu_stats[cpu].system = kcs->cpustat[CPUTIME_SYSTEM];
    g_cpu_stats[cpu].idle = kcs->cpustat[CPUTIME_IDLE];
    g_cpu_stats[cpu].io_wait = kcs->cpustat[CPUTIME_IOWAIT];
    g_cpu_stats[cpu].irq = kcs->cpustat[CPUTIME_IRQ];
    g_cpu_stats[cpu].soft_irq = kcs->cpustat[CPUTIME_SOFTIRQ];
    g_cpu_stats[cpu].steal = kcs->cpustat[CPUTIME_STEAL];
    // 表示虚拟化环境的CPU时间
    g_cpu_stats[cpu].guest = kcs->cpustat[CPUTIME_GUEST];
    g_cpu_stats[cpu].guest_nice = kcs->cpustat[CPUTIME_GUEST_NICE];

    // 计算总时间
    g_cpu_stats[cpu].total = g_cpu_stats[cpu].user + g_cpu_stats[cpu].nice +
                             g_cpu_stats[cpu].system + g_cpu_stats[cpu].idle +
                             g_cpu_stats[cpu].io_wait + g_cpu_stats[cpu].irq +
                             g_cpu_stats[cpu].soft_irq +
                             g_cpu_stats[cpu].steal + g_cpu_stats[cpu].guest +
                             g_cpu_stats[cpu].guest_nice;

    // 设置CPU名称
    snprintf(g_cpu_stats[cpu].cpu_name, sizeof(g_cpu_stats[cpu].cpu_name),
             "CPU%d", cpu);

    // 更新时间戳（纳秒）
    g_cpu_stats[cpu].timestamp = ktime_get_ns();
  }

  mutex_unlock(&stat_lock);
}

static enum hrtimer_restart cpu_stat_timer_callback(struct hrtimer *timer) {
  update_cpu_stats();
  hrtimer_forward_now(timer, ktime_interval);
  return HRTIMER_RESTART;
}

static int cpu_stat_monitor_mmap(struct file *filp,
                                 struct vm_area_struct *vma) {
  unsigned long size = sizeof(struct cpu_stat) * MAX_CPU;
  unsigned long pfn;
  int ret;

  // 如果用户请求的大小小于我们的大小，也允许，但限制在请求的大小
  unsigned long request_size = vma->vm_end - vma->vm_start;
  if (request_size == 0) {
    printk(KERN_ERR "cpu_stat_monitor: Requested size is 0\n");
    return -EINVAL;
  }

  // 使用较小的那个大小
  unsigned long map_size = (request_size < size) ? request_size : size;

  printk(KERN_INFO "cpu_stat_monitor: Requested size=%lu, Mapping size=%lu\n",
         request_size, map_size);

  // 使用vmalloc_to_pfn获取正确的页帧号
  pfn = vmalloc_to_pfn(g_cpu_stats);
  if (!pfn) {
    printk(KERN_ERR "cpu_stat_monitor: Failed to get pfn\n");
    return -EINVAL;
  }

  ret = remap_pfn_range(vma, vma->vm_start, pfn, map_size, vma->vm_page_prot);
  if (ret) {
    printk(KERN_ERR "cpu_stat_monitor: remap_pfn_range failed: %d\n", ret);
    return ret;
  }

  printk(KERN_INFO
         "cpu_stat_monitor: mmap successful: vma_start=%lx, size=%lu\n",
         vma->vm_start, map_size);

  return 0;
}

// // 添加read操作以便通过常规read测试
// static ssize_t cpu_stat_monitor_read(struct file *filp, char __user *buf,
//                                      size_t count, loff_t *ppos) {
//   int nr_cpus = num_online_cpus();
//   size_t data_size = sizeof(struct cpu_stat) * nr_cpus;

//   mutex_lock(&stat_lock);

//   if (*ppos >= data_size) {
//     mutex_unlock(&stat_lock);
//     return 0; // EOF
//   }

//   if (count > data_size - *ppos)
//     count = data_size - *ppos;

//   if (copy_to_user(buf, (char *)g_cpu_stats + *ppos, count)) {
//     mutex_unlock(&stat_lock);
//     return -EFAULT;
//   }

//   *ppos += count;
//   mutex_unlock(&stat_lock);

//   return count;
// }

static const struct file_operations cpu_stat_monitor_fops = {
    .owner = THIS_MODULE,
    .mmap = cpu_stat_monitor_mmap,
    // .read = cpu_stat_monitor_read,
};

static struct miscdevice cpu_stat_monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &cpu_stat_monitor_fops,
    .mode = 0666, // 允许读写
};

static int __init cpu_stat_monitor_init(void) {
  int i;

  printk(KERN_INFO "cpu_stat_monitor: Initializing module\n");

  // 分配内存并初始化
  g_cpu_stats = vmalloc(sizeof(struct cpu_stat) * MAX_CPU);
  if (!g_cpu_stats) {
    printk(KERN_ERR "cpu_stat_monitor: Failed to allocate memory\n");
    return -ENOMEM;
  }

  // 初始化数据结构
  memset(g_cpu_stats, 0, sizeof(struct cpu_stat) * MAX_CPU);
  for (i = 0; i < MAX_CPU; i++) {
    snprintf(g_cpu_stats[i].cpu_name, sizeof(g_cpu_stats[i].cpu_name), "CPU%d",
             i);
  }

  // 初始化定时器
  ktime_interval = ktime_set(0, UPDATE_INTERVAL_NS);
  hrtimer_init(&cpu_stat_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  cpu_stat_timer.function = &cpu_stat_timer_callback;

  // 先更新一次数据
  update_cpu_stats();

  // 启动定时器
  hrtimer_start(&cpu_stat_timer, ktime_interval, HRTIMER_MODE_REL);

  // 注册设备
  if (misc_register(&cpu_stat_monitor_dev)) {
    vfree(g_cpu_stats);
    printk(KERN_ERR "cpu_stat_monitor: Failed to register device\n");
    return -ENODEV;
  }

  printk(KERN_INFO "cpu_stat_monitor: Device registered at /dev/%s\n",
         DEVICE_NAME);
  printk(KERN_INFO "cpu_stat_monitor: Number of online CPUs: %d\n",
         num_online_cpus());

  return 0;
}

static void __exit cpu_stat_monitor_exit(void) {
  hrtimer_cancel(&cpu_stat_timer);
  misc_deregister(&cpu_stat_monitor_dev);

  if (g_cpu_stats) {
    vfree(g_cpu_stats);
  }

  printk(KERN_INFO "cpu_stat_monitor: Module unloaded\n");
}

module_init(cpu_stat_monitor_init);
module_exit(cpu_stat_monitor_exit);