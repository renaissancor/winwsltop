// The last 10 minutes of what the graphs draw, in memory only: nothing here reaches the disk, the snapshot
// or the recorder. It lives in src/ui because only the screen reads it. push() is defined in render.cpp, so
// nothing that includes this header sees the parser; wsl_view itself is core's.
#pragma once
#include "core/snapshot.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace wwt {

struct wsl_view;

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
  float own_kernel = 0, vm_cpu = 0;                    // cpu_derived's two, each percent of the machine
  float wsl_busy = 0, wsl_iowait = 0;
  float lost_vcpus = 0;                                 // Inferred, in vCPUs; a separate unit from the rest
  // Panel 4, GiB, each side's own figure and never one across them: Windows used (total less available) and
  // cache, vmmemWSL's working set, the guest's used and cache under the large layout's repeat rule.
  float windows_used = 0, windows_cache = 0, vm_working_set = 0, wsl_used = 0, wsl_cache = 0;
  // The Windows whole each process row is part of, snapshot.process_totals as derived by src/analysis: cores, and
  // working sets in GiB. NaN where it was not known. Windows only: the guest has no whole (design/panel-grammar-
  // slice2.md section 2, dropped on experiment 07).
  float windows_cores_all = 0, windows_ws_all = 0;
  bool gpu_engines_known = false;  // The engine counters read: a process with no engine row then measured 0
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

// A process on one side, as the per-process ring tells it apart: side, pid, its start (Windows' create
// time, the guest's starttime; 0 where unknown) and its name, so a reused pid is another process.
// design/panel-grammar.md part 3, "The per-process ring".
struct proc_id {
  uint8_t side = 0;  // 0 Windows, 1 the guest
  uint32_t pid = 0;
  uint64_t start = 0;
  std::string name;
};

class history {
 public:
  static constexpr int64_t kSpanMs = 60000;   // What a graph's width stands for, unless it is told otherwise
  static constexpr int64_t kKeepMs = 600000;  // What is kept, the longest range a graph can be told to draw

  // keep: processes whose slots are never reused, the rows a screen has selected.
  void push(const snapshot& s, const std::vector<proc_id>& keep = {});
  sample_range samples() const { return {ring_.data(), ring_.size(), first_, count_}; }

  // The per-process ring: one slot a process, a cores and a memory value (GiB) at every ring position, NaN
  // where that sample did not measure it, the first reading included. slot_of is -1 for a process with no
  // slot. A value is read by the position of a sample, position(), so it moves with the samples.
  static constexpr size_t kLeastSlots = 1024;  // Slots kept, or twice the rows of the newest sample if more
  int slot_of(const proc_id& id) const;
  float cores_at(int slot, size_t position) const { return slots_[static_cast<size_t>(slot)].cores[position]; }
  float memory_at(int slot, size_t position) const { return slots_[static_cast<size_t>(slot)].memory[position]; }
  size_t position(const history_sample& p) const { return static_cast<size_t>(&p - ring_.data()); }
  size_t slots_used() const { return index_.size(); }

  // Panel 2: a Windows process's GPU %, busiest engine, at a position, as the list shows it. The series is made
  // only when the process first reads above 0; before that, and for a slot without one, it reads 0 where the
  // process was present (its memory measured) and the engines were read, NaN elsewhere
  // (design/panel-grammar-slice2.md section 5).
  float gpu_at(int slot, size_t position) const {
    const auto& sl = slots_[static_cast<size_t>(slot)];
    if (!sl.gpu.empty()) return sl.gpu[position];
    return !std::isnan(sl.memory[position]) && ring_[position].gpu_engines_known ? 0.0f : std::numeric_limits<float>::quiet_NaN();
  }
  // The newest sample in which the slot's process had an engine above 0, or 0 for none.
  int64_t gpu_seen_ms(int slot) const { return slots_[static_cast<size_t>(slot)].gpu_seen_ms; }
  struct gpu_slot {
    int slot;
    proc_id id;
    int64_t seen_ms;
  };
  // The slots in use whose process has used an engine, with what identifies them, for the list's rows that have
  // left the snapshot.
  std::vector<gpu_slot> gpu_slots() const {
    std::vector<gpu_slot> out;
    for (size_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].used && slots_[i].gpu_seen_ms != 0) out.push_back({static_cast<int>(i), slots_[i].id, slots_[i].gpu_seen_ms});
    }
    return out;
  }
  size_t slots_allocated() const { return slots_.size(); }

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

  struct slot {
    proc_id id;
    bool used = false;
    int64_t seen_ms = 0;              // The newest sample it was in
    std::vector<float> cores, memory;  // kCapacity each, by ring position
    std::vector<float> gpu;            // Empty until the process first reads above 0 on an engine, then kCapacity
    int64_t gpu_seen_ms = 0;           // The newest sample with an engine above 0, or 0
  };
  using slot_key = std::tuple<uint8_t, uint32_t, uint64_t>;
  int take_slot(const proc_id& id, int64_t t, size_t cap, const std::vector<proc_id>& keep);
  void let_go(size_t i);
  void write_processes(const snapshot& s, const wsl_view& w, size_t position, bool guest, const std::vector<proc_id>& keep);

  std::vector<history_sample> ring_;  // Sized once, by the first push: a sample then allocates nothing
  size_t first_ = 0, count_ = 0;
  int64_t watched_from_ms_ = 0;
  bool watching_ = false;
  std::string last_wsl_;  // The collector's line in the last sample, to tell a new line from a repeat
  int repeats_ = 0;       // Repeated lines in a row
  std::vector<slot> slots_;            // Never shrinks: a slot let go is reused, so a push allocates only past the most yet
  std::map<slot_key, int> index_;      // Slots in use, by side, pid and start; the name is checked in the slot
  std::vector<int> free_;              // Slots let go, to reuse first
  std::vector<int> victims_;           // At the cap, this push's reusable slots, least recently seen last
  bool victims_ready_ = false;         // victims_ was gathered for this push
  std::vector<int> found_;             // This push's rows' slots, or -1 for a row with none yet
};

}  // namespace wwt
