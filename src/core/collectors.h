#pragma once
#include "core/platform.h"
#include "core/snapshot.h"
#include <pdh.h>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace wwt {

class cpu_collector {
 public:
  bool sample(cpu_view& out);
 private:
  struct times { uint64_t idle = 0, total = 0; };
  std::vector<times> prev_;
};

bool sample_memory(memory_view& out);

class process_collector {
 public:
  bool sample(std::vector<process_row>& out, vm_view& vm);
 private:
  using key = std::pair<uint32_t, int64_t>;  // pid + create time: a PID can be reused
  std::vector<unsigned char> buf_;
  std::map<key, uint64_t> prev_;
  double prev_t_ = 0;
};

class disk_collector {
 public:
  disk_collector();
  ~disk_collector();
  disk_collector(const disk_collector&) = delete;
  disk_collector& operator=(const disk_collector&) = delete;
  bool sample(std::vector<disk_row>& out);
 private:
  PDH_HQUERY query_ = nullptr;
  PDH_HCOUNTER read_ = nullptr, write_ = nullptr, idle_ = nullptr;
  bool primed_ = false;
};

class net_collector {
 public:
  bool sample(std::vector<net_row>& out);
 private:
  struct counters { uint64_t in = 0, out = 0; };
  std::map<uint64_t, counters> prev_;
  double prev_t_ = 0;
};

class gpu_engine_collector {
 public:
  gpu_engine_collector();
  ~gpu_engine_collector();
  gpu_engine_collector(const gpu_engine_collector&) = delete;
  gpu_engine_collector& operator=(const gpu_engine_collector&) = delete;
  bool sample(std::vector<gpu_engine_row>& out);
 private:
  PDH_HQUERY query_ = nullptr;
  PDH_HCOUNTER util_ = nullptr;
  bool primed_ = false;
};

// Loads nvml.dll at runtime; present stays false if it is missing.
class gpu_device_collector {
 public:
  gpu_device_collector();
  ~gpu_device_collector();
  gpu_device_collector(const gpu_device_collector&) = delete;
  gpu_device_collector& operator=(const gpu_device_collector&) = delete;
  bool sample(gpu_device_view& out);
 private:
  struct impl;
  impl* impl_ = nullptr;
};

class gpu_memory_collector {
 public:
  gpu_memory_collector();
  ~gpu_memory_collector();
  gpu_memory_collector(const gpu_memory_collector&) = delete;
  gpu_memory_collector& operator=(const gpu_memory_collector&) = delete;
  bool sample(std::vector<gpu_memory_row>& out);
 private:
  PDH_HQUERY query_ = nullptr;
  PDH_HCOUNTER dedicated_ = nullptr, shared_ = nullptr;
};

}  // namespace wwt
