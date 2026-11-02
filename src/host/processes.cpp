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
  LARGE_INTEGER Io[6];
};
static_assert(sizeof(void*) == 8, "x64 only");
static_assert(offsetof(nt_process, ImageName) == 0x38);
static_assert(offsetof(nt_process, UniqueProcessId) == 0x50);
static_assert(offsetof(nt_process, WorkingSetSize) == 0x90);
static_assert(sizeof(nt_process) == 0x100);  // The thread rows start here

// One thread row, NumberOfThreads of them following each nt_process.
struct nt_thread {
  LARGE_INTEGER KernelTime, UserTime, CreateTime;
  ULONG WaitTime;
  PVOID StartAddress;
  CLIENT_ID ClientId;
  LONG Priority, BasePriority;
  ULONG ContextSwitches, ThreadState, WaitReason;
};
static_assert(sizeof(nt_thread) == 0x50);

constexpr ULONG kReady = 1, kDeferredReady = 7;  // ThreadState, as the kernel numbers them

constexpr NTSTATUS kLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);

}  // namespace

// All four outputs are reset first, so a failed query or the priming call leaves them empty, never the
// previous sample (docs/TODO.md item 1). The Ready counts come from this same call. Ledger: "A Windows
// `Ready` count is a free "short of CPU" figure", "The count of `vmmemWSL` threads in `Ready` tracks lost
// work". They are one instant, so a new vmmemWSL has its count on its first sample, though no cores yet.
bool process_collector::sample(std::vector<process_row>& out, vm_view& vm, std::optional<uint32_t>& ready_threads, std::optional<uint32_t>& vm_ready_threads) {
  out.clear();
  vm = {};
  ready_threads.reset();
  vm_ready_threads.reset();
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
  uint32_t ready = 0;

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
    // Identity for readers: 100 ns since 1601, the same CreateTime this collector keys on. Ledger,
    // "Memory and process collectors checked": constant while a process lives.
    r.create_time = static_cast<uint64_t>(p->CreateTime.QuadPart);
    bool rated = false;
    if (have_prev) {
      if (const auto it = prev_.find(k); it != prev_.end() && cpu >= it->second) {
        r.cores = static_cast<double>(cpu - it->second) / 1e7 / elapsed;
        rated = true;
      }
    }
    // A first reading has no earlier time to subtract: cores is then an unmeasured 0 (design
    // panel-grammar part 3, "A first reading is not a measurement").
    r.cores_known = rated;
    const auto* threads = reinterpret_cast<const nt_thread*>(p + 1);
    uint32_t ready_here = 0;
    for (ULONG i = 0; i < p->NumberOfThreads; ++i) {
      if (threads[i].ThreadState == kReady || threads[i].ThreadState == kDeferredReady) ++ready_here;
    }
    ready += ready_here;
    if (_stricmp(r.name.c_str(), "vmmemWSL") == 0) {
      vm.vmmem_pid = r.pid;
      vm.vmwp_pid = r.ppid;
      vm.vmmem_working_set = r.working_set;
      vm.vmmem_cores = r.cores;
      vm.vmmem_cores_known = rated;
      vm_ready_threads = ready_here;
    }
    out.push_back(std::move(r));
    if (p->NextEntryOffset == 0) break;
    at += p->NextEntryOffset;
  }
  prev_ = std::move(now);
  prev_t_ = now_t;
  if (!have_prev) {  // The priming call has no rates: it reports no rows.
    out.clear();
    vm = {};
    vm_ready_threads.reset();
    return false;
  }
  ready_threads = ready;
  return true;
}

}  // namespace wwt
