// The last 10 minutes of what the graphs draw, in memory only: nothing here reaches the disk, the snapshot
// or the recorder. It lives in src/ui because only the screen reads it. push() is defined in render.cpp, so
// nothing that includes this header sees the parser; wsl_view itself is core's.
#pragma once
#include "core/snapshot.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wwt {

constexpr int kHistoryThreads = 64;  // A side's threads kept per sample; Windows reads one group, so at most 64

// One sample. A value is NaN where this sample did not measure it, so a graph can show a gap: 0 would
// be a measurement. gpu_mem_pct and gpu_power_pct are heights on a graph; the screen prints the bytes
// and watts from the snapshot. Each value keeps its own source and they are never combined.
struct history_sample {
  int64_t unix_ms = 0;
  double gpu_util = 0, gpu_mem_pct = 0, gpu_power_pct = 0, vm_pct = 0, windows_pct = 0, windows_cpu = 0, wsl_cpu = 0;
  // What the GPU memory and power graphs' labels print, in their own units, NaN wherever the height beside it
  // is: the label covers the samples the graph draws. GiB of the device's memory used, and watts.
  double gpu_mem_gib = 0, gpu_power_w = 0;

  // The large layout of panel 3. Windows' own figures, then the guest's, which differ from wsl_cpu in one
  // way: a repeated line carries its value for one sample, and only a second repeat is a gap. That is
  // docs/design/cpu-panel.md section 5, for this layout only. Floats, because 601 of these are kept.
  float own_kernel = 0, vm_cpu = 0, windows_ram = 0;    // cpu_derived's two, then RAM used, each percent of the machine
  float wsl_busy = 0, wsl_iowait = 0, wsl_ram = 0;
  float lost_vcpus = 0;                                 // Inferred, in vCPUs; a separate unit from the rest
  uint16_t windows_threads = 0, wsl_threads = 0;        // How many of the values below this sample read
  float windows_core[kHistoryThreads] = {}, wsl_core[kHistoryThreads] = {};
};

// The samples in time order. Cheap to copy, and good until the next push.
class sample_range {
 public:
  class iterator {
   public:
    iterator(const sample_range* r, size_t i) : r_(r), i_(i) {}
    const history_sample& operator*() const { return (*r_)[i_]; }
    iterator& operator++() { ++i_; return *this; }
    bool operator!=(const iterator& o) const { return i_ != o.i_; }

   private:
    const sample_range* r_;
    size_t i_;
  };

  sample_range(const history_sample* ring, size_t capacity, size_t first, size_t count) : ring_(ring), capacity_(capacity), first_(first), count_(count) {}
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const history_sample& operator[](size_t i) const { return ring_[(first_ + i) % capacity_]; }
  const history_sample& front() const { return (*this)[0]; }
  const history_sample& back() const { return (*this)[count_ - 1]; }
  iterator begin() const { return {this, 0}; }
  iterator end() const { return {this, count_}; }

 private:
  const history_sample* ring_;
  size_t capacity_, first_, count_;
};

class history {
 public:
  static constexpr int64_t kSpanMs = 60000;   // What a graph's width stands for, unless it is told otherwise
  static constexpr int64_t kKeepMs = 600000;  // What is kept, the longest range a graph can be told to draw

  void push(const snapshot& s);
  sample_range samples() const { return {ring_.data(), ring_.size(), first_, count_}; }

  // When watching began, on the samples' clock, and meaningful only once there is a sample. A graph
  // draws the time before it as not watched yet, which is not a gap: a gap is watched time with no
  // measurement. Kept apart from the samples, which are dropped once they are older than kKeepMs.
  int64_t watched_from_ms() const { return watched_from_ms_; }
  // Whether anything was pushed, even a sample whose time could not be placed.
  bool watching() const { return watching_; }

 private:
  static constexpr int64_t kLatestMs = 4102444800000;  // 2100: past this a time is not a clock reading
  static constexpr size_t kCapacity = 601;             // 10 minutes at the one a second main.cpp pushes, both ends in
  static constexpr int64_t kStepMs = 1000;             // The wall clock this far short of interval_s stepped back

  history_sample& at(size_t i) { return ring_[(first_ + i) % ring_.size()]; }

  std::vector<history_sample> ring_;  // Sized once, by the first push: a sample then allocates nothing
  size_t first_ = 0, count_ = 0;
  int64_t watched_from_ms_ = 0;
  bool watching_ = false;
  std::string last_wsl_;  // The collector's line in the last sample, to tell a new line from a repeat
  int repeats_ = 0;       // Repeated lines in a row
};

}  // namespace wwt
