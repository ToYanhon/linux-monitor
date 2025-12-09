#pragma once
#include "monitor_info.grpc.pb.h"
#include "monitor_info.pb.h"
#include <string>

namespace yanhon {
class RpcClient {
public:
  RpcClient(const std::string &server_address = "localhost:50051");
  ~RpcClient();
  void SetMonitorInfo(const monitor::proto::MonitorInfo &monito_info);
  void GetMonitorInfo(monitor::proto::MultiMonitorInfo *monito_infos);

  RpcClient(const RpcClient &) = delete;
  RpcClient &operator=(const RpcClient &) = delete;
  RpcClient(RpcClient &&) = default;
  RpcClient &operator=(RpcClient &&) = default;

private:
  std::unique_ptr<monitor::proto::GrpcManager::Stub> stub_ptr;
};
} // namespace yanhon