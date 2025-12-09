#pragma once
#include "monitor/monitor_inter.hpp"

namespace yanhon {
struct NetInfo {
  std::string name;
  uint64_t rcv_bytes;
  uint64_t rcv_packets;
  uint64_t snd_bytes;
  uint64_t snd_packets;
  uint64_t err_in;   // 新增: 接收错误计数，用于判断网卡/驱动是否异常
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
  uint64_t err_in;   // 新增: 接收错误计数，用于判断网卡/驱动是否异常
  uint64_t err_out;  // 新增: 发送错误计数，辅助判断链路质量
  uint64_t drop_in;  // 新增: 接收丢弃数，反映内核队列压力
  uint64_t drop_out; // 新增: 发送丢弃数，判断应用层处理能力
};

// struct NetStatsValue {
//   uint64_t rcv_bytes;
//   uint64_t rcv_packets;
//   uint64_t snd_bytes;
//   uint64_t snd_packets;
//   uint64_t err_in;
//   uint64_t err_out;
//   uint64_t drop_in;
//   uint64_t drop_out;
// };

class NetMonitor : public MonitorInter {
public:
  NetMonitor() {}
  virtual ~NetMonitor() {}

  virtual void UpdateOnce(monitor::proto::MonitorInfo *monitor_info);

  virtual void Stop() {};

private:
  // key: 网卡名
  std::unordered_map<std::string, NetInfo> last_net_info_;
};
} // namespace yanhon