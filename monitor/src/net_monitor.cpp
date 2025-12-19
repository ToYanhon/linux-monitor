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
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <unistd.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <string.h>
#include <iostream>

namespace yanhon {
static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
                           va_list args) {
  // 打印所有级别的日志，以便调试
  const char *level_str;
  switch (level) {
  case LIBBPF_WARN:
    level_str = "WARN";
    break;
  case LIBBPF_INFO:
    level_str = "INFO";
    break;
  case LIBBPF_DEBUG:
    level_str = "DEBUG";
    break;
  default:
    level_str = "UNKNOWN";
    break;
  }
  fprintf(stderr, "[libbpf %s] ", level_str);
  return vfprintf(stderr, format, args);
}

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

/**
 * @brief 将 ifindex 转换为接口名称
 * @param ifindex 接口索引
 * @return 接口名称，如果转换失败则返回空字符串
 */
static std::string ifindex_to_ifname(int ifindex) {
  char ifname[IF_NAMESIZE];
  if (if_indextoname(ifindex, ifname)) {
    return std::string(ifname);
  }
  return "";
}

// 通过netlink获取网络接口
static std::unordered_map<int, std::string> get_network_interfaces() {
  std::unordered_map<int, std::string> ifindex_map;

  int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sock < 0) {
    perror("socket");
    return ifindex_map;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;

  if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("bind");
    close(sock);
    return ifindex_map;
  }

  // 构建请求消息
  struct {
    struct nlmsghdr nlh;
    struct rtgenmsg g;
  } req;

  memset(&req, 0, sizeof(req));
  req.nlh.nlmsg_len = sizeof(req);
  req.nlh.nlmsg_type = RTM_GETLINK;
  req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.nlh.nlmsg_seq = 1;
  req.nlh.nlmsg_pid = getpid();
  req.g.rtgen_family = AF_PACKET;

  // 发送请求
  if (send(sock, &req, sizeof(req), 0) < 0) {
    perror("send");
    close(sock);
    return ifindex_map;
  }

  // 设置接收超时
  struct timeval tv;
  tv.tv_sec = 2; // 2秒超时
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // 接收响应
  char buf[4096];
  ssize_t len;

  len = recv(sock, buf, sizeof(buf), 0);
  if (len > 0) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;

    for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
      if (nlh->nlmsg_type == NLMSG_DONE) {
        break;
      }

      if (nlh->nlmsg_type == RTM_NEWLINK) {
        struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
        struct rtattr *rta = IFLA_RTA(ifi);
        int rtalen = IFLA_PAYLOAD(nlh);

        for (; RTA_OK(rta, rtalen); rta = RTA_NEXT(rta, rtalen)) {
          if (rta->rta_type == IFLA_IFNAME) {
            char ifname[IFNAMSIZ];
            strncpy(ifname, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
            ifname[IFNAMSIZ - 1] = '\0';

            // 跳过虚拟接口
            if (!is_virtual_interface(ifname)) {
              ifindex_map[ifi->ifi_index] = ifname;
              printf("Found interface: %s (ifindex: %d)\n", ifname,
                     ifi->ifi_index);
            }
            break;
          }
        }
      }
    }
  }

  close(sock);
  return std::move(ifindex_map);
}

// ----------------------------------------------------------------------
// eBPF 模拟部分：负责 bytes 和 packets
// ----------------------------------------------------------------------

/**
 * @brief 从 eBPF Map 读取所有网络接口的 bytes 和 packets 统计信息
 * @return 包含接口名和 bytes/packets 统计数据的 map
 */
std::unordered_map<std::string, if_counters> NetMonitor::ebpf_get_net_stats() {
  std::unordered_map<std::string, if_counters> states_map;

  // 如果BPF没有加载成功，返回空map
  if (!skel || !bpf_loaded) {
    fprintf(stderr, "BPF not loaded, cannot get network statistics\n");
    return std::move(states_map);
  }

  int key = 0, next_key;
  int err;
  auto map = skel->maps.if_stats;
  int map_fd = bpf_map__fd(map);

  // 获取CPU数量
  int num_cpus = libbpf_num_possible_cpus();
  if (num_cpus <= 0) {
    num_cpus = 1;
  }

  // 为每个CPU的值分配内存
  struct if_counters *values = static_cast<struct if_counters *>(
      calloc(num_cpus, sizeof(struct if_counters)));
  if (!values) {
    fprintf(stderr, "Failed to allocate memory for per-CPU values\n");
    return std::move(states_map);
  }

  printf("\n=== Network Interface Statistics ===\n");
  printf("%-10s %-15s %-15s %-15s %-15s %-15s\n", "IFINDEX", "IFNAME",
         "RCV_BYTES", "RCV_PACKETS", "SND_BYTES", "SND_PACKETS");
  printf("%-10s %-15s %-15s %-15s %-15s %-15s\n", "-------", "------",
         "---------", "-----------", "---------", "-----------");

  // 遍历映射中的所有键
  while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
    // 对于PERCPU_HASH，bpf_map_lookup_elem返回指向每个CPU值数组的指针
    err = bpf_map_lookup_elem(map_fd, &next_key, values);
    if (err == 0) {
      // 汇总所有CPU的值
      struct if_counters sum = {0};
      for (int i = 0; i < num_cpus; i++) {
        sum.rcv_bytes += values[i].rcv_bytes;
        sum.rcv_packets += values[i].rcv_packets;
        sum.snd_bytes += values[i].snd_bytes;
        sum.snd_packets += values[i].snd_packets;
      }

      // 获取接口名称
      std::string ifname = ifindex_to_ifname(next_key);
      if (ifname.empty()) {
        // 如果无法获取接口名称，跳过此接口
        key = next_key;
        continue;
      }

      // 跳过虚拟接口
      if (is_virtual_interface(ifname)) {
        key = next_key;
        continue;
      }

      // 只显示非零的统计信息
      if (sum.rcv_bytes > 0 || sum.rcv_packets > 0 || sum.snd_bytes > 0 ||
          sum.snd_packets > 0) {
        printf("%-10d %-15s %-15llu %-15llu %-15llu %-15llu\n", next_key,
               ifname.c_str(), sum.rcv_bytes, sum.rcv_packets, sum.snd_bytes,
               sum.snd_packets);

        total.rcv_bytes += sum.rcv_bytes;
        total.rcv_packets += sum.rcv_packets;
        total.snd_bytes += sum.snd_bytes;
        total.snd_packets += sum.snd_packets;
      }

      // 将数据添加到返回的map中
      states_map[ifname] = sum;
    }
    key = next_key;
  }

  free(values);

  printf("\n=== Total Statistics ===\n");
  printf("Total Received: %llu bytes, %llu packets\n", total.rcv_bytes,
         total.rcv_packets);
  printf("Total Sent:     %llu bytes, %llu packets\n", total.snd_bytes,
         total.snd_packets);
  printf("=========================\n\n");
  return std::move(states_map);
}

// ----------------------------------------------------------------------
// /proc 部分：负责 err 和 drop
// ----------------------------------------------------------------------

/**
 * @brief 从 /proc/net/dev 文件读取所有网络接口的 err 和 drop 统计信息
 * @return 包含接口名和 err/drop 数据的 map
 */
static std::unordered_map<std::string, NetStat> proc_get_net_err_drop_stats() {
  std::unordered_map<std::string, NetStat> stats_map;
  std::ifstream file("/proc/net/dev");

  if (!file.is_open()) {
    std::cerr << "Error opening /proc/net/dev for err/drop stats" << std::endl;
    return std::move(stats_map);
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
  return std::move(stats_map);
}

// ----------------------------------------------------------------------
// NetMonitor::UpdateOnce 实现 (合并逻辑)
// ----------------------------------------------------------------------

NetMonitor::NetMonitor() {
  int err;
  struct bpf_map_info info = {};
  __u32 info_len = sizeof(info);
  int interval = 2; // Default update interval in seconds
  time_t last_print = 0;

  libbpf_set_print(libbpf_print_fn);

  printf("Starting network monitor...\n");

  // 获取网络接口
  ifindex_map = get_network_interfaces();
  if (ifindex_map.empty()) {
    fprintf(stderr, "No network interfaces found\n");
    return;
  }

  printf("Found %zu network interfaces:\n", ifindex_map.size());
  int idx = 0;
  for (const auto &pair : ifindex_map) {
    printf("  [%d] ifindex: %d, name: %s\n", idx++, pair.first,
           pair.second.c_str());
  }
  printf("\n");

  skel = net_monitor_bpf__open_and_load();
  if (!skel) {
    fprintf(stderr, "Failed to open and load BPF object\n");
    // 设置skel为nullptr并返回，避免后续操作
    skel = nullptr;
    bpf_loaded = false;
    return;
  }

  bpf_loaded = true;

  // int err = net_monitor_bpf__load(skel);
  // if (err) {
  //   fprintf(stderr, "Failed to load BPF object: %d\n", err);
  //   net_monitor_bpf__destroy(skel);
  //   skel = nullptr;
  //   return;
  // }

  // 分配内存存储TC钩子和选项
  size_t interface_count = ifindex_map.size();
  tc_hooks_ingress = static_cast<struct bpf_tc_hook *>(
      calloc(interface_count, sizeof(struct bpf_tc_hook)));
  tc_hooks_egress = static_cast<struct bpf_tc_hook *>(
      calloc(interface_count, sizeof(struct bpf_tc_hook)));
  tc_opts_ingress = static_cast<struct bpf_tc_opts *>(
      calloc(interface_count, sizeof(struct bpf_tc_opts)));
  tc_opts_egress = static_cast<struct bpf_tc_opts *>(
      calloc(interface_count, sizeof(struct bpf_tc_opts)));
  hooks_created_ingress =
      static_cast<bool *>(calloc(interface_count, sizeof(bool)));
  hooks_created_egress =
      static_cast<bool *>(calloc(interface_count, sizeof(bool)));

  if (!tc_hooks_ingress || !tc_hooks_egress || !tc_opts_ingress ||
      !tc_opts_egress || !hooks_created_ingress || !hooks_created_egress) {
    fprintf(stderr, "Failed to allocate memory for TC hooks\n");
    net_monitor_bpf__destroy(skel);
    return;
  }

  // 如果BPF加载失败，直接返回，不初始化TC钩子
  if (!bpf_loaded) {
    fprintf(stderr, "BPF not loaded, skipping TC hook initialization\n");
    return;
  }

  // 初始化TC钩子和选项
  int i = 0;
  for (const auto &pair : ifindex_map) {
    int ifindex = pair.first;
    // ingress钩子
    tc_hooks_ingress[i] = (struct bpf_tc_hook){
        .sz = sizeof(struct bpf_tc_hook),
        .ifindex = ifindex,
        .attach_point = BPF_TC_INGRESS,
    };

    // egress钩子
    tc_hooks_egress[i] = (struct bpf_tc_hook){
        .sz = sizeof(struct bpf_tc_hook),
        .ifindex = ifindex,
        .attach_point = BPF_TC_EGRESS,
    };

    // ingress选项
    tc_opts_ingress[i] = (struct bpf_tc_opts){
        .sz = sizeof(struct bpf_tc_opts),
        .prog_fd = bpf_program__fd(skel->progs.on_ingress),
        .handle = 1,
        .priority = 1,
    };

    // egress选项
    tc_opts_egress[i] = (struct bpf_tc_opts){
        .sz = sizeof(struct bpf_tc_opts),
        .prog_fd = bpf_program__fd(skel->progs.on_egress),
        .handle = 2,
        .priority = 1,
    };
    i++;
  }

  // 为每个接口创建和附加TC程序
  printf("Attaching TC programs to interfaces...\n");
  i = 0;
  for (const auto &pair : ifindex_map) {
    int ifindex = pair.first;
    const std::string &ifname = pair.second;
    printf("Interface %s (ifindex %d):\n", ifname.c_str(), ifindex);

    // 首先尝试销毁可能已存在的TC钩子
    bpf_tc_hook_destroy(&tc_hooks_ingress[i]);
    bpf_tc_hook_destroy(&tc_hooks_egress[i]);

    // 创建ingress TC钩子
    err = bpf_tc_hook_create(&tc_hooks_ingress[i]);
    if (!err)
      hooks_created_ingress[i] = true;
    if (err && err != -EEXIST) {
      fprintf(stderr, "  Failed to create ingress TC hook: %d\n", err);
      i++;
      continue;
    }

    // 创建egress TC钩子
    err = bpf_tc_hook_create(&tc_hooks_egress[i]);
    if (!err)
      hooks_created_egress[i] = true;
    if (err && err != -EEXIST) {
      fprintf(stderr, "  Failed to create egress TC hook: %d\n", err);
      if (hooks_created_ingress[i])
        bpf_tc_hook_destroy(&tc_hooks_ingress[i]);
      i++;
      continue;
    }

    // 附加ingress程序
    err = bpf_tc_attach(&tc_hooks_ingress[i], &tc_opts_ingress[i]);
    if (err) {
      fprintf(stderr, "  Failed to attach ingress TC: %d\n", err);
      if (hooks_created_ingress[i])
        bpf_tc_hook_destroy(&tc_hooks_ingress[i]);
      if (hooks_created_egress[i])
        bpf_tc_hook_destroy(&tc_hooks_egress[i]);
      i++;
      continue;
    }

    // 附加egress程序
    err = bpf_tc_attach(&tc_hooks_egress[i], &tc_opts_egress[i]);
    if (err) {
      fprintf(stderr, "  Failed to attach egress TC: %d\n", err);
      bpf_tc_detach(&tc_hooks_ingress[i], &tc_opts_ingress[i]);
      if (hooks_created_ingress[i])
        bpf_tc_hook_destroy(&tc_hooks_ingress[i]);
      if (hooks_created_egress[i])
        bpf_tc_hook_destroy(&tc_hooks_egress[i]);
      i++;
      continue;
    }

    printf("  ✓ ingress and egress programs attached\n");
    i++;
  }

  printf("\nTC programs attachment completed.\n");

  // 仍然调用标准的attach，它可能会设置一些其他东西
  err = net_monitor_bpf__attach(skel);
  if (err) {
    fprintf(stderr,
            "Warning: Standard attach failed: %d (this may be normal for TC "
            "programs)\n",
            err);
    throw std::runtime_error("net_monitor_bpf__attach failed");
  }

  // Retrieve and print map info
  err = bpf_obj_get_info_by_fd(bpf_map__fd(skel->maps.if_stats), &info,
                               &info_len);
  if (err) {
    fprintf(stderr, "Failed to get map info: %d\n", err);
    net_monitor_bpf__destroy(skel);
    throw std::runtime_error("bpf_obj_get_info_by_fd failed");
  }

  printf("BPF program loaded successfully!\n");
  printf("Map 'if_stats' info:\n");
  printf("  ID: %u\n", info.id);
  printf("  Type: %u\n", info.type);
  printf("  Key Size: %u\n", info.key_size);
  printf("  Value Size: %u\n", info.value_size);
  printf("  Max Entries: %u\n", info.max_entries);
  printf("\n");
}

void NetMonitor::UpdateOnce(monitor::proto::MonitorInfo *monitor_info) {
  auto now = std::chrono::steady_clock::now();

  // 1. 获取所有网络接口的 bytes/packets 统计数据 (eBPF 负责)
  std::unordered_map<std::string, if_counters> ebpf_stats;
  try {
    ebpf_stats = ebpf_get_net_stats();
  } catch (...) {
    ebpf_stats.clear();
  }

  // 如果 eBPF 数据为空，直接返回
  if (ebpf_stats.empty()) {
    std::cerr << "No eBPF statistics available." << std::endl;
    return;
  }

  // 2. 获取所有网络接口的 err/drop 统计数据 (/proc 负责)
  std::unordered_map<std::string, NetStat> proc_stats =
      proc_get_net_err_drop_stats();

  // 3. 合并数据：以 eBPF 数据为主，填充 /proc 的 err/drop
  std::vector<NetStat> current_stats;
  for (const auto &pair : ebpf_stats) {
    const std::string &ifname = pair.first;
    const if_counters &ebpf_data = pair.second;

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

NetMonitor::~NetMonitor() { // 分离TC程序并销毁TC钩子
  printf("Detaching TC programs...\n");
  size_t interface_count = ifindex_map.size();
  for (size_t i = 0; i < interface_count; i++) {
    bpf_tc_detach(&tc_hooks_ingress[i], &tc_opts_ingress[i]);
    bpf_tc_detach(&tc_hooks_egress[i], &tc_opts_egress[i]);

    if (hooks_created_ingress[i])
      bpf_tc_hook_destroy(&tc_hooks_ingress[i]);
    if (hooks_created_egress[i])
      bpf_tc_hook_destroy(&tc_hooks_egress[i]);
  }

  // 释放内存
  free(tc_hooks_ingress);
  free(tc_hooks_egress);
  free(tc_opts_ingress);
  free(tc_opts_egress);
  free(hooks_created_ingress);
  free(hooks_created_egress);

  net_monitor_bpf__destroy(skel);
  printf("Network monitor stopped.\n");
}

} // namespace yanhon
