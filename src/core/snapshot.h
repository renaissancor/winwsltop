// The shared model. Collectors write it; the recorder and any screen only read it.
// Everything here is measured. Inference about Windows vs WSL belongs in a later layer.
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace wwt {

struct cpu_view {
  double busy_pct = 0;
  std::vector<double> core_pct;
  // Windows' own split of the whole machine, NaN when not read, and user_pct + kernel_pct is busy_pct.
  // kernel_pct has idle taken out and still holds the VM's time: docs/measurable.md, "The VM's time is
  // Windows kernel time". Taking the VM out of it is for whoever draws it.
  double user_pct = std::numeric_limits<double>::quiet_NaN();
  double kernel_pct = std::numeric_limits<double>::quiet_NaN();
};

// One logical CPU, from GroupMask bits and never index arithmetic: docs/measurable.md, "CPU layout".
struct cpu_layout_row {
  uint32_t core = 0;        // Physical core, numbered from 0. Rows with the same core are siblings
  uint32_t thread = 0;      // Place within that core, lowest logical CPU first, so 0 on a one-thread core
  uint32_t efficiency = 0;  // EfficiencyClass: higher is faster, and the same on a machine that is not hybrid
};

// Read once at start, and kept out of cpu_view, which is reset on a failed read. rows[i] is
// cpu.core_pct[i]; known is false with no rows. A reader still compares the two sizes.
struct cpu_layout_view {
  bool known = false;
  std::vector<cpu_layout_row> rows;
};

struct memory_view {
  uint64_t total = 0, available = 0, commit_used = 0, commit_limit = 0, cache = 0;
};

struct process_row {
  uint32_t pid = 0, ppid = 0, threads = 0, handles = 0;
  std::string name;
  double cores = 0;  // 1.0 is one logical CPU busy for the whole interval
  uint64_t working_set = 0, private_bytes = 0;
};

// The WSL VM as Windows sees it. vmwp is vmmemWSL's parent, see docs/measurable.md.
struct vm_view {
  uint32_t vmmem_pid = 0, vmwp_pid = 0;
  uint64_t vmmem_working_set = 0;
  double vmmem_cores = 0;
  // False when vmmem_pid is 0 or on a new vmmemWSL's first sample: vmmem_cores is then an unmeasured 0,
  // which a reader of the figure takes as NaN. A pid and create time seen last sample make it true.
  bool vmmem_cores_known = false;
};

struct disk_row {
  std::string name;
  double read_bytes_s = 0, write_bytes_s = 0, busy_pct = 0;
};

struct net_row {
  std::string name;
  double in_bytes_s = 0, out_bytes_s = 0;
  uint64_t link_bits_s = 0;
  bool hardware = false;
};

struct gpu_engine_row {
  uint32_t pid = 0;
  std::string engine;
  double pct = 0;
};

// One physical GPU, from NVML on Windows. has_* are false where the driver does not answer.
struct gpu_device_view {
  bool present = false;
  std::string name;
  bool has_util = false, has_power = false, has_temp = false, has_clock = false, has_mem = false;
  double util_pct = 0, power_w = 0, power_limit_w = 0, temp_c = 0, sm_mhz = 0;
  uint64_t mem_used = 0, mem_total = 0;
};

// \GPU Process Memory(pid_*) counters. WDDM accounting, not exact physical residency, and
// dedicated is not VRAM in use: see docs/measurable.md. local is the closest per-process figure.
struct gpu_memory_row {
  uint32_t pid = 0;
  uint64_t dedicated = 0, shared = 0, local = 0;
};

// Derived by src/analysis, never by a collector. Percentages are of GPU engine use and are not
// exact shares: measured totals have summed to 88% and peaked at 112%.
struct gpu_split_view {
  bool valid = false;  // false without a known vmwp PID or without engine data
  double vm_pct = 0, windows_pct = 0;
  uint64_t vm_local = 0, windows_local = 0;
};

struct collect_cost {
  double cpu_ms = 0, memory_ms = 0, processes_ms = 0, disk_ms = 0, net_ms = 0, gpu_ms = 0, gpu_device_ms = 0, gpu_memory_ms = 0;
};

struct snapshot {
  int64_t unix_ms = 0;
  double interval_s = 0;
  cpu_view cpu;
  cpu_layout_view cpu_layout;  // Set once at start; no sample() touches it
  memory_view memory;
  vm_view vm;
  std::vector<process_row> processes;
  // Threads in Ready or DeferredReady in the process read, one instant, and vmmemWSL's among them:
  // docs/measurable.md, "A Windows `Ready` count is a free "short of CPU" figure". Empty when not read, never
  // 0, and vm_ready_threads is also empty when vmmemWSL is not found. Only the VM count names a side.
  std::optional<uint32_t> ready_threads, vm_ready_threads;
  std::vector<disk_row> disks;
  std::vector<net_row> nets;
  std::vector<gpu_engine_row> gpu_engines;
  bool gpu_engines_known = false;  // false when the engine counters could not be read this sample
  gpu_device_view gpu_device;
  std::vector<gpu_memory_row> gpu_memory;
  gpu_split_view gpu_split;
  collect_cost cost;
  std::string wsl_json;  // the collector's line as received, or empty
  double wsl_age_s = -1;
  bool wsl_clock_known = false;
  double wsl_clock_offset_s = 0;  // Windows unix time minus WSL unix time
  int wsl_restarts = 0;
};

}  // namespace wwt
