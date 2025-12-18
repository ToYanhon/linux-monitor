#include "rpc/server.hpp"
#include <grpc/grpc.h>
#include <grpcpp/server_builder.h>
#include <iostream>
#include <atomic>
#include <csignal>
#include <thread>

// 定义了服务器的启动流程，初始化 gRPC 服务并监听指定端口。
constexpr char kServerPortInfo[] = "0.0.0.0:50051";

// 全局标志，用于指示服务器是否应该停止
std::atomic<bool> server_shutdown(false);

// 信号处理函数
void SignalHandler(int signal) {
  std::cout << "接收到信号 " << signal << "，正在关闭服务器..."
            << std::endl;
  server_shutdown.store(true);
}

void InitServer() {
  grpc::ServerBuilder builder; // 用于配置服务器
  builder.AddListeningPort(kServerPortInfo, grpc::InsecureServerCredentials());

  yanhon::RpcServerImpl grpc_server;
  builder.RegisterService(&grpc_server);

  // 调用 BuildAndStart 启动服务器
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

  if (!server) {
    std::cerr << "服务器启动失败" << std::endl;
    return;
  }

  std::cout << "服务器已启动，监听地址: " << kServerPortInfo << std::endl;
  std::cout << "按 Ctrl+C 或发送 SIGTERM 信号来关闭服务器" << std::endl;

  // 注册信号处理
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  // 使用自定义等待循环，定期检查关闭标志
  while (!server_shutdown.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 优雅关闭服务器
  std::cout << "正在关闭服务器..." << std::endl;
  server->Shutdown();

  // 等待服务器完全关闭
  std::cout << "服务器已关闭" << std::endl;
}

int main() {
  InitServer();
  return 0;
}
