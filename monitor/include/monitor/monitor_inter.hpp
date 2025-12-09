#pragma once
#include "monitor_info.grpc.pb.h"
#include "monitor_info.pb.h"
#include <string>

namespace yanhon {
class MonitorInter {
public:
  MonitorInter() {}
  virtual ~MonitorInter() {}
  virtual void UpdateOnce(monitor::proto::MonitorInfo *monitor_info) = 0;
  virtual void Stop() = 0;
};
} // namespace yanhon
