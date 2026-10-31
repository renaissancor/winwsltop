#include "core/collectors.h"
#include <string>

// NVML is reached through nvml.dll at runtime, so the build needs no NVIDIA SDK and the program runs
// without an NVIDIA driver. The types below mirror nvml.h. The library is loaded by full path only:
// a bare name would also search the current directory.
namespace wwt {
namespace {

using nvml_return = int;
constexpr nvml_return nvml_success = 0;
constexpr unsigned nvml_temperature_gpu = 0;
constexpr unsigned nvml_clock_sm = 1;

struct nvml_device_tag;
using nvml_device = nvml_device_tag*;
struct nvml_utilization { unsigned gpu, memory; };
struct nvml_memory_v1 { unsigned long long total, free, used; };
struct nvml_memory_v2 { unsigned version; unsigned long long total, reserved, free, used; };
constexpr unsigned nvml_memory_v2_version = static_cast<unsigned>(sizeof(nvml_memory_v2)) | (2u << 24);

HMODULE load_from(std::wstring dir) {
  dir += L"\\nvml.dll";
  return LoadLibraryExW(dir.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

HMODULE load_nvml() {
  wchar_t dir[MAX_PATH];
  const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
  if (n > 0 && n < MAX_PATH)
    if (HMODULE lib = load_from(std::wstring(dir, n))) return lib;
  const DWORD m = GetEnvironmentVariableW(L"ProgramFiles", dir, MAX_PATH);
  if (m > 0 && m < MAX_PATH) return load_from(std::wstring(dir, m) + L"\\NVIDIA Corporation\\NVSMI");
  return nullptr;
}

template <class F>
void bind(HMODULE lib, const char* name, F& fn) {
  fn = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(lib, name)));
}

}  // namespace

struct gpu_device_collector::impl {
  HMODULE lib = nullptr;
  nvml_device device = nullptr;
  std::string name;
  nvml_return (*init)() = nullptr;
  nvml_return (*shutdown)() = nullptr;
  nvml_return (*handle_by_index)(unsigned, nvml_device*) = nullptr;
  nvml_return (*get_name)(nvml_device, char*, unsigned) = nullptr;
  nvml_return (*utilization)(nvml_device, nvml_utilization*) = nullptr;
  nvml_return (*memory_v1)(nvml_device, nvml_memory_v1*) = nullptr;
  nvml_return (*memory_v2)(nvml_device, nvml_memory_v2*) = nullptr;
  nvml_return (*power_usage)(nvml_device, unsigned*) = nullptr;
  nvml_return (*power_limit)(nvml_device, unsigned*) = nullptr;
  nvml_return (*temperature)(nvml_device, unsigned, unsigned*) = nullptr;
  nvml_return (*clock)(nvml_device, unsigned, unsigned*) = nullptr;
};

gpu_device_collector::gpu_device_collector() {
  HMODULE lib = load_nvml();
  if (!lib) return;
  impl* p = new impl;
  p->lib = lib;
  bind(lib, "nvmlInit_v2", p->init);
  bind(lib, "nvmlShutdown", p->shutdown);
  bind(lib, "nvmlDeviceGetHandleByIndex_v2", p->handle_by_index);
  bind(lib, "nvmlDeviceGetName", p->get_name);
  bind(lib, "nvmlDeviceGetUtilizationRates", p->utilization);
  bind(lib, "nvmlDeviceGetMemoryInfo", p->memory_v1);
  bind(lib, "nvmlDeviceGetMemoryInfo_v2", p->memory_v2);
  bind(lib, "nvmlDeviceGetPowerUsage", p->power_usage);
  bind(lib, "nvmlDeviceGetEnforcedPowerLimit", p->power_limit);
  bind(lib, "nvmlDeviceGetTemperature", p->temperature);
  bind(lib, "nvmlDeviceGetClockInfo", p->clock);

  if (!p->init || !p->shutdown || !p->handle_by_index || p->init() != nvml_success) {
    FreeLibrary(lib);
    delete p;
    return;
  }
  if (p->handle_by_index(0, &p->device) != nvml_success) {
    p->shutdown();
    FreeLibrary(lib);
    delete p;
    return;
  }
  char name[96] = {};
  if (p->get_name && p->get_name(p->device, name, sizeof name) == nvml_success) p->name = name;
  impl_ = p;
}

gpu_device_collector::~gpu_device_collector() {
  if (!impl_) return;
  impl_->shutdown();
  FreeLibrary(impl_->lib);
  delete impl_;
}

// Rebuilt on every call, so a value that stops answering reads as absent and not as stale.
bool gpu_device_collector::sample(gpu_device_view& out) {
  out = {};
  if (!impl_) return false;
  const impl& n = *impl_;
  out.present = true;
  out.name = n.name;

  nvml_utilization util{};
  if (n.utilization && n.utilization(n.device, &util) == nvml_success) {
    out.has_util = true;
    out.util_pct = util.gpu;
  }
  unsigned value = 0;
  if (n.power_usage && n.power_usage(n.device, &value) == nvml_success) {
    out.has_power = true;
    out.power_w = value / 1000.0;
  }
  if (n.power_limit && n.power_limit(n.device, &value) == nvml_success) out.power_limit_w = value / 1000.0;
  if (n.temperature && n.temperature(n.device, nvml_temperature_gpu, &value) == nvml_success) {
    out.has_temp = true;
    out.temp_c = value;
  }
  if (n.clock && n.clock(n.device, nvml_clock_sm, &value) == nvml_success) {
    out.has_clock = true;
    out.sm_mhz = value;
  }

  // v2 keeps driver-reserved memory out of "used", as nvidia-smi does; v1 counts it as used.
  nvml_memory_v2 mem2{};
  mem2.version = nvml_memory_v2_version;
  nvml_memory_v1 mem1{};
  if (n.memory_v2 && n.memory_v2(n.device, &mem2) == nvml_success) {
    out.has_mem = true;
    out.mem_used = mem2.used;
    out.mem_total = mem2.total;
  } else if (n.memory_v1 && n.memory_v1(n.device, &mem1) == nvml_success) {
    out.has_mem = true;
    out.mem_used = mem1.used;
    out.mem_total = mem1.total;
  }
  return true;
}

}  // namespace wwt
