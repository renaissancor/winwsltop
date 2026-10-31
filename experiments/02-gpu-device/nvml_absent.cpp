// Runs gpu_device_collector with nvml.dll unavailable, without touching the machine: every load
// made by nvml.cpp is redirected. "missing" fails the load; "wrong" hands back a real DLL with no
// NVML exports (kernel32), standing in for a corrupt or unrelated nvml.dll.
#include "core/platform.h"
#include <cstdio>
#include <cstring>

static bool g_wrong = false;
static int g_loads = 0;
static HMODULE WINAPI fake_load(LPCWSTR, HANDLE, DWORD) {
  ++g_loads;
  return g_wrong ? LoadLibraryW(L"kernel32.dll") : nullptr;
}
#define LoadLibraryExW fake_load
#include "gpu/nvml.cpp"
#undef LoadLibraryExW

int main(int argc, char** argv) {
  g_wrong = argc > 1 && strcmp(argv[1], "wrong") == 0;
  int failures = 0;
  {
    wwt::gpu_device_collector c;
    wwt::gpu_device_view v;
    v.present = true;
    v.has_util = true;
    const bool ok = c.sample(v);
    printf("mode=%s loads=%d sample=%d present=%d has_util=%d\n", g_wrong ? "wrong" : "missing", g_loads, ok, v.present, v.has_util);
    failures = ok || v.present || v.has_util;
  }
  printf("destructor ok\n");
  return failures;
}
