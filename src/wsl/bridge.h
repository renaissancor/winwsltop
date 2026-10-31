#pragma once
#include "core/platform.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace wwt {

// Keeps one wsl.exe collector running and holds its latest line. Restarts it if it ends.
class wsl_bridge {
 public:
  explicit wsl_bridge(std::wstring script_path);
  ~wsl_bridge();
  wsl_bridge(const wsl_bridge&) = delete;
  wsl_bridge& operator=(const wsl_bridge&) = delete;
  bool wait_first_line(double seconds);
  // Latest raw JSON line, empty if none yet. age_s is seconds since it arrived.
  std::string latest(double& age_s, int& restarts) const;

 private:
  void run();
  bool run_once(const std::string& script);
  std::wstring script_path_;
  mutable std::mutex mu_;
  std::string line_;
  double line_t_ = 0;
  int started_ = 0;
  std::atomic<bool> stop_{false};
  HANDLE job_ = nullptr;
  std::thread thread_;
};

}  // namespace wwt
