// winwsltop: a live screen by default. --text prints one frame, --once one JSON line, --record N
// prints N JSON lines, one a second.
#include "core/collectors.h"
#include "analysis/attribution.h"
#include "record/jsonl.h"
#include "ui/console.h"
#include "ui/history.h"
#include "wsl/bridge.h"
#include <cerrno>
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

#ifdef WWT_RELEASE
// A download sits in a folder others can write to, so only the embedded collector may ever run.
std::wstring collector_path() { return {}; }
#else
// The tree is mirrored as <root>\build\winwsltop.exe and <root>\wsl-collector\collector.py.
std::wstring collector_path() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};  // Unreadable: the bridge falls back to the embedded copy
  std::wstring p(path, n);
  const size_t slash = p.find_last_of(L'\\');
  if (slash == std::wstring::npos) return {};
  p.resize(slash);
  return p + L"\\..\\wsl-collector\\collector.py";
}
#endif

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

constexpr const char* kVersion = "0.1.0";  // scripts/release.sh refuses a tag that differs

void print_help(FILE* to) {
  fprintf(to,
    "winwsltop %s: read-only monitor for Windows and WSL2 sharing one GPU\n"
    "\n"
    "usage: winwsltop [--text | --once | --record N] [--distro NAME]\n"
    "\n"
    "  (no option)    live screen: 1 overall, 2 GPU, 3 CPU, q quit\n"
    "  --text         print one text frame and exit\n"
    "  --once         print one JSON line and exit; about 3 s, about 8 s if WSL does not answer\n"
    "  --record N     print N JSON lines, one a second, N at least 1\n"
    "  --distro NAME  read this WSL distro instead of the default\n"
    "  --version      print the version\n"
    "  --help         print this\n"
    "\n"
    "JSON fields: https://github.com/renaissancor/winwsltop/blob/main/docs/json-output.md\n",
    kVersion);
}

}  // namespace

int main(int argc, char** argv) {
  enum class mode { live, json, text } run = mode::live;
  int lines = 1, modes = 0;
  std::wstring distro;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--once") == 0) { run = mode::json; ++modes; }
    else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
      // A typo must not look like success: atoi turned "abc" into 0, which printed nothing and exited 0.
      // long is 32 bits here, so a value too large only shows as ERANGE, never as n > INT_MAX.
      char* end = nullptr;
      errno = 0;
      const long n = strtol(argv[++i], &end, 10);
      if (errno == ERANGE || *end != '\0' || end == argv[i] || n < 1) { print_help(stderr); return 2; }
      run = mode::json;
      ++modes;
      lines = static_cast<int>(n);
    }
    else if (strcmp(argv[i], "--text") == 0) { run = mode::text; ++modes; }
    else if (strcmp(argv[i], "--distro") == 0 && i + 1 < argc) {
      distro = wide(argv[++i]);
      // The bridge cannot pass these to wsl.exe; refusing here beats 8 s of silence and exit 0.
      if (distro.find_first_of(L" \t\"\r\n") != std::wstring::npos) { print_help(stderr); return 2; }
    }
    else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { print_help(stdout); return 0; }
    else if (strcmp(argv[i], "--version") == 0) { printf("winwsltop %s\n", kVersion); return 0; }
    else { print_help(stderr); return 2; }
  }
  if (modes > 1) { print_help(stderr); return 2; }  // Otherwise the last one silently wins

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
  wwt::history hist;
  wwt::view panel = wwt::view::overall;
  bool have_frame = false, quit = false;  // nothing to draw until the first sample after priming
  const auto draw = [&] {
    int w = 131, h = 40;
    screen->size(w, h);
    // A redirected live run has no panels to switch, so it gets the plain frame.
    if (screen->is_terminal()) screen->draw(wwt::render(s, w, h, unix_ms(), &hist, panel));
    else screen->draw(wwt::render(s, w, h, unix_ms()));
  };
  // Called inside the wait, so a switch redraws the last snapshot at once instead of up to a second later.
  const auto handle_keys = [&] {
    if (!screen) return;
    const wwt::view was = panel;
    for (char key = screen->poll_key(); key != 0; key = screen->poll_key()) {
      if (key == '1') panel = wwt::view::overall;
      else if (key == '2') panel = wwt::view::gpu;
      else if (key == '3') panel = wwt::view::cpu;
      else if (key == 'q') quit = true;
    }
    if (panel != was && have_frame && !quit && screen->is_terminal()) draw();
  };
  for (int i = 0; run == mode::live || i <= lines; ++i) {  // The first pass only primes the deltas.
    if (i) {
      // A fixed deadline: adding up short sleeps drifts, because Windows timers overshoot. After a
      // stall longer than a second (suspend, a stopped debugger) the deadline moves to now instead
      // of being caught up with back-to-back samples (docs/TODO.md item 2).
      next += 1.0;
      if (const double t = wwt::seconds_now(); next < t) next = t;
      handle_keys();  // A pass that overran skips the wait below, and keys must still work.
      for (double left = next - wwt::seconds_now(); left > 0 && !quit && !wwt::console::stop_requested(); left = next - wwt::seconds_now()) {
        Sleep(left < 0.05 ? static_cast<DWORD>(left * 1000) + 1 : 50);
        handle_keys();
      }
      if (quit || wwt::console::stop_requested()) break;
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
    s.cost.gpu_ms = timed([&] { s.gpu_engines_known = gpu.sample(s.gpu_engines); });
    s.cost.gpu_device_ms = timed([&] { gpu_device.sample(s.gpu_device); });
    s.cost.gpu_memory_ms = timed([&] { gpu_memory.sample(s.gpu_memory); });
    s.gpu_split = wwt::split_gpu(s);
    s.wsl_json = wsl.latest(s.wsl_age_s, s.wsl_restarts);
    s.wsl_clock_known = wsl.clock_offset(s.wsl_clock_offset_s);
    if (!i) continue;
    if (screen && screen->is_terminal()) hist.push(s);  // only the live screen draws it
    have_frame = true;
    if (run == mode::json) {
      const std::string line = wwt::to_json_line(s) + "\n";
      // A reader that has gone must end the run, or a pipeline outlives it for N seconds.
      if (fwrite(line.data(), 1, line.size(), stdout) != line.size() || fflush(stdout) != 0) return 1;
    } else if (run == mode::text) {
      const std::string frame = wwt::as_text(wwt::render(s, 131, 50, unix_ms()));
      if (fwrite(frame.data(), 1, frame.size(), stdout) != frame.size() || fflush(stdout) != 0) return 1;
    } else {
      draw();
    }
  }
  delete screen;
  return 0;
}
