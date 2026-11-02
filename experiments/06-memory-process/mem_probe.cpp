// Prints what src/host/memory.cpp and src/host/processes.cpp read, for comparison with Get-Counter and
// Get-Process. Reads only: it starts no load, changes no setting, and opens no process.
//   once   one round: the memory collector's fields, then the process collector's rows, with the
//          create time it keys each row on. Process names outside KEEP print as "other": this repo is
//          public, and pid, sizes and create time are enough to join the rows.
// The collector holds create times only in its private map, which is opened below, so the create time
// printed is the one the collector read, not a second reading. Tab-separated, one record per line.
#include "core/platform.h"
#include "core/snapshot.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define private public
#include "core/collectors.h"
#undef private
#include "host/memory.cpp"
#include "host/processes.cpp"

// UTC, 100 ns since 1601, the clock every Windows line in the results is aligned on.
static unsigned long long filetime_now() {
  FILETIME f;
  GetSystemTimePreciseAsFileTime(&f);
  return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// Windows' own processes and the WSL VM's, named; the rest are masked. windows.ps1 holds the same list.
static const char* const KEEP[] = {"Idle", "System", "Registry", "smss.exe", "csrss.exe", "wininit.exe",
                                   "services.exe", "lsass.exe", "svchost.exe", "winlogon.exe", "dwm.exe",
                                   "explorer.exe", "fontdrvhost.exe", "conhost.exe", "vmmemWSL", "vmwp.exe",
                                   "vmcompute.exe", "wslservice.exe", "wslhost.exe", "wslrelay.exe", "wsl.exe",
                                   "ping.exe", "powershell.exe", "mem_probe.exe", "winwsltop.exe"};

static const char* shown(const std::string& name) {
  for (const char* k : KEEP)
    if (_stricmp(k, name.c_str()) == 0) return k;
  return "other";
}

int main(int argc, char** argv) {
  if (argc < 2 || std::strcmp(argv[1], "once") != 0) {
    std::fprintf(stderr, "usage: mem_probe once\n");
    return 2;
  }
  std::printf("t\tmemory_start\t%llu\n", filetime_now());
  wwt::memory_view m;
  const bool mem_ok = wwt::sample_memory(m);
  std::printf("t\tmemory_end\t%llu\n", filetime_now());
  std::printf("mem\t%d\t%llu\t%llu\t%llu\t%llu\t%llu\n", mem_ok ? 1 : 0, (unsigned long long)m.total,
              (unsigned long long)m.available, (unsigned long long)m.commit_used,
              (unsigned long long)m.commit_limit, (unsigned long long)m.cache);

  wwt::process_collector pc;
  std::vector<wwt::process_row> rows;
  wwt::vm_view vm;
  std::optional<uint32_t> ready, vm_ready;
  pc.sample(rows, vm, ready, vm_ready);  // priming: no rows
  Sleep(200);
  std::printf("t\tprocess_start\t%llu\n", filetime_now());
  const bool proc_ok = pc.sample(rows, vm, ready, vm_ready);
  std::printf("t\tprocess_end\t%llu\n", filetime_now());
  std::map<uint32_t, long long> create;
  for (const auto& [k, cpu] : pc.prev_) create[k.first] = k.second;
  std::printf("proc_ok\t%d\t%zu\n", proc_ok ? 1 : 0, rows.size());
  for (const auto& r : rows) {
    const auto it = create.find(r.pid);
    std::printf("proc\t%u\t%s\t%lld\t%llu\t%llu\n", r.pid, shown(r.name), it == create.end() ? -1LL : it->second,
                (unsigned long long)r.working_set, (unsigned long long)r.private_bytes);
  }
  std::printf("vm\t%u\t%u\t%llu\n", vm.vmmem_pid, vm.vmwp_pid, (unsigned long long)vm.vmmem_working_set);
  return 0;
}
