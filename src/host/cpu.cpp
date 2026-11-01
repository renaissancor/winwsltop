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
  uint64_t sum_idle = 0, sum_total = 0, sum_user = 0;
  const bool have_prev = prev_.size() == now.size();
  if (have_prev) out.core_pct.assign(info.size(), 0.0);
  for (size_t i = 0; i < info.size(); ++i) {
    now[i].idle = static_cast<uint64_t>(info[i].IdleTime.QuadPart);
    // KernelTime includes idle time.
    now[i].user = static_cast<uint64_t>(info[i].UserTime.QuadPart);
    now[i].total = static_cast<uint64_t>(info[i].KernelTime.QuadPart) + now[i].user;
    if (!have_prev) continue;
    const uint64_t d_idle = now[i].idle - prev_[i].idle, d_total = now[i].total - prev_[i].total;
    sum_idle += d_idle;
    sum_total += d_total;
    sum_user += now[i].user - prev_[i].user;
    if (d_total) out.core_pct[i] = 100.0 * (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total));
  }
  prev_ = std::move(now);
  if (have_prev) out.busy_pct = sum_total ? 100.0 * (1.0 - static_cast<double>(sum_idle) / static_cast<double>(sum_total)) : 0.0;
  // Ledger: "User and kernel time come from the call `cpu.cpp` already makes" and "The VM's time is
  // Windows kernel time". Over busy's own sum_total: kernel is subtracted across the whole machine, after
  // summing, and clamped there, because the deltas are unsigned. NaN stays on priming.
  if (have_prev && sum_total) {
    const double total = static_cast<double>(sum_total);
    const uint64_t not_kernel = sum_user + sum_idle;
    out.user_pct = 100.0 * static_cast<double>(sum_user) / total;
    out.kernel_pct = sum_total > not_kernel ? 100.0 * static_cast<double>(sum_total - not_kernel) / total : 0.0;
  }
  return have_prev;
}

// Ledger: "CPU layout". The calling thread's group, the one sample() above covers. rows[i] is that
// group's logical CPU i, found from its GroupMask bit, because core records need not be in LP order and
// a core need not have two threads.
bool read_cpu_layout(cpu_layout_view& out) {
  out = {};
  GROUP_AFFINITY group{};
  if (!GetThreadGroupAffinity(GetCurrentThread(), &group)) return false;
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  const size_t count = si.dwNumberOfProcessors;

  DWORD size = 0;
  if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
  std::vector<unsigned char> buf(size);
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &size)) return false;

  std::vector<cpu_layout_row> rows(count);
  uint64_t covered = 0;
  uint32_t core = 0;
  for (DWORD at = 0; at < size;) {
    const auto* r = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + at);
    at += r->Size;
    // A core record always has a GroupCount of 1.
    const GROUP_AFFINITY& mask = r->Processor.GroupMask[0];
    if (r->Relationship != RelationProcessorCore || mask.Group != group.Group) continue;
    uint32_t thread = 0;
    for (size_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
      if (!(mask.Mask >> bit & 1)) continue;
      if (bit >= count) return false;  // sample() has no such index
      rows[bit] = {core, thread++, r->Processor.EfficiencyClass};
      covered |= 1ull << bit;
    }
    ++core;
  }
  if (covered != (count < 64 ? (1ull << count) - 1 : ~0ull)) return false;  // Some index is in no record
  out.rows = std::move(rows);
  out.known = true;
  return true;
}

}  // namespace wwt
