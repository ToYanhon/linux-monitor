#include "rpc/client.hpp"
#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
// #include <grpcpp/grpcpp.h>

namespace yanhon {
RpcClient::RpcClient(const std::string &server_address) {
  auto channel =
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
  stub_ptr = monitor::proto::GrpcManager::NewStub(channel);
}

RpcClient ::~RpcClient() {}

void RpcClient::SetMonitorInfo(const monitor::proto::MonitorInfo &monito_info) {
  grpc::ClientContext ctx;
  google::protobuf::Empty resp;

  auto status = stub_ptr->SetMonitorInfo(&ctx, monito_info, &resp);

  if (status.ok()) {
  } else {
    // 输出错误信息
    std::cout << status.error_details() << std::endl;
    std::cout << "status.error_message: " << status.error_message()
              << std::endl;
    std::cout << "falied to connect !!!" << std::endl;
  }
}

void RpcClient::GetMonitorInfo(monitor::proto::MultiMonitorInfo *monito_infos) {
  grpc::ClientContext ctx;
  google::protobuf::Empty req;

  auto status = stub_ptr->GetMonitorInfo(&ctx, req, monito_infos);
  if (status.ok()) {
  } else {
    // 输出错误信息
    std::cout << status.error_details() << std::endl;
    std::cout << "status.error_message: " << status.error_message()
              << std::endl;
    std::cout << "falied to connect !!!" << std::endl;
  }
}

} // namespace yanhon