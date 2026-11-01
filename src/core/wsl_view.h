// The fields of the collector's line that its readers use, wsl-collector/PROTOCOL.md schema 1. The screen
// and src/analysis read it; snapshot.wsl_json stays a raw string for everything else.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wwt {

struct wsl_process {
  uint32_t pid = 0;
  std::string name, state;
  double cores = 0;
  uint64_t rss = 0;
  bool on_gpu = false;  // Holds /dev/dxg: identity, not usage
  std::optional<uint32_t> threads;  // Empty for null, or from a collector that does not send it
};

struct wsl_view {
  bool ok = false;  // false for an empty line, a malformed one, or another schema
  int64_t schema = 0;
  std::string distro;
  double busy_pct = 0, steal_pct = 0;
  std::optional<double> cpu_user, cpu_sys, cpu_iowait;  // cpu_split: empty for null, or from an older collector
  std::array<std::optional<double>, 3> load;  // 1, 5 and 15 minutes, the same way
  std::vector<double> core_pct;
  uint64_t mem_total = 0, mem_available = 0, mem_cached = 0, swap_used = 0;
  std::optional<double> psi_cpu, psi_memory, psi_io;  // PSI is null where the kernel has none
  int64_t process_count = 0, dxg_denied = 0;
  std::vector<uint32_t> dxg_pids;
  std::vector<wsl_process> processes;
};

wsl_view read_wsl(std::string_view json);

// True when line is the one last seen, and makes it the last seen. The bridge hands over its latest line,
// so the same line twice measured nothing new. The line carries mono_s, so a new line always differs,
// which is why this compares the raw line: wsl_view has no mono_s, and needs none.
inline bool wsl_line_repeats(const std::string& line, std::string& last) {
  const bool repeated = line == last;
  last = line;
  return repeated;
}

}  // namespace wwt
