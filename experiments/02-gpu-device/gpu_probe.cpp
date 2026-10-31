// Prints gpu_device_collector and gpu_memory_collector once a second, for comparison with
// nvidia-smi and Get-Counter. Also prints both NVML memory calls once, since the collector uses one.
#include "gpu/nvml.cpp"
#include "gpu/pdh_memory.cpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Direct calls, separate from the collector, to show what v1 and v2 each call "used".
static void print_memory_versions() {
  HMODULE lib = wwt::load_nvml();
  if (!lib) return;
  wwt::nvml_return (*init)() = nullptr, (*shutdown)() = nullptr;
  wwt::nvml_return (*handle)(unsigned, wwt::nvml_device*) = nullptr;
  wwt::nvml_return (*v1)(wwt::nvml_device, wwt::nvml_memory_v1*) = nullptr;
  wwt::nvml_return (*v2)(wwt::nvml_device, wwt::nvml_memory_v2*) = nullptr;
  wwt::bind(lib, "nvmlInit_v2", init);
  wwt::bind(lib, "nvmlShutdown", shutdown);
  wwt::bind(lib, "nvmlDeviceGetHandleByIndex_v2", handle);
  wwt::bind(lib, "nvmlDeviceGetMemoryInfo", v1);
  wwt::bind(lib, "nvmlDeviceGetMemoryInfo_v2", v2);
  wwt::nvml_device dev = nullptr;
  if (init && init() == 0 && handle(0, &dev) == 0) {
    wwt::nvml_memory_v1 a{};
    wwt::nvml_memory_v2 b{};
    b.version = wwt::nvml_memory_v2_version;
    const int ra = v1(dev, &a), rb = v2(dev, &b);
    printf("nvml v1 ret=%d used %llu free %llu MiB | v2 ret=%d used %llu free %llu reserved %llu MiB\n", ra, a.used >> 20, a.free >> 20, rb, b.used >> 20, b.free >> 20, b.reserved >> 20);
    shutdown();
  }
  FreeLibrary(lib);
}

int main(int argc, char** argv) {
  using namespace wwt;
  const int rounds = argc > 1 ? atoi(argv[1]) : 3;
  const int top = argc > 2 ? atoi(argv[2]) : 10;

  print_memory_versions();
  double t0 = seconds_now();
  gpu_device_collector device;
  gpu_memory_collector memory;
  printf("construct_ms %.2f\n", (seconds_now() - t0) * 1000);

  for (int r = 0; r < rounds; ++r) {
    if (r) Sleep(1000);
    gpu_device_view d;
    t0 = seconds_now();
    const bool dok = device.sample(d);
    const double dms = (seconds_now() - t0) * 1000;
    std::vector<gpu_memory_row> rows;
    t0 = seconds_now();
    const bool mok = memory.sample(rows);
    const double mms = (seconds_now() - t0) * 1000;

    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("%02d:%02d:%02d.%03d device ok=%d %.2fms present=%d name=\"%s\"\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, dok, dms, d.present, d.name.c_str());
    printf("  util has=%d %.0f%% | power has=%d %.2f W limit %.2f W | temp has=%d %.0f C | sm has=%d %.0f MHz\n",
           d.has_util, d.util_pct, d.has_power, d.power_w, d.power_limit_w, d.has_temp, d.temp_c, d.has_clock, d.sm_mhz);
    printf("  mem used %llu MiB total %llu MiB\n", d.mem_used >> 20, d.mem_total >> 20);

    uint64_t ded = 0, sh = 0;
    for (const auto& row : rows) { ded += row.dedicated; sh += row.shared; }
    printf("  memory ok=%d %.2fms pids=%zu dedicated_sum %.1f MiB shared_sum %.1f MiB\n", mok, mms, rows.size(), ded / 1048576.0, sh / 1048576.0);
    if (r == rounds - 1) {
      std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.dedicated > b.dedicated; });
      for (int i = 0; i < top && i < static_cast<int>(rows.size()); ++i)
        printf("  pid %u dedicated %llu shared %llu\n", rows[i].pid, rows[i].dedicated, rows[i].shared);
    }
  }
  return 0;
}
