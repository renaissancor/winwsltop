// winwsltop: a live screen by default. --text prints one frame, --once one JSON line, --record N
// prints N JSON lines, one a second.
#include "core/collectors.h"
#include "analysis/attribution.h"
#include "record/jsonl.h"
#include "ui/console.h"
#include "wsl/bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int64_t unix_ms() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<int64_t>(u.QuadPart / 10000) - 11644473600000LL;
}

// The tree is mirrored as <root>\build\winwsltop.exe and <root>\wsl-collector\collector.py.
std::wstring collector_path() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring p(path, n);
  p.resize(p.find_last_of(L'\\'));
  return p + L"\\..\\wsl-collector\\collector.py";
}

std::wstring wide(const char* s) {
  const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
  std::wstring w(n > 0 ? static_cast<size_t>(n) - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
  return w;
}

template <class F> double timed(F f) {
  const double t = wwt::seconds_now();
  f();
  return (wwt::seconds_now() - t) * 1000.0;
}

}  // namespace

int main(int argc, char** argv) {
  enum class mode { live, json, text } run = mode::live;
  int lines = 1;
  std::wstring distro;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--once") == 0) run = mode::json;
    else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) { run = mode::json; lines = atoi(argv[++i]); }
    else if (strcmp(argv[i], "--text") == 0) run = mode::text;
    else if (strcmp(argv[i], "--distro") == 0 && i + 1 < argc) distro = wide(argv[++i]);
    else { fprintf(stderr, "usage: winwsltop [--text | --once | --record N] [--distro NAME]\n"); return 2; }
  }

  wwt::cpu_collector cpu;
  wwt::process_collector procs;
  wwt::disk_collector disks;
  wwt::net_collector nets;
  wwt::gpu_engine_collector gpu;
  wwt::gpu_device_collector gpu_device;  // nvmlInit takes about 650 ms
  wwt::gpu_memory_collector gpu_memory;

  wwt::wsl_bridge wsl(collector_path(), distro);
  wsl.wait_first_line(6.0);

  wwt::snapshot s;
  double last = wwt::seconds_now();
  double next = last;
  wwt::console* screen = run == mode::live ? new wwt::console : nullptr;  // restores the terminal when deleted
  for (int i = 0; run == mode::live || i <= lines; ++i) {  // The first pass only primes the deltas.
    if (i) {
      // A fixed deadline: adding up short sleeps drifts, because Windows timers overshoot.
      next += 1.0;
      for (double left = next - wwt::seconds_now(); left > 0 && !wwt::console::stop_requested(); left = next - wwt::seconds_now()) {
        Sleep(left < 0.05 ? static_cast<DWORD>(left * 1000) + 1 : 50);
      }
      if (wwt::console::stop_requested()) break;
    }
    const double now = wwt::seconds_now();
    s.interval_s = now - last;
    last = now;
    s.unix_ms = unix_ms();
    s.cost.cpu_ms = timed([&] { cpu.sample(s.cpu); });
    s.cost.memory_ms = timed([&] { wwt::sample_memory(s.memory); });
    s.cost.processes_ms = timed([&] { procs.sample(s.processes, s.vm); });
    s.cost.disk_ms = timed([&] { disks.sample(s.disks); });
    s.cost.net_ms = timed([&] { nets.sample(s.nets); });
    s.cost.gpu_ms = timed([&] { gpu.sample(s.gpu_engines); });
    s.cost.gpu_device_ms = timed([&] { gpu_device.sample(s.gpu_device); });
    s.cost.gpu_memory_ms = timed([&] { gpu_memory.sample(s.gpu_memory); });
    s.gpu_split = wwt::split_gpu(s);
    s.wsl_json = wsl.latest(s.wsl_age_s, s.wsl_restarts);
    s.wsl_clock_known = wsl.clock_offset(s.wsl_clock_offset_s);
    if (!i) continue;
    if (run == mode::json) {
      const std::string line = wwt::to_json_line(s);
      fwrite(line.data(), 1, line.size(), stdout);
      fputc('\n', stdout);
      fflush(stdout);
    } else if (run == mode::text) {
      const std::string frame = wwt::as_text(wwt::render(s, 131, 50, unix_ms()));
      fwrite(frame.data(), 1, frame.size(), stdout);
      fflush(stdout);
    } else {
      int w = 131, h = 40;
      screen->size(w, h);
      screen->draw(wwt::render(s, w, h, unix_ms()));
    }
  }
  delete screen;
  return 0;
}
