// Experiment 04's probe, carried on for a run under load. Non-elevated. Reads only: it starts no load,
// changes no setting, and opens no process. Tab-separated, one record per line.
//   topology   token, OS build, CPUID brand, GetLogicalProcessorInformationEx, CPU sets, power info
//   run <n>    n rounds a second apart: every counter in the tables below, the same NT call
//              src/host/cpu.cpp makes with all its fields, vmmemWSL and vmwp from the process list,
//              cpu_burn's process row, and how many threads sit in each state
//   clock      prints the Windows clock once, for the driver to bracket
// Changed from 04's: the VP table gains five counters, a whole-array read that fails prints its
// status, the state counts are new, and cost mode and the capped re-read are gone.
#include "core/pdh_array.h"
#include <intrin.h>
#include <powerbase.h>
#include <winternl.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "advapi32.lib")

using wwt::utf8;
using wwt::seconds_now;

static unsigned long long filetime_now() {
  FILETIME f;
  GetSystemTimePreciseAsFileTime(&f);
  return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// ---- counters ---------------------------------------------------------------------------------

#define PI L"\\Processor Information(*)\\"
#define SY L"\\System\\"
#define TZ L"\\Thermal Zone Information(*)\\"
#define VP L"\\Hyper-V Hypervisor Virtual Processor(*)\\"
#define LP L"\\Hyper-V Hypervisor Logical Processor(*)\\"
#define RV L"\\Hyper-V Hypervisor Root Virtual Processor(*)\\"
#define WV L"\\Hyper-V Worker Virtual Processor(*)\\"

static const wchar_t* const kPi10[] = {PI L"% Processor Time", PI L"% User Time", PI L"% Privileged Time", PI L"% Interrupt Time",
                                       PI L"% DPC Time", PI L"% Idle Time", PI L"% Processor Performance", PI L"% Processor Utility",
                                       PI L"Processor Frequency", PI L"Parking Status"};
static const wchar_t* const kPiExtra[] = {PI L"Actual Frequency", PI L"% of Maximum Frequency", PI L"% Privileged Utility", PI L"% Performance Limit",
                                          PI L"Performance Limit Flags"};
static const wchar_t* const kSys[] = {SY L"Processor Queue Length", SY L"Context Switches/sec", SY L"System Calls/sec"};
static const wchar_t* const kTz[] = {TZ L"Temperature", TZ L"High Precision Temperature", TZ L"% Passive Limit", TZ L"Throttle Reasons"};
static const wchar_t* const kVp[] = {VP L"% Guest Run Time", VP L"% Hypervisor Run Time", VP L"% Total Run Time", VP L"Logical Processor Dispatches/sec",
                                     VP L"Logical Processor Migrations/sec", VP L"CPU Wait Time Per Dispatch", VP L"CPU Contention Time Per Dispatch",
                                     VP L"CPU Wake Up Time Per Dispatch", VP L"% Total Core Run Time", VP L"% Guest-Relative Utilization"};
static const wchar_t* const kLp[] = {LP L"% Guest Run Time", LP L"% Hypervisor Run Time", LP L"% Total Run Time", LP L"% Idle Time"};
static const wchar_t* const kRv[] = {RV L"% Guest Run Time", RV L"% Total Run Time"};
static const wchar_t* const kWv[] = {WV L"Initial APIC ID"};

struct counter {
  std::wstring path;
  PDH_HCOUNTER h = nullptr;
  std::string names;   // instance list last printed
};

template <size_t N>
static void add(PDH_HQUERY q, std::vector<counter>& out, const wchar_t* const (&paths)[N]) {
  for (const wchar_t* p : paths) {
    counter c;
    c.path = p;
    const PDH_STATUS st = PdhAddEnglishCounterW(q, p, 0, &c.h);
    if (st != ERROR_SUCCESS) {
      printf("add-failed\t%s\t0x%08lx\n", utf8(p).c_str(), static_cast<unsigned long>(st));
      continue;
    }
    out.push_back(std::move(c));
  }
}

// The array, or for a counter with no instances the single value under the instance name "-".
static bool read_counter(int round, const char* path, PDH_HCOUNTER h, DWORD fmt, std::vector<unsigned char>& buf, std::vector<std::string>& names, std::vector<std::string>& values) {
  names.clear();
  values.clear();
  char v[64];
  DWORD size = 0, count = 0;
  const PDH_STATUS st = PdhGetFormattedCounterArrayW(h, fmt, &size, &count, nullptr);
  if (st == PDH_MORE_DATA) {
    buf.resize(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    const PDH_STATUS second = PdhGetFormattedCounterArrayW(h, fmt, &size, &count, items);
    if (second != ERROR_SUCCESS) {
      printf("array-status\t%d\t%s\tsecond\t0x%08lx\n", round, path, static_cast<unsigned long>(second));
      return false;
    }
    for (DWORD i = 0; i < count; ++i) {
      names.push_back(utf8(items[i].szName));
      if (items[i].FmtValue.CStatus == ERROR_SUCCESS) snprintf(v, sizeof v, "%.6f", items[i].FmtValue.doubleValue);
      else snprintf(v, sizeof v, "!%08lx", static_cast<unsigned long>(items[i].FmtValue.CStatus));
      values.push_back(v);
    }
    return true;
  }
  // Experiment 04 left the status of these reads unprinted; a wildcard counter should not get here.
  if (strchr(path, '*')) printf("array-status\t%d\t%s\tfirst\t0x%08lx\n", round, path, static_cast<unsigned long>(st));
  PDH_FMT_COUNTERVALUE one{};
  const PDH_STATUS got = PdhGetFormattedCounterValue(h, fmt, nullptr, &one);
  names.push_back("-");
  if (got == ERROR_SUCCESS && one.CStatus == ERROR_SUCCESS) snprintf(v, sizeof v, "%.6f", one.doubleValue);
  else snprintf(v, sizeof v, "!%08lx", static_cast<unsigned long>(got != ERROR_SUCCESS ? got : one.CStatus));
  values.push_back(v);
  return true;
}

static std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const auto& x : v) { s += '\t'; s += x; }
  return s;
}

// ---- the NT calls -----------------------------------------------------------------------------

// SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION: winternl.h calls DpcTime and InterruptTime Reserved1.
static void print_processor_times(int round) {
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> info(si.dwNumberOfProcessors);
  ULONG got = 0;
  const NTSTATUS st = NtQuerySystemInformation(SystemProcessorPerformanceInformation, info.data(), static_cast<ULONG>(info.size() * sizeof(info[0])), &got);
  if (st < 0) { printf("nt-failed\t%d\t0x%08lx\n", round, static_cast<unsigned long>(st)); return; }
  info.resize(got / sizeof(info[0]));
  const auto row = [&](const char* name, auto get) {
    printf("nt\t%d\t%s", round, name);
    for (const auto& i : info) printf("\t%llu", static_cast<unsigned long long>(get(i)));
    printf("\n");
  };
  using S = SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;
  row("idle", [](const S& i) { return i.IdleTime.QuadPart; });
  row("kernel", [](const S& i) { return i.KernelTime.QuadPart; });
  row("user", [](const S& i) { return i.UserTime.QuadPart; });
  row("dpc", [](const S& i) { return i.Reserved1[0].QuadPart; });
  row("interrupt", [](const S& i) { return i.Reserved1[1].QuadPart; });
  row("interrupt_count", [](const S& i) { return static_cast<long long>(i.Reserved2); });
}

// The same layout src/host/processes.cpp declares, carried on to the thread rows that follow it.
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
  SIZE_T Sizes[12];
  LARGE_INTEGER Io[6];
};
static_assert(offsetof(nt_process, UniqueProcessId) == 0x50);
static_assert(sizeof(nt_process) == 0x100);

struct nt_thread {
  LARGE_INTEGER KernelTime, UserTime, CreateTime;
  ULONG WaitTime;
  PVOID StartAddress;
  CLIENT_ID ClientId;
  LONG Priority, BasePriority;
  ULONG ContextSwitches, ThreadState, WaitReason;
};
static_assert(sizeof(nt_thread) == 0x50);

// ThreadState values as the kernel numbers them: 0 Initialized, 1 Ready, 2 Running, 3 Standby,
// 4 Terminated, 5 Waiting, 6 Transition, 7 DeferredReady, 8 GateWait, 9 WaitingForProcessOutSwap.
static constexpr int kStates = 10;

static void print_processes(int round, std::vector<unsigned char>& buf) {
  for (;;) {
    ULONG need = 0;
    const NTSTATUS st = NtQuerySystemInformation(SystemProcessInformation, buf.data(), static_cast<ULONG>(buf.size()), &need);
    if (st >= 0) break;
    if (st != static_cast<NTSTATUS>(0xC0000004L)) { printf("proc-failed\t%d\t0x%08lx\n", round, static_cast<unsigned long>(st)); return; }
    buf.resize(static_cast<size_t>(need) + (1 << 16));
  }
  uintptr_t vmwp = 0;
  const auto walk = [&](auto visit) {
    for (size_t at = 0;;) {
      const auto* p = reinterpret_cast<const nt_process*>(buf.data() + at);
      visit(p);
      if (p->NextEntryOffset == 0) break;
      at += p->NextEntryOffset;
    }
  };
  const auto is = [](const nt_process* p, const wchar_t* name) {
    return p->ImageName.Buffer && _wcsnicmp(p->ImageName.Buffer, name, p->ImageName.Length / sizeof(wchar_t)) == 0 && wcslen(name) == p->ImageName.Length / sizeof(wchar_t);
  };
  const auto states = [](const nt_process* p, unsigned (&n)[kStates + 1]) {
    const auto* t = reinterpret_cast<const nt_thread*>(p + 1);
    for (ULONG i = 0; i < p->NumberOfThreads; ++i) ++n[t[i].ThreadState < kStates ? t[i].ThreadState : kStates];
  };
  // Scopes never overlap: idle is PID 0, rest is every other process, burners and the VM excluded.
  unsigned idle[kStates + 1] = {}, vm[kStates + 1] = {}, burn[kStates + 1] = {}, self[kStates + 1] = {}, rest[kStates + 1] = {};
  const auto me = static_cast<uintptr_t>(GetCurrentProcessId());
  walk([&](const nt_process* p) { if (is(p, L"vmmemWSL")) vmwp = reinterpret_cast<uintptr_t>(p->InheritedFromUniqueProcessId); });
  walk([&](const nt_process* p) {
    const bool vmmem = is(p, L"vmmemWSL");
    const bool burner = is(p, L"cpu_burn.exe");
    const uintptr_t pid = reinterpret_cast<uintptr_t>(p->UniqueProcessId);
    const bool worker = pid == vmwp && vmwp != 0;
    if (pid == 0) states(p, idle);
    else if (vmmem) states(p, vm);
    else if (burner) states(p, burn);
    else if (pid == me) states(p, self);
    else states(p, rest);
    const bool other = is(p, L"vmwp.exe") && !worker;
    if (!vmmem && !worker && !other && !burner) return;
    printf("proc\t%d\t%s\t%llu\t%lld\t%lld\t%llu\t%lu\n", round, vmmem ? "vmmemWSL" : worker ? "vmwp" : burner ? "cpu_burn" : "vmwp-other",
           static_cast<unsigned long long>(pid), p->KernelTime.QuadPart, p->UserTime.QuadPart, p->CycleTime, p->NumberOfThreads);
    if (!vmmem) return;
    const auto* t = reinterpret_cast<const nt_thread*>(p + 1);
    for (ULONG i = 0; i < p->NumberOfThreads; ++i) {
      printf("thr\t%d\t%llu\t%lld\t%lld\t%lu\t%lu\t%lu\t%ld\n", round, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(t[i].ClientId.UniqueThread)),
             t[i].KernelTime.QuadPart, t[i].UserTime.QuadPart, t[i].ContextSwitches, t[i].ThreadState, t[i].WaitReason, t[i].Priority);
    }
  });
  const auto row = [&](const char* name, const unsigned (&n)[kStates + 1]) {
    printf("states\t%d\t%s", round, name);
    for (int i = 0; i <= kStates; ++i) printf("\t%u", n[i]);
    printf("\n");
  };
  row("idle", idle);
  row("vmmemWSL", vm);
  row("cpu_burn", burn);
  row("probe", self);
  row("rest", rest);
}

struct power_info { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; };

static void print_power(int round) {
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  std::vector<power_info> p(si.dwNumberOfProcessors);
  const LONG st = CallNtPowerInformation(ProcessorInformation, nullptr, 0, p.data(), static_cast<ULONG>(p.size() * sizeof(p[0])));
  if (st != 0) { printf("power-failed\t%d\t0x%08lx\n", round, static_cast<unsigned long>(st)); return; }
  const auto row = [&](const char* name, ULONG power_info::*f) {
    printf("power\t%d\t%s", round, name);
    for (const auto& i : p) printf("\t%lu", i.*f);
    printf("\n");
  };
  row("max_mhz", &power_info::MaxMhz);
  row("current_mhz", &power_info::CurrentMhz);
  row("limit_mhz", &power_info::MhzLimit);
}

// ---- topology ---------------------------------------------------------------------------------

static bool in_group(WELL_KNOWN_SID_TYPE t) {
  BYTE sid[SECURITY_MAX_SID_SIZE];
  DWORD n = sizeof sid;
  BOOL member = FALSE;
  if (!CreateWellKnownSid(t, nullptr, sid, &n)) return false;
  return CheckTokenMembership(nullptr, sid, &member) && member;
}

static void print_token() {
  HANDLE tok = nullptr;
  TOKEN_ELEVATION e{};
  DWORD n = 0;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
    GetTokenInformation(tok, TokenElevation, &e, sizeof e, &n);
    CloseHandle(tok);
  }
  printf("token\televated\t%lu\n", e.TokenIsElevated);
  printf("token\tadministrators_enabled\t%d\n", in_group(WinBuiltinAdministratorsSid) ? 1 : 0);
  printf("token\tperformance_monitor_users\t%d\n", in_group(WinBuiltinPerfMonitoringUsersSid) ? 1 : 0);
  printf("token\tperformance_log_users\t%d\n", in_group(WinBuiltinPerfLoggingUsersSid) ? 1 : 0);
  printf("token\thyper_v_administrators\t%d\n", in_group(WinBuiltinHyperVAdminsSid) ? 1 : 0);
}

static void print_os() {
  using get_t = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof v;
  const auto get = reinterpret_cast<get_t>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
  if (get && get(&v) == 0) printf("os\tversion\t%lu.%lu.%lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
  DWORD ubr = 0, size = sizeof ubr;
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size) == ERROR_SUCCESS)
    printf("os\tubr\t%lu\n", ubr);
}

static void print_cpuid() {
  int r[4];
  char brand[49] = {};
  for (int i = 0; i < 3; ++i) {
    __cpuid(r, static_cast<int>(0x80000002u + static_cast<unsigned>(i)));
    memcpy(brand + 16 * i, r, 16);
  }
  printf("cpuid\tbrand\t%s\n", brand);
  __cpuid(r, 1);
  printf("cpuid\tleaf1_eax\t0x%08x\n", static_cast<unsigned>(r[0]));
  printf("cpuid\thypervisor_bit\t%d\n", (static_cast<unsigned>(r[2]) >> 31) & 1);
  __cpuid(r, 0);
  if (r[0] >= 0x16) {
    __cpuid(r, 0x16);
    printf("cpuid\tleaf16_base_max_bus_mhz\t%d\t%d\t%d\n", r[0] & 0xffff, r[1] & 0xffff, r[2] & 0xffff);
  }
}

static std::string lps(KAFFINITY mask) {
  std::string s;
  for (int i = 0; i < 64; ++i) {
    if (mask & (static_cast<KAFFINITY>(1) << i)) {
      if (!s.empty()) s += ',';
      s += std::to_string(i);
    }
  }
  return s;
}

static void print_layout() {
  DWORD size = 0;
  GetLogicalProcessorInformationEx(RelationAll, nullptr, &size);
  std::vector<unsigned char> buf(size);
  if (!GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &size)) {
    printf("glpie-failed\t%lu\n", GetLastError());
    return;
  }
  printf("active\tgroups\t%u\n", GetActiveProcessorGroupCount());
  printf("active\tprocessors\t%lu\n", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  int core = 0;
  printf("core-head\tcore\tefficiency_class\tsmt_flag\tgroup\tlogical_processors\n");
  for (DWORD at = 0; at < size;) {
    const auto* r = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + at);
    if (r->Relationship == RelationProcessorCore) {
      printf("core\t%d\t%u\t%d\t%u\t%s\n", core++, r->Processor.EfficiencyClass, (r->Processor.Flags & LTP_PC_SMT) ? 1 : 0, r->Processor.GroupMask[0].Group,
             lps(r->Processor.GroupMask[0].Mask).c_str());
    } else if (r->Relationship == RelationProcessorPackage) {
      printf("package\t%s\n", lps(r->Processor.GroupMask[0].Mask).c_str());
    } else if (r->Relationship == RelationNumaNode) {
      printf("numa\t%lu\t%s\n", r->NumaNode.NodeNumber, lps(r->NumaNode.GroupMask.Mask).c_str());
    } else if (r->Relationship == RelationCache) {
      printf("cache\tL%u\ttype%d\t%lu\t%s\n", r->Cache.Level, static_cast<int>(r->Cache.Type), r->Cache.CacheSize, lps(r->Cache.GroupMask.Mask).c_str());
    } else if (r->Relationship == RelationGroup) {
      printf("group\t%u\t%u\t%u\n", r->Group.ActiveGroupCount, r->Group.GroupInfo[0].ActiveProcessorCount, r->Group.GroupInfo[0].MaximumProcessorCount);
    }
    at += r->Size;
  }
}

static void print_cpu_sets() {
  ULONG size = 0;
  GetSystemCpuSetInformation(nullptr, 0, &size, GetCurrentProcess(), 0);
  std::vector<unsigned char> buf(size);
  if (!GetSystemCpuSetInformation(reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buf.data()), size, &size, GetCurrentProcess(), 0)) {
    printf("cpuset-failed\t%lu\n", GetLastError());
    return;
  }
  printf("cpuset-head\tid\tgroup\tlp\tcore_index\tllc\tnuma\tefficiency_class\tscheduling_class\tflags\n");
  for (ULONG at = 0; at < size;) {
    const auto* s = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buf.data() + at);
    const auto& c = s->CpuSet;
    printf("cpuset\t%lu\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t0x%02x\n", c.Id, c.Group, c.LogicalProcessorIndex, c.CoreIndex, c.LastLevelCacheIndex, c.NumaNodeIndex,
           c.EfficiencyClass, c.SchedulingClass, c.AllFlags);
    at += s->Size;
  }
}

// ---- modes ------------------------------------------------------------------------------------

static int run(int rounds) {
  print_token();
  PDH_HQUERY q = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) { printf("open-failed\n"); return 1; }
  std::vector<counter> c;
  add(q, c, kPi10);
  add(q, c, kPiExtra);
  add(q, c, kSys);
  add(q, c, kTz);
  add(q, c, kVp);
  add(q, c, kLp);
  add(q, c, kRv);
  add(q, c, kWv);
  std::vector<unsigned char> pdh_buf, proc_buf(1 << 20);
  std::vector<std::string> names, values;
  for (int r = 0; r < rounds; ++r) {
    const double t0 = seconds_now();
    printf("round\t%d\t%llu\t%.6f\n", r, filetime_now(), t0);
    print_processor_times(r);
    const PDH_STATUS st = PdhCollectQueryData(q);
    if (st != ERROR_SUCCESS) printf("collect-failed\t%d\t0x%08lx\n", r, static_cast<unsigned long>(st));
    print_processes(r, proc_buf);
    print_power(r);
    if (r > 0) {
      for (auto& x : c) {
        const std::string path = utf8(x.path);
        if (!read_counter(r, path.c_str(), x.h, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, pdh_buf, names, values)) {
          printf("read-failed\t%d\t%s\n", r, path.c_str());
          continue;
        }
        const std::string n = joined(names);
        if (n != x.names) {
          printf("names\t%d\t%s%s\n", r, path.c_str(), n.c_str());
          x.names = n;
        }
        printf("pdh\t%d\t%s%s\n", r, path.c_str(), joined(values).c_str());
      }
    }
    fflush(stdout);
    const double spent = seconds_now() - t0;
    if (r + 1 < rounds && spent < 1.0) Sleep(static_cast<DWORD>((1.0 - spent) * 1000));
  }
  PdhCloseQuery(q);
  return 0;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "topology") {
    print_token();
    print_os();
    print_cpuid();
    print_layout();
    print_cpu_sets();
    print_power(0);
    return 0;
  }
  if (mode == "run" && argc > 2) return run(atoi(argv[2]));
  if (mode == "clock") { printf("clock\t%llu\n", filetime_now()); return 0; }
  fprintf(stderr, "usage: contention_probe topology | run <rounds> | clock\n");
  return 2;
}
