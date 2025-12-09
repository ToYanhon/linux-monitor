// main.cpp - 使用示例
#include "manager/performance_server.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace yanhon {

volatile std::sig_atomic_t stop_flag = 0;

void signal_handler(int signal) {
  stop_flag = 1;
  std::cout << "\nReceived stop signal (signal " << signal
            << "), shutting down..." << std::endl;
}

} // namespace yanhon

int main(int argc, char *argv[]) {
  using namespace yanhon;

  // 设置信号处理
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try {
    // 配置MySQL连接
    PerformanceMonitorClient::MySQLConfig mysql_config;
    // 使用默认构造函数，所以使用默认值

    // 创建客户端实例
    PerformanceMonitorClient client("localhost:50051", mysql_config);

    // 测试连接和单次数据获取
    std::cout << "Testing connection to monitor server..." << std::endl;
    if (client.fetchMonitorData()) {
      std::cout << "Connection test successful" << std::endl;

      // 存储到数据库
      if (client.storeToDatabase()) {
        std::cout << "Initial data storage successful" << std::endl;
      } else {
        std::cerr << "Initial data storage failed" << std::endl;
      }
    } else {
      std::cerr << "Connection test failed" << std::endl;
    }

    // 启动定期监控（每60秒一次）
    std::cout << "\nStarting periodic monitoring..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    client.startMonitoring(60);

    // 主循环，等待停止信号
    while (!stop_flag) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 停止监控
    client.stopMonitoring();

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Application terminated successfully" << std::endl;
  return EXIT_SUCCESS;
}