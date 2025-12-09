#include <string.h>      // 添加: 使用 strtok 和 strdup
#include <sys/syscall.h> // 添加: 使用 __NR_getuid
#include <unistd.h>      // 添加: 使用 syscall

#include <chrono> // 添加: 使用标准库的 chrono
#include <memory>
#include <thread>
#include <vector>

#include "monitor/cpu_load_monitor.hpp"
#include "monitor/cpu_softirq_monitor.hpp"
#include "monitor/cpu_stat_monitor.hpp"
#include "monitor/disk_monitor.hpp"
#include "monitor/mem_monitor.hpp"
#include "monitor/monitor_inter.hpp"
#include "monitor/net_monitor.hpp"
#include "rpc/client.hpp"

#include "monitor_info.grpc.pb.h"
#include "monitor_info.pb.h"

// 完全自定义的getuid
static uid_t my_getuid(void) {
  return syscall(__NR_getuid); // 或直接使用内联汇编
}

// 自定义的getpwuid
struct my_passwd {
  char *pw_name;
  uid_t pw_uid;
};

static struct my_passwd *my_getpwuid(uid_t uid) {
  static struct my_passwd result;
  static char line[1024];
  FILE *fp = fopen("/etc/passwd", "r");

  if (!fp)
    return NULL;

  while (fgets(line, sizeof(line), fp)) {
    char *name = strtok(line, ":");
    strtok(NULL, ":"); // 跳过密码
    char *uid_str = strtok(NULL, ":");

    if (uid_str && atoi(uid_str) == uid) {
      result.pw_name = strdup(name);
      result.pw_uid = uid;
      fclose(fp);
      return &result;
    }
  }

  fclose(fp);
  return NULL;
}

int main() {
  std::vector<std::shared_ptr<yanhon::MonitorInter>> runners_;
  runners_.emplace_back(new yanhon::CpuSoftIrqMonitor());
  runners_.emplace_back(new yanhon::CpuLoadMonitor());
  runners_.emplace_back(new yanhon::CpuStatMonitor());
  runners_.emplace_back(new yanhon::MemMonitor());
  runners_.emplace_back(new yanhon::NetMonitor());
  runners_.emplace_back(new yanhon::DiskMonitor());

  yanhon::RpcClient rpc_client_;
  uid_t uid = my_getuid(); // 使用自定义的 my_getuid 获取 UID
  struct my_passwd *pwd =
      my_getpwuid(uid); // 使用自定义的 my_getpwuid 获取用户名
  std::string username =
      pwd ? pwd->pw_name : "unknown_user"; // 如果获取失败，使用默认用户名
  std::unique_ptr<std::thread> thread_ = nullptr;
  thread_ = std::make_unique<std::thread>([&]() {
    while (true) {
      monitor::proto::MonitorInfo monitor_info;
      monitor_info.set_name(username); // 使用自定义获取的用户名
      for (auto &runner : runners_) {
        runner->UpdateOnce(&monitor_info);
      }

      rpc_client_.SetMonitorInfo(monitor_info);
      std::this_thread::sleep_for(
          std::chrono::seconds(3)); // 修改: 使用标准库的 chrono
    }
  });

  thread_->join();
  return 0;
}