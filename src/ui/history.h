// The last 60 s of what the graphs draw, in memory only: nothing here reaches the disk, the snapshot
// or the recorder. It lives in src/ui because the WSL figure comes through wsl_view, private to src/ui:
// push() is defined in render.cpp, so nothing that includes this header sees the parser.
#pragma once
#include "core/snapshot.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace wwt {

// One sample. A value is NaN where this sample did not measure it, so a graph can show a gap: 0 would
// be a measurement. gpu_mem_pct and gpu_power_pct are heights on a graph; the screen prints the bytes
// and watts from the snapshot. Each value keeps its own source and they are never combined.
struct history_sample {
  int64_t unix_ms = 0;
  double gpu_util = 0, gpu_mem_pct = 0, gpu_power_pct = 0, vm_pct = 0, windows_pct = 0, windows_cpu = 0, wsl_cpu = 0;
};

class history {
 public:
  static constexpr int64_t kSpanMs = 60000;  // What a graph's width stands for

  void push(const snapshot& s);
  const std::deque<history_sample>& samples() const { return samples_; }

 private:
  static constexpr int64_t kLatestMs = 4102444800000;  // 2100: past this a time is not a clock reading
  static constexpr size_t kMaxSamples = 600;           // Ten a second; main.cpp pushes one

  std::deque<history_sample> samples_;
  std::string last_wsl_;  // The collector's line in the last sample, to tell a new line from a repeat
};

}  // namespace wwt
