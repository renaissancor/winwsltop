#include "core/collectors.h"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace wwt {

bool sample_memory(memory_view& out) {
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  if (!GlobalMemoryStatusEx(&m)) return false;
  out.total = m.ullTotalPhys;
  out.available = m.ullAvailPhys;
  PERFORMANCE_INFORMATION p{};
  p.cb = sizeof p;
  if (!GetPerformanceInfo(&p, sizeof p)) return false;
  out.commit_used = static_cast<uint64_t>(p.CommitTotal) * p.PageSize;
  out.commit_limit = static_cast<uint64_t>(p.CommitLimit) * p.PageSize;
  out.cache = static_cast<uint64_t>(p.SystemCache) * p.PageSize;
  return true;
}

}  // namespace wwt
