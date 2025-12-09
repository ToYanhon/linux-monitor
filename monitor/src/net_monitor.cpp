#include "monitor/net_monitor.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <net/if.h>
#include <sstream>
#include <string>
#include <vector>
#include <bcc/BPF.h>
#include <unistd.h>

namespace yanhon {

// 假设 NetStat 和 NetInfo 结构体已在 net_monitor.hpp 中定义，并包含以下字段：
// struct NetStat {
//     std::string name;
//     uint64_t rcv_bytes = 0;
//     uint64_t rcv_packets = 0;
//     uint64_t err_in = 0;
//     uint64_t drop_in = 0;
//     uint64_t snd_bytes = 0;
//     uint64_t snd_packets = 0;
//     uint64_t err_out = 0;
//     uint64_t drop_out = 0;
// };
// class NetMonitor { ... private: std::map<std::string, NetInfo>
// last_net_info_; ... }; 假设 monitor::proto::MonitorInfo 是 protobuf 类型，有
// add_net_info() 方法。

/**
 * @brief 检查是否为虚拟接口
 * @param ifname 接口名称
 * @return 如果是虚拟接口返回 true
 */
static bool is_virtual_interface(const std::string &ifname) {
  return ifname == "lo" || ifname.find("veth") == 0 ||
         ifname.find("docker") == 0 || ifname.find("br") == 0 ||
         ifname.find("virbr") == 0 || ifname.find("tun") == 0 ||
         ifname.find("tap") == 0;
}

// ----------------------------------------------------------------------
// eBPF 模拟部分：负责 bytes 和 packets
// ----------------------------------------------------------------------

/// @brief eBPF Map 中存储的统计数据结构 (只关心流量和包数)
struct EbpfStats {
  uint64_t rcv_bytes;
  uint64_t rcv_packets;
  uint64_t snd_bytes;
  uint64_t snd_packets;
};

/**
 * @brief 从 eBPF Map 读取所有网络接口的 bytes 和 packets 统计信息
 * * !!! 注意：此函数内部使用 /proc/net/dev 模拟 eBPF 的数据获取。
 * !!! 在实际项目中，应替换为 eBPF Map (如 BPF_MAP_TYPE_HASH) 的读取逻辑。
 * * @return 包含接口名和 bytes/packets 统计数据的 map
 */
static std::map<std::string, EbpfStats> ebpf_get_net_stats() {
  if (geteuid() != 0) {
    std::map<std::string, EbpfStats> stats_map;
    std::ifstream file("/proc/net/dev");
    if (file.is_open()) {
      std::string line;
      std::getline(file, line);
      std::getline(file, line);
      while (std::getline(file, line)) {
        char ifname_c[IF_NAMESIZE + 1];
        uint64_t rcv_bytes, rcv_packets, rcv_errs, rcv_drop, rcv_fifo, rcv_frame,
            rcv_compressed, rcv_multicast;
        uint64_t snd_bytes, snd_packets, snd_errs, snd_drop, snd_fifo, snd_colls,
            snd_carrier, snd_compressed;
        int ret = std::sscanf(
            line.c_str(),
            " %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
            ifname_c, &rcv_bytes, &rcv_packets, &rcv_errs, &rcv_drop, &rcv_fifo,
            &rcv_frame, &rcv_compressed, &rcv_multicast, &snd_bytes, &snd_packets,
            &snd_errs, &snd_drop, &snd_fifo, &snd_colls, &snd_carrier,
            &snd_compressed);
        if (ret >= 17) {
          std::string ifname = ifname_c;
          if (!ifname.empty() && ifname.back() == ':') { ifname.pop_back(); }
          if (!is_virtual_interface(ifname)) {
            stats_map[ifname] = {rcv_bytes, rcv_packets, snd_bytes, snd_packets};
          }
        }
      }
    }
    return stats_map;
  }
  static std::unique_ptr<ebpf::BPF> bpf;
  static bool initialized = false;
  std::map<std::string, EbpfStats> stats_map;

  if (!initialized) {
    const std::string program = R"(
      #include <uapi/linux/ptrace.h>
      #include <linux/skbuff.h>
      #include <linux/netdevice.h>
      struct val_t { u64 rcv_bytes; u64 rcv_packets; u64 snd_bytes; u64 snd_packets; };
      BPF_HASH(ifstats, u32, struct val_t, 1024);

      static __always_inline int update_rcv(struct sk_buff *skb) {
        struct net_device *dev = NULL;
        bpf_probe_read_kernel(&dev, sizeof(dev), &skb->dev);
        if (!dev) return 0;
        u32 ifindex = 0; u32 len = 0;
        bpf_probe_read_kernel(&ifindex, sizeof(ifindex), &dev->ifindex);
        bpf_probe_read_kernel(&len, sizeof(len), &skb->len);
        struct val_t *val = ifstats.lookup(&ifindex);
        if (val) { val->rcv_bytes += len; val->rcv_packets += 1; }
        else { struct val_t zero = {}; zero.rcv_bytes = len; zero.rcv_packets = 1; ifstats.update(&ifindex, &zero); }
        return 0;
      }

      static __always_inline int update_snd(struct sk_buff *skb) {
        struct net_device *dev = NULL;
        bpf_probe_read_kernel(&dev, sizeof(dev), &skb->dev);
        if (!dev) return 0;
        u32 ifindex = 0; u32 len = 0;
        bpf_probe_read_kernel(&ifindex, sizeof(ifindex), &dev->ifindex);
        bpf_probe_read_kernel(&len, sizeof(len), &skb->len);
        struct val_t *val = ifstats.lookup(&ifindex);
        if (val) { val->snd_bytes += len; val->snd_packets += 1; }
        else { struct val_t zero = {}; zero.snd_bytes = len; zero.snd_packets = 1; ifstats.update(&ifindex, &zero); }
        return 0;
      }

      int trace_netif_receive_skb(struct pt_regs *ctx) {
        struct sk_buff *skb = (struct sk_buff *)PT_REGS_PARM1(ctx);
        if (!skb) return 0;
        return update_rcv(skb);
      }

      int trace_dev_queue_xmit(struct pt_regs *ctx) {
        struct sk_buff *skb = (struct sk_buff *)PT_REGS_PARM1(ctx);
        if (!skb) return 0;
        return update_snd(skb);
      }
    )";

    bpf.reset(new ebpf::BPF());
    auto st = bpf->init(program);
    if (!st.ok()) {
      return stats_map;
    }
    st = bpf->attach_kprobe("netif_receive_skb", "trace_netif_receive_skb");
    if (!st.ok()) {
      bpf->attach_kprobe("netif_receive_skb_core", "trace_netif_receive_skb");
    }
    st = bpf->attach_kprobe("dev_queue_xmit", "trace_dev_queue_xmit");
    if (!st.ok()) {
      return stats_map;
    }
    initialized = true;
  }

  struct Val { uint64_t rcv_bytes; uint64_t rcv_packets; uint64_t snd_bytes; uint64_t snd_packets; };
  auto table = bpf->get_hash_table<uint32_t, Val>("ifstats");
  std::vector<std::pair<uint32_t, Val>> entries = table.get_table_offline();
  if (entries.empty()) {
    // 回退：当 eBPF 不可用或尚未采集到数据时，使用 /proc 获取 bytes/packets
    std::ifstream file("/proc/net/dev");
    if (file.is_open()) {
      std::string line;
      std::getline(file, line);
      std::getline(file, line);
      while (std::getline(file, line)) {
        char ifname_c[IF_NAMESIZE + 1];
        uint64_t rcv_bytes, rcv_packets, rcv_errs, rcv_drop, rcv_fifo, rcv_frame,
            rcv_compressed, rcv_multicast;
        uint64_t snd_bytes, snd_packets, snd_errs, snd_drop, snd_fifo, snd_colls,
            snd_carrier, snd_compressed;
        int ret = std::sscanf(
            line.c_str(),
            " %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
            ifname_c, &rcv_bytes, &rcv_packets, &rcv_errs, &rcv_drop, &rcv_fifo,
            &rcv_frame, &rcv_compressed, &rcv_multicast, &snd_bytes, &snd_packets,
            &snd_errs, &snd_drop, &snd_fifo, &snd_colls, &snd_carrier,
            &snd_compressed);
        if (ret >= 17) {
          std::string ifname = ifname_c;
          if (!ifname.empty() && ifname.back() == ':') { ifname.pop_back(); }
          if (!is_virtual_interface(ifname)) {
            stats_map[ifname] = {rcv_bytes, rcv_packets, snd_bytes, snd_packets};
          }
        }
      }
    }
    return stats_map;
  }
  for (const auto &kv : entries) {
    uint32_t ifindex = kv.first;
    const auto &v = kv.second;
    char name[IF_NAMESIZE] = {0};
    if (if_indextoname(ifindex, name) == nullptr) {
      continue;
    }
    std::string ifname = name;
    if (is_virtual_interface(ifname)) {
      continue;
    }
    stats_map[ifname] = {v.rcv_bytes, v.rcv_packets, v.snd_bytes, v.snd_packets};
  }

  return stats_map;
}

// ----------------------------------------------------------------------
// /proc 部分：负责 err 和 drop
// ----------------------------------------------------------------------

/**
 * @brief 从 /proc/net/dev 文件读取所有网络接口的 err 和 drop 统计信息
 * @return 包含接口名和 err/drop 数据的 map
 */
static std::map<std::string, NetStat> proc_get_net_err_drop_stats() {
  std::map<std::string, NetStat> stats_map;
  std::ifstream file("/proc/net/dev");

  if (!file.is_open()) {
    std::cerr << "Error opening /proc/net/dev for err/drop stats" << std::endl;
    return stats_map;
  }

  std::string line;
  std::getline(file, line); // Skip 1
  std::getline(file, line); // Skip 2

  while (std::getline(file, line)) {
    NetStat s = {}; // 临时结构体，只用 err/drop 字段

    char ifname_c[IF_NAMESIZE + 1];
    uint64_t rcv_bytes, rcv_packets, rcv_errs, rcv_drop, rcv_fifo, rcv_frame,
        rcv_compressed, rcv_multicast;
    uint64_t snd_bytes, snd_packets, snd_errs, snd_drop, snd_fifo, snd_colls,
        snd_carrier, snd_compressed;

    // 解析所有 16 个字段
    int ret = std::sscanf(
        line.c_str(),
        " %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
        ifname_c, &rcv_bytes, &rcv_packets, &rcv_errs, &rcv_drop, &rcv_fifo,
        &rcv_frame, &rcv_compressed, &rcv_multicast, &snd_bytes, &snd_packets,
        &snd_errs, &snd_drop, &snd_fifo, &snd_colls, &snd_carrier,
        &snd_compressed);

    if (ret >= 17) {
      s.name = ifname_c;

      if (!s.name.empty() && s.name.back() == ':') {
        s.name.pop_back();
      }

      if (is_virtual_interface(s.name)) {
        continue;
      }

      // 仅填充 /proc 负责的 err 和 drop 字段
      s.err_in = rcv_errs;
      s.drop_in = rcv_drop;
      s.err_out = snd_errs;
      s.drop_out = snd_drop;

      stats_map[s.name] = s;
    }
  }
  return stats_map;
}

// ----------------------------------------------------------------------
// NetMonitor::UpdateOnce 实现 (合并逻辑)
// ----------------------------------------------------------------------

void NetMonitor::UpdateOnce(monitor::proto::MonitorInfo *monitor_info) {
  auto now = std::chrono::steady_clock::now();

  // 1. 获取所有网络接口的 bytes/packets 统计数据 (eBPF 负责)
  std::map<std::string, EbpfStats> ebpf_stats;
  try {
    ebpf_stats = ebpf_get_net_stats();
  } catch (...) {
    ebpf_stats.clear();
  }

  // 2. 获取所有网络接口的 err/drop 统计数据 (/proc 负责)
  std::map<std::string, NetStat> proc_stats = proc_get_net_err_drop_stats();

  // 3. 合并数据：以 eBPF 数据为主，填充 /proc 的 err/drop
  std::vector<NetStat> current_stats;
  for (const auto &pair : ebpf_stats) {
    const std::string &ifname = pair.first;
    const EbpfStats &ebpf_data = pair.second;

    NetStat s = {};
    s.name = ifname;

    // 填充 eBPF 数据 (bytes/packets)
    s.rcv_bytes = ebpf_data.rcv_bytes;
    s.rcv_packets = ebpf_data.rcv_packets;
    s.snd_bytes = ebpf_data.snd_bytes;
    s.snd_packets = ebpf_data.snd_packets;

    // 填充 /proc 数据 (err/drop)
    auto proc_it = proc_stats.find(ifname);
    if (proc_it != proc_stats.end()) {
      s.err_in = proc_it->second.err_in;
      s.drop_in = proc_it->second.drop_in;
      s.err_out = proc_it->second.err_out;
      s.drop_out = proc_it->second.drop_out;
    } else {
      // 如果 eBPF 监控的接口在 /proc 中找不到，则将 err/drop 设为 0
      s.err_in = s.drop_in = s.err_out = s.drop_out = 0;
    }

    current_stats.push_back(s);
  }

  // 4. 遍历当前统计数据，计算速率，并更新缓存
  for (const auto &stat : current_stats) {
    auto it = last_net_info_.find(stat.name);
    float rcv_rate = 0, rcv_packets_rate = 0, send_rate = 0,
          send_packets_rate = 0;
    float err_in_rate = 0, err_out_rate = 0, drop_in_rate = 0,
          drop_out_rate = 0;

    if (it != last_net_info_.end()) {
      const NetInfo &last = it->second;
      // 计算时间间隔
      double dt = std::chrono::duration<double>(now - last.timepoint).count();

      if (dt > 0) {
        // 计算流量速率 (eBPF 数据)
        rcv_rate = (stat.rcv_bytes - last.rcv_bytes) / 1024.0 / dt; // KB/s
        rcv_packets_rate = (stat.rcv_packets - last.rcv_packets) / dt;
        send_rate = (stat.snd_bytes - last.snd_bytes) / 1024.0 / dt; // KB/s
        send_packets_rate = (stat.snd_packets - last.snd_packets) / dt;

        // 计算错误和丢弃速率 (/proc 数据)
        err_in_rate = (stat.err_in - last.err_in) / dt;
        err_out_rate = (stat.err_out - last.err_out) / dt;
        drop_in_rate = (stat.drop_in - last.drop_in) / dt;
        drop_out_rate = (stat.drop_out - last.drop_out) / dt;
      }
    }

    // 填充 protobuf
    auto net_info = monitor_info->add_net_info();
    net_info->set_name(stat.name);
    net_info->set_rcv_rate(rcv_rate);
    net_info->set_rcv_packets_rate(rcv_packets_rate);
    net_info->set_send_rate(send_rate);
    net_info->set_send_packets_rate(send_packets_rate);

    // /proc 提供的原始计数
    net_info->set_err_in(stat.err_in);
    net_info->set_err_out(stat.err_out);
    net_info->set_drop_in(stat.drop_in);
    net_info->set_drop_out(stat.drop_out);

    // /proc 提供的速率
    net_info->set_err_in_rate(err_in_rate);
    net_info->set_err_out_rate(err_out_rate);
    net_info->set_drop_in_rate(drop_in_rate);
    net_info->set_drop_out_rate(drop_out_rate);

    // 更新缓存
    NetInfo new_info;
    new_info.name = stat.name;
    new_info.rcv_bytes = stat.rcv_bytes;
    new_info.rcv_packets = stat.rcv_packets;
    new_info.snd_bytes = stat.snd_bytes;
    new_info.snd_packets = stat.snd_packets;
    new_info.err_in = stat.err_in;
    new_info.err_out = stat.err_out;
    new_info.drop_in = stat.drop_in;
    new_info.drop_out = stat.drop_out;
    new_info.timepoint = now;
    last_net_info_[stat.name] = new_info;
  }
}

} // namespace yanhon
