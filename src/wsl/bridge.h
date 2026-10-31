#pragma once
#include "core/platform.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace wwt {

// Keeps one wsl.exe collector running and holds its latest line. Restarts it if it ends.
class wsl_bridge {
 public:
  // script_path is read when the bridge starts; if it cannot be read the embedded copy is used.
  // distro is passed to `wsl.exe -d`; empty means the default distro.
  explicit wsl_bridge(std::wstring script_path, std::wstring distro = L"");
  ~wsl_bridge();
  wsl_bridge(const wsl_bridge&) = delete;
  wsl_bridge& operator=(const wsl_bridge&) = delete;
  bool wait_first_line(double seconds);
  // Latest raw JSON line, empty if none yet. age_s is seconds since it arrived.
  std::string latest(double& age_s, int& restarts) const;
  // Windows unix time minus WSL unix time, seconds, now. Taken from the lowest-round-trip ping in
  // the last 30 samples and carried forward to the present at the drift rate fitted over the window,
  // because the two clocks can run at different rates (docs/measurable.md, WSL clock rate).
  // False until a sample exists. rtt_s, when given, is the chosen sample's round trip with the
  // collector's hold time removed. A step of the WSL wall clock empties the window.
  bool clock_offset(double& offset_s, double* rtt_s = nullptr) const;
  // Rate of change of that offset, seconds per second, fitted over the window; false with fewer
  // than 5 samples or under 3 s of spread.
  bool clock_drift(double& s_per_s) const;
  int clock_steps() const;

 private:
  void run();
  bool run_once(const std::string& script);
  void on_pong(const std::string& line, double t4_unix, double q4);
  std::wstring script_path_;
  std::wstring distro_;
  struct ping_sent { long long seq; double unix_s; double q; };
  struct sample { double t; double offset; double rtt; };
  std::deque<ping_sent> sent_;
  std::deque<sample> window_;
  double last_wm_ = 0;
  bool have_wm_ = false;
  int steps_ = 0;
  bool drift_locked(double& s_per_s) const;
  long long seq_ = 0;
  mutable std::mutex mu_;
  std::string line_;
  double line_t_ = 0;
  int started_ = 0;
  std::atomic<bool> stop_{false};
  HANDLE job_ = nullptr;
  std::thread thread_;
};

}  // namespace wwt
