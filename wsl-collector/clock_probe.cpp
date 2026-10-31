// Manual test: build with `scripts/build.sh wsl-collector/clock_probe.cpp`.
// Usage: clock_probe [seconds] [distro] [script path, or "-" for the embedded copy]
#include "../src/wsl/bridge.cpp"
#include "../src/wsl/collector_embed.cpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  const int secs = argc > 1 ? atoi(argv[1]) : 25;
  std::wstring distro = argc > 2 ? std::wstring(argv[2], argv[2] + strlen(argv[2])) : L"";
  std::wstring path = L"missing.py";
  if (argc > 3 && strcmp(argv[3], "-") != 0) path = std::wstring(argv[3], argv[3] + strlen(argv[3]));
  wwt::wsl_bridge b(path, distro);
  printf("first line: %d\n", b.wait_first_line(10.0));
  for (int i = 0; i < secs; ++i) {
    Sleep(1000);
    double age, off = 0, rtt = 0, drift = 0;
    int restarts;
    const std::string l = b.latest(age, restarts);
    const bool ok = b.clock_offset(off, &rtt);
    const bool dk = b.clock_drift(drift);
    printf("t=%2d offset=%s%.6f s rtt=%.3f ms drift=%s%.1f ms/s steps=%d restarts=%d age=%.2f len=%zu\n", i, ok ? "" : "n/a ", off, rtt * 1000, dk ? "" : "n/a ", drift * 1000,
           b.clock_steps(), restarts, age, l.size());
    fflush(stdout);
  }
}
