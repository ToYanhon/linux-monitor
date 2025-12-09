#pragma once

#include <grpcpp/support/status.h>
#include <mutex>
#include <unordered_map>

#include "monitor_info.grpc.pb.h"
#include "monitor_info.pb.h"

namespace yanhon {
class RpcServerImpl : public monitor::proto::GrpcManager::Service {
public:
  RpcServerImpl();
  ~RpcServerImpl();

  grpc::Status SetMonitorInfo(grpc::ServerContext *context,
                              const monitor::proto::MonitorInfo *request,
                              ::google::protobuf::Empty *response);
  grpc::Status GetMonitorInfo(grpc::ServerContext *context,
                              const google::protobuf::Empty *request,
                              monitor::proto::MultiMonitorInfo *response);

private: // monitor::proto::MonitorInfo monitor_infos_;
  std::unordered_map<std::string, monitor::proto::MonitorInfo>
      monitor_infos_map_;
  std::mutex mtx_;
};
} // namespace yanhon