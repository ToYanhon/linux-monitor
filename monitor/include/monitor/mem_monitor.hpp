#pragma once
#include "monitor/monitor_inter.hpp"
#include <cstdint>
#include <string>

namespace yanhon {
/**
 * @struct MenInfo
 * @brief 系统内存信息结构体，存储从 /proc/meminfo 解析的各类内存统计数据
 * 所有字段单位为 KB
 */
struct MenInfo {
  int64_t total;         /**< 内存总量 */
  int64_t free;          /**< 未被使用的内存 */
  int64_t avail;         /**< 可用内存（包括可回收的缓存） */
  int64_t buffers;       /**< 用于文件系统缓冲区的内存 */
  int64_t cached;        /**< 页面缓存的内存（不包括交换缓存） */
  int64_t swap_cached;   /**< 已交换出去但仍在交换缓存中的内存 */
  int64_t active;        /**< 活跃内存总量 */
  int64_t in_active;     /**< 非活跃内存总量 */
  int64_t active_anon;   /**< 活跃匿名内存（进程堆栈等） */
  int64_t inactive_anon; /**< 非活跃匿名内存 */
  int64_t active_file;   /**< 活跃文件相关内存（文件页、缓存等） */
  int64_t inactive_file; /**< 非活跃文件相关内存 */
  int64_t dirty;         /**< 待写入磁盘的脏页内存 */
  int64_t writeback;     /**< 正在写入磁盘的内存 */
  int64_t anon_pages;    /**< 匿名页内存（不与任何文件关联） */
  int64_t mapped;        /**< 被映射到用户进程的内存 */
  int64_t kReclaimable;  /**< 内核可回收内存（如 slab 缓存） */
  int64_t sReclaimable;  /**< 可回收的 slab 内存 */
  int64_t sUnreclaim;    /**< 不可回收的 slab 内存 */
};

/**
 * @class MemMonitor
 * @brief 内存监控器，继承自 MonitorInter 接口
 * 负责定时读取系统内存信息并将其转换为 protobuf 格式
 */
class MemMonitor : public MonitorInter {
public:
  /** @brief 构造函数 */
  MemMonitor() {}

  /** @brief 析构函数 */
  ~MemMonitor() {}

  /**
   * @brief 执行一次内存监控数据更新
   * @param monitor_info 指向 protobuf MonitorInfo 对象的指针，用于存储监控结果
   * @details 读取当前系统内存统计信息，填充到 monitor_info 中
   */
  void UpdateOnce(monitor::proto::MonitorInfo *monitor_info) override;

  /**
   * @brief 停止内存监控
   * @details 清理监控相关资源，停止后续的更新操作
   */
  void Stop() override {}

private:
  /** @brief 私有成员区域（可扩展用于存储监控状态） */
};
} // namespace yanhon