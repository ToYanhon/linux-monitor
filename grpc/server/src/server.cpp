#include "rpc/server.hpp"
#include <iostream>

namespace yanhon {
RpcServerImpl::RpcServerImpl() {}
RpcServerImpl::~RpcServerImpl() {}

grpc::Status
RpcServerImpl::SetMonitorInfo(grpc::ServerContext *context,
                              const monitor::proto::MonitorInfo *request,
                              ::google::protobuf::Empty *response) {
  std::unique_lock<std::mutex> lock(mtx_);
  monitor_infos_map_[request->name()] = *request;
  lock.unlock();
  std::cout << "SetMonitorInfo called for: " << request->name() << std::endl;

  auto cpu_load = request->cpu_load();
  std::cout << "  CPU Load - 1min: " << cpu_load.load_avg_1()
            << ", 3min: " << cpu_load.load_avg_3()
            << ", 15min: " << cpu_load.load_avg_15() << std::endl;

  auto cpu_softirq = request->soft_irq();
  // std::cout << "  SoftIrq count: " << cpu_softirq.size()
  // << std::endl; // 添加这行
  for (auto i = 0; i < cpu_softirq.size(); ++i) {
    const auto &irq = cpu_softirq.Get(i);
    std::cout << "  SoftIrq[" << i << "] - CPU: " << irq.cpu()
              << ", Hi: " << irq.hi() << ", Timer: " << irq.timer()
              << ", NetTx: " << irq.net_tx() << ", NetRx: " << irq.net_rx()
              << ", Block: " << irq.block() << ", IrqPoll: " << irq.irq_poll()
              << ", Tasklet: " << irq.tasklet() << ", Sched: " << irq.sched()
              << ", Hrtimer: " << irq.hrtimer() << ", Rcu: " << irq.rcu()
              << std::endl;
  }

  auto cpu_stat = request->cpu_stat();
  // std::cout << "  CpuStat count: " << cpu_stat.size() << std::endl;
  for (auto i = 0; i < cpu_stat.size(); ++i) {
    const auto &stat = cpu_stat.Get(i);
    std::cout << "  CpuStat[" << i << "] - Name: " << stat.cpu_name()
              << ", CpuPercent: " << stat.cpu_percent()
              << ", UsrPercent: " << stat.usr_percent()
              << ", SystemPercent: " << stat.system_percent()
              << ", NicePercent: " << stat.nice_percent()
              << ", IdlePercent: " << stat.idle_percent()
              << ", IoWaitPercent: " << stat.io_wait_percent()
              << ", IrqPercent: " << stat.irq_percent()
              << ", SoftIrqPercent: " << stat.soft_irq_percent() << std::endl;
  }

  auto disk_info = request->disk_info();
  for (auto i = 0; i < disk_info.size(); ++i) {
    const auto &disk = disk_info.Get(i);
    std::cout << "  DiskInfo[" << i << "] - Name: " << disk.name()
              << ", Read: " << disk.reads() << ", Write: " << disk.writes()
              << ", ReadSectors: " << disk.sectors_read()
              << ", WriteSectors: " << disk.sectors_written()
              << ", ReadTimeMs: " << disk.read_time_ms()
              << ", WriteTimeMs: " << disk.write_time_ms()
              << ", IoInProgress: " << disk.io_in_progress()
              << ", IoTimeMs: " << disk.io_time_ms()
              << ", WeightedIoTimeMs: " << disk.weighted_io_time_ms()
              << ", ReadBytesPerSec: " << disk.read_bytes_per_sec()
              << ", WriteBytesPerSec: " << disk.write_bytes_per_sec()
              << ", ReadIOPS: " << disk.read_iops()
              << ", WriteIOPS: " << disk.write_iops()
              << ", AvgReadLatencyMs: " << disk.avg_read_latency_ms()
              << ", AvgWriteLatencyMs: " << disk.avg_write_latency_ms()
              << ", UtilPercent: " << disk.util_percent() << std::endl;
  }

  auto mem_info = request->mem_info();
  std::cout << "  MemInfo - Total: " << mem_info.total()
            << ", Free: " << mem_info.free() << ", Avail: " << mem_info.avail()
            << ", buffers: " << mem_info.buffers()
            << ", Cached: " << mem_info.cached()
            << ", SwapCached: " << mem_info.swap_cached()
            << ", Active: " << mem_info.active()
            << ", Inactive: " << mem_info.inactive()
            << ", ActiveAnon: " << mem_info.active_anon()
            << ", InactiveAnon: " << mem_info.inactive_anon()
            << ", ActiveFile: " << mem_info.active_file()
            << ", InactiveFile: " << mem_info.inactive_file()
            << ", Dirty: " << mem_info.dirty()
            << ", Writeback: " << mem_info.writeback()
            << ", AnonPages: " << mem_info.anon_pages()
            << ", Mapped: " << mem_info.mapped()
            << ", KReclaimable: " << mem_info.kreclaimable()
            << ", SReclaimable: " << mem_info.sreclaimable()
            << ", SUnreclaim: " << mem_info.sunreclaim()
            << ", UsedPercent: " << mem_info.used_percent() << std::endl;

  auto net_info = request->net_info();
  for (auto i = 0; i < net_info.size(); ++i) {
    const auto &net = net_info.Get(i);
    std::cout << "  NetInfo[" << i << "] - Name: " << net.name()
              << ", SendRate: " << net.send_rate()
              << ", RcvRate: " << net.rcv_rate()
              << ", SendPacketsRate: " << net.send_packets_rate()
              << ", RcvPacketsRate: " << net.rcv_packets_rate()
              << ", ErrIn: " << net.err_in() << ", ErrOut: " << net.err_out()
              << ", DropIn: " << net.drop_in()
              << ", DropOut: " << net.drop_out()
              << ", ErrInRate: " << net.err_in_rate()
              << ", ErrOutRate: " << net.err_out_rate()
              << ", DropInRate: " << net.drop_in_rate()
              << ", DropOutRate: " << net.drop_out_rate() << std::endl;
  }

  return grpc::Status::OK;
}

grpc::Status
RpcServerImpl::GetMonitorInfo(grpc::ServerContext *context,
                              const google::protobuf::Empty *request,
                              monitor::proto::MultiMonitorInfo *response) {
  std::unique_lock<std::mutex> lock(mtx_);
  for (const auto &[_, second] : monitor_infos_map_) {
    auto *info = response->add_infos();
    info->CopyFrom(second);
  }
  return grpc::Status::OK;
}
} // namespace yanhon