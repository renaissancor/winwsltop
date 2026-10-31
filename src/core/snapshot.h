// The shared model. Collectors write it; the recorder and any screen only read it.
// Everything here is measured. Inference about Windows vs WSL belongs in a later layer.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wwt {

struct cpu_view {
  double busy_pct = 0;
  std::vector<double> core_pct;
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
  memory_view memory;
  vm_view vm;
  std::vector<process_row> processes;
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
