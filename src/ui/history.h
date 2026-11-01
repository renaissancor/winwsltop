// The last 60 s of what the graphs draw, in memory only: nothing here reaches the disk, the snapshot
// or the recorder. It lives in src/ui because the WSL figure comes through wsl_view, private to src/ui.
#pragma once
#include "core/snapshot.h"
#include "ui/wsl_view.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>

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
  static constexpr double kStaleS = 5;       // A collector line older than this is the screen's "stale"

  void push(const snapshot& s) {
    const int64_t t = s.unix_ms;
    if (t <= 0 || t > kLatestMs) return;  // A sample that cannot be placed in time is a gap

    // The wall clock stepped back. interval_s comes from QPC, so the old samples keep their spacing
    // and move with the clock: nothing is lost and nothing lands ahead of "now".
    if (!samples_.empty() && t <= samples_.back().unix_ms) {
      const double gap_s = s.interval_s > 0 && s.interval_s < 3600 ? s.interval_s : 1.0;
      const int64_t shift = samples_.back().unix_ms + static_cast<int64_t>(gap_s * 1000.0) - t;
      for (history_sample& old : samples_) old.unix_ms -= shift;
    }

    const double missing = std::numeric_limits<double>::quiet_NaN();
    const gpu_device_view& g = s.gpu_device;
    history_sample n;
    n.unix_ms = t;
    n.gpu_util = g.present && g.has_util ? g.util_pct : missing;
    n.gpu_mem_pct = g.present && g.has_mem && g.mem_total > 0 ? 100.0 * static_cast<double>(g.mem_used) / static_cast<double>(g.mem_total) : missing;
    n.gpu_power_pct = g.present && g.has_power && g.power_limit_w > 0 ? 100.0 * g.power_w / g.power_limit_w : missing;
    n.vm_pct = s.gpu_split.valid ? s.gpu_split.vm_pct : missing;
    n.windows_pct = s.gpu_split.valid ? s.gpu_split.windows_pct : missing;
    n.windows_cpu = s.cpu.core_pct.empty() ? missing : s.cpu.busy_pct;  // A failed read leaves no cores
    // The bridge keeps its last line after the collector dies: a stale line measures nothing new.
    const wsl_view w = read_wsl(s.wsl_json);
    n.wsl_cpu = w.ok && s.wsl_age_s < kStaleS ? w.busy_pct : missing;
    samples_.push_back(n);

    // One sample older than the span stays, so the left edge is drawn from where that sample ended.
    while (samples_.size() > kMaxSamples ||
           (samples_.size() > 1 && samples_[1].unix_ms < t - kSpanMs)) {
      samples_.pop_front();
    }
  }

  const std::deque<history_sample>& samples() const { return samples_; }

 private:
  static constexpr int64_t kLatestMs = 4102444800000;  // 2100: past this a time is not a clock reading
  static constexpr size_t kMaxSamples = 600;           // Ten a second; main.cpp pushes one

  std::deque<history_sample> samples_;
};

}  // namespace wwt
