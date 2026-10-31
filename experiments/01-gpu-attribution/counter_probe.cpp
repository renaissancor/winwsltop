// Prints non-zero \GPU Engine(*) instances each second, marking the WSL VM's PID.
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>
#pragma comment(lib, "pdh.lib")

static DWORD find_vmmem(DWORD* parent) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W e{};
  e.dwSize = sizeof e;
  DWORD pid = 0;
  for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
    if (_wcsnicmp(e.szExeFile, L"vmmem", 5) == 0) { pid = e.th32ProcessID; *parent = e.th32ParentProcessID; break; }
  }
  CloseHandle(snap);
  return pid;
}

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? atoi(argv[1]) : 5;
  DWORD vmwp = 0;
  const DWORD vmmem = find_vmmem(&vmwp);
  printf("vmmem_pid %lu vmwp_pid %lu\n", vmmem, vmwp);

  PDH_HQUERY query;
  PDH_HCOUNTER counter;
  if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return 1;
  if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) return 1;
  PdhCollectQueryData(query);

  for (int t = 1; t <= seconds; ++t) {
    Sleep(1000);
    PdhCollectQueryData(query);
    DWORD size = 0, count = 0;
    PDH_STATUS r = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
    if (r != PDH_MORE_DATA) { printf("t=%d no data %08lx\n", t, (unsigned long)r); continue; }
    std::vector<BYTE> buf(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) != ERROR_SUCCESS) continue;
    int shown = 0;
    for (DWORD i = 0; i < count; ++i) {
      if (items[i].FmtValue.CStatus != ERROR_SUCCESS || items[i].FmtValue.doubleValue <= 0.0) continue;
      const wchar_t* pid_at = wcsstr(items[i].szName, L"pid_");
      const wchar_t* eng_at = wcsstr(items[i].szName, L"engtype_");
      const DWORD pid = pid_at ? static_cast<DWORD>(_wtoi(pid_at + 4)) : 0;
      printf("t=%d pid=%lu %s %ls %.2f\n", t, pid, (pid == vmmem || pid == vmwp) ? "[vm]" : "-", eng_at ? eng_at + 8 : L"?",
             items[i].FmtValue.doubleValue);
      ++shown;
    }
    printf("t=%d instances=%lu nonzero=%d\n", t, count, shown);
  }
  PdhCloseQuery(query);
  return 0;
}
