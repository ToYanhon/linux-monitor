#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched/loadavg.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#error "This module requires Linux kernel version 5.6 or later"
#endif

struct cpu_load {
  unsigned long load_avg_1;  /* avenrun[0] (fixed-point) */
  unsigned long load_avg_3;  /* avenrun[1] (fixed-point) */
  unsigned long load_avg_15; /* avenrun[2] (fixed-point) */
};

static struct cpu_load *g_cpu_load = NULL;

static void update_cpu_load(struct cpu_load *info) {
  if (!info)
    return;
  info->load_avg_1 = avenrun[0];
  info->load_avg_3 = avenrun[1];
  info->load_avg_15 = avenrun[2];
}

static int cpu_load_monitor_mmap(struct file *filp,
                                 struct vm_area_struct *vma) {
  unsigned long size = sizeof(struct cpu_load);
  unsigned long pfn;
  if ((vma->vm_end - vma->vm_start) < size)
    return -EINVAL;
  // 移除 update_cpu_load 调用,因为定时器会定期更新
  pfn = vmalloc_to_pfn(g_cpu_load);
  return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static const struct file_operations cpu_load_monitor_fops = {
    .owner = THIS_MODULE,
    .mmap = cpu_load_monitor_mmap,
};

static struct miscdevice cpu_load_monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "cpu_load_monitor",
    .fops = &cpu_load_monitor_fops,
    .mode = 0444,
};

static struct hrtimer cpu_load_timer;
static ktime_t ktime;
#define UPDATE_INTERVAL_NS 1000000000L // 1秒 = 1000000000 纳秒

static enum hrtimer_restart cpu_load_timer_callback(struct hrtimer *timer) {
  update_cpu_load(g_cpu_load);
  hrtimer_forward_now(timer, ktime);
  return HRTIMER_RESTART;
}

static int __init cpu_load_monitor_init(void) {
  g_cpu_load = vmalloc(sizeof(struct cpu_load));
  if (!g_cpu_load)
    return -ENOMEM;

  // 初始化并启动定时器
  ktime = ktime_set(0, UPDATE_INTERVAL_NS);
  hrtimer_init(&cpu_load_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  cpu_load_timer.function = &cpu_load_timer_callback;
  hrtimer_start(&cpu_load_timer, ktime, HRTIMER_MODE_REL);

  misc_register(&cpu_load_monitor_dev);
  printk(KERN_INFO "cpu_load_monitor device registered\n");
  return 0;
}

static void __exit cpu_load_monitor_exit(void) {
  hrtimer_cancel(&cpu_load_timer);
  misc_deregister(&cpu_load_monitor_dev);
  if (g_cpu_load)
    vfree(g_cpu_load);
  printk(KERN_INFO "cpu_load_monitor device unregistered\n");
}

module_init(cpu_load_monitor_init);
module_exit(cpu_load_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yanhon");
MODULE_DESCRIPTION("CPU Load Monitor Module with mmap support");
