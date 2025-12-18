#pragma once
#include "monitor/monitor_inter.hpp"
#include "net_monitor.skel.h"
#include <unordered_map>

namespace yanhon {
#define MAX_INTERFACES 32
struct NetInfo {
  std::string name;
  uint64_t rcv_bytes;
  uint64_t rcv_packets;
  uint64_t snd_bytes;
  uint64_t snd_packets;
  uint64_t err_in; // 新增: 接收错误计数，用于判断网卡/驱动是否异常
  uint64_t err_out;  // 新增: 发送错误计数，辅助判断链路质量
  uint64_t drop_in;  // 新增: 接收丢弃数，反映内核队列压力
  uint64_t drop_out; // 新增: 发送丢弃数，判断应用层处理能力
  std::chrono::steady_clock::time_point timepoint;
};

struct NetStat {
  std::string name;
  uint64_t rcv_bytes;
  uint64_t rcv_packets;
  uint64_t snd_bytes;
  uint64_t snd_packets;
  uint64_t err_in; // 新增: 接收错误计数，用于判断网卡/驱动是否异常
  uint64_t err_out;  // 新增: 发送错误计数，辅助判断链路质量
  uint64_t drop_in;  // 新增: 接收丢弃数，反映内核队列压力
  uint64_t drop_out; // 新增: 发送丢弃数，判断应用层处理能力
};

/// @brief eBPF Map 中存储的统计数据结构 (只关心流量和包数)
struct if_counters {
  __u64 rcv_bytes;
  __u64 rcv_packets;
  __u64 snd_bytes;
  __u64 snd_packets;
};

class NetMonitor : public MonitorInter {
public:
  NetMonitor();
  virtual ~NetMonitor();

  virtual void UpdateOnce(monitor::proto::MonitorInfo *monitor_info);

  virtual void Stop(){};

private:
  std::unordered_map<std::string, if_counters> ebpf_get_net_stats();
  // key: 网卡名
  std::unordered_map<std::string, NetInfo> last_net_info_;

  // key: ifindex, value: interface name
  std::unordered_map<int, std::string> ifindex_map;
  struct net_monitor_bpf *skel;
  // 存储TC钩子和选项的数组（按ifindex索引）
  struct bpf_tc_hook *tc_hooks_ingress = NULL;
  struct bpf_tc_hook *tc_hooks_egress = NULL;
  struct bpf_tc_opts *tc_opts_ingress = NULL;
  struct bpf_tc_opts *tc_opts_egress = NULL;
  bool *hooks_created_ingress = NULL;
  bool *hooks_created_egress = NULL;
  struct if_counters total = {0};
};
} // namespace yanhon
