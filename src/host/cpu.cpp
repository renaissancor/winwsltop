#include "core/collectors.h"
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")

namespace wwt {

// Covers the calling thread's processor group: up to 64 logical CPUs.
// out is reset first: a failed read or the priming call leaves a default view, never the previous
// sample (docs/TODO.md item 1).
bool cpu_collector::sample(cpu_view& out) {
  out = {};
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> info(si.dwNumberOfProcessors);
  ULONG got = 0;
  const NTSTATUS st = NtQuerySystemInformation(SystemProcessorPerformanceInformation, info.data(),
                                               static_cast<ULONG>(info.size() * sizeof(info[0])), &got);
  if (st < 0) return false;
  info.resize(got / sizeof(info[0]));

  std::vector<times> now(info.size());
  uint64_t sum_idle = 0, sum_total = 0;
  const bool have_prev = prev_.size() == now.size();
  if (have_prev) out.core_pct.assign(info.size(), 0.0);
  for (size_t i = 0; i < info.size(); ++i) {
    now[i].idle = static_cast<uint64_t>(info[i].IdleTime.QuadPart);
    // KernelTime includes idle time.
    now[i].total = static_cast<uint64_t>(info[i].KernelTime.QuadPart) + static_cast<uint64_t>(info[i].UserTime.QuadPart);
    if (!have_prev) continue;
    const uint64_t d_idle = now[i].idle - prev_[i].idle, d_total = now[i].total - prev_[i].total;
    sum_idle += d_idle;
    sum_total += d_total;
    if (d_total) out.core_pct[i] = 100.0 * (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total));
  }
  prev_ = std::move(now);
  if (have_prev) out.busy_pct = sum_total ? 100.0 * (1.0 - static_cast<double>(sum_idle) / static_cast<double>(sum_total)) : 0.0;
  return have_prev;
}

}  // namespace wwt
