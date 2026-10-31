// The fields of the collector's line that the screen shows, wsl-collector/PROTOCOL.md schema 1.
// Private to src/ui: snapshot.wsl_json stays a raw string for everything else.
#pragma once
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
};

struct wsl_view {
  bool ok = false;  // false for an empty line, a malformed one, or another schema
  int64_t schema = 0;
  std::string distro;
  double busy_pct = 0, steal_pct = 0;
  std::vector<double> core_pct;
  uint64_t mem_total = 0, mem_available = 0, mem_cached = 0, swap_used = 0;
  std::optional<double> psi_cpu, psi_memory, psi_io;  // PSI is null where the kernel has none
  int64_t process_count = 0, dxg_denied = 0;
  std::vector<uint32_t> dxg_pids;
  std::vector<wsl_process> processes;
};

wsl_view read_wsl(std::string_view json);

}  // namespace wwt
