#include "core/collectors.h"
#include <winternl.h>
#include <cstddef>
#include <cwchar>
#pragma comment(lib, "ntdll.lib")

namespace wwt {
namespace {

// The full x64 layout; winternl.h hides these fields behind Reserved arrays.
struct nt_process {
  ULONG NextEntryOffset;
  ULONG NumberOfThreads;
  LARGE_INTEGER WorkingSetPrivateSize;
  ULONG HardFaultCount;
  ULONG NumberOfThreadsHighWatermark;
  ULONGLONG CycleTime;
  LARGE_INTEGER CreateTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER KernelTime;
  UNICODE_STRING ImageName;
  LONG BasePriority;
  HANDLE UniqueProcessId;
  HANDLE InheritedFromUniqueProcessId;
  ULONG HandleCount;
  ULONG SessionId;
  ULONG_PTR UniqueProcessKey;
  SIZE_T PeakVirtualSize;
  SIZE_T VirtualSize;
  ULONG PageFaultCount;
  SIZE_T PeakWorkingSetSize;
  SIZE_T WorkingSetSize;
  SIZE_T QuotaPeakPagedPoolUsage;
  SIZE_T QuotaPagedPoolUsage;
  SIZE_T QuotaPeakNonPagedPoolUsage;
  SIZE_T QuotaNonPagedPoolUsage;
  SIZE_T PagefileUsage;
  SIZE_T PeakPagefileUsage;
  SIZE_T PrivatePageCount;
};
static_assert(sizeof(void*) == 8, "x64 only");
static_assert(offsetof(nt_process, ImageName) == 0x38);
static_assert(offsetof(nt_process, UniqueProcessId) == 0x50);
static_assert(offsetof(nt_process, WorkingSetSize) == 0x90);

constexpr NTSTATUS kLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);

}  // namespace

// out and vm are reset first, so a failed query or the priming call leaves them empty, never the
// previous sample (docs/TODO.md item 1).
bool process_collector::sample(std::vector<process_row>& out, vm_view& vm) {
  out.clear();
  vm = {};
  if (buf_.empty()) buf_.resize(1 << 20);
  for (;;) {
    ULONG need = 0;
    const NTSTATUS st = NtQuerySystemInformation(SystemProcessInformation, buf_.data(), static_cast<ULONG>(buf_.size()), &need);
    if (st >= 0) break;
    if (st != kLengthMismatch) return false;
    buf_.resize(static_cast<size_t>(need) + (1 << 16));
  }

  const double now_t = seconds_now();
  const double elapsed = now_t - prev_t_;
  const bool have_prev = prev_t_ != 0 && elapsed > 0;
  std::map<key, uint64_t> now;

  for (size_t at = 0;;) {
    const auto* p = reinterpret_cast<const nt_process*>(buf_.data() + at);
    process_row r;
    r.pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->UniqueProcessId));
    r.ppid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->InheritedFromUniqueProcessId));
    r.threads = p->NumberOfThreads;
    r.handles = p->HandleCount;
    r.working_set = p->WorkingSetSize;
    r.private_bytes = p->PrivatePageCount;
    // Only the System Idle Process (PID 0) has no image name by design; any other nameless row is
    // shown as "?" rather than called Idle (docs/TODO.md item 13).
    if (p->ImageName.Buffer) r.name = utf8({p->ImageName.Buffer, p->ImageName.Length / sizeof(wchar_t)});
    else r.name = r.pid == 0 ? "Idle" : "?";

    const uint64_t cpu = static_cast<uint64_t>(p->UserTime.QuadPart) + static_cast<uint64_t>(p->KernelTime.QuadPart);
    const key k{r.pid, p->CreateTime.QuadPart};
    now[k] = cpu;
    if (have_prev) {
      if (const auto it = prev_.find(k); it != prev_.end() && cpu >= it->second) {
        r.cores = static_cast<double>(cpu - it->second) / 1e7 / elapsed;
      }
    }
    if (_stricmp(r.name.c_str(), "vmmemWSL") == 0) {
      vm.vmmem_pid = r.pid;
      vm.vmwp_pid = r.ppid;
      vm.vmmem_working_set = r.working_set;
      vm.vmmem_cores = r.cores;
    }
    out.push_back(std::move(r));
    if (p->NextEntryOffset == 0) break;
    at += p->NextEntryOffset;
  }
  prev_ = std::move(now);
  prev_t_ = now_t;
  if (!have_prev) { out.clear(); vm = {}; }  // The priming call has no rates: it reports no rows.
  return have_prev;
}

}  // namespace wwt
