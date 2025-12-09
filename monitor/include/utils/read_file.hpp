#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace yanhon {
class ReadFile {
public:
  // 接受文件名作为参数，并初始化文件输入流ifs_
  explicit ReadFile(const std::string &name) : ifs_(name) {}
  ~ReadFile() { ifs_.close(); }

  bool ReadLine(std::vector<std::string> *args);

private:
  std::ifstream ifs_; // 定义了一个私有的文件输入流对象，用于实际的文件读取操作
};
} // namespace yanhon
