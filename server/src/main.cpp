#include "rpc/server.hpp"
#include <grpc/grpc.h>
#include <grpcpp/server_builder.h>
#include <iostream>

// 定义了服务器的启动流程，初始化 gRPC 服务并监听指定端口。
constexpr char kServerPortInfo[] = "0.0.0.0:50051";
void InitServer() {
  grpc::ServerBuilder builder; // 用于配置服务器
  builder.AddListeningPort(kServerPortInfo, grpc::InsecureServerCredentials());

  yanhon::RpcServerImpl grpc_server;
  builder.RegisterService(&grpc_server);

  // 调用 BuildAndStart 启动服务器，并通过 server->Wait()
  // 阻塞主线程，保持服务器运行
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  server->Wait();

  return;
}

int main() {
  InitServer();
  return 0;
}