// A CPU burner that reports what it got done. One source for both sides, built with g++ in WSL and
// MSVC on Windows. Pure integer work on the stack: no allocation in the loop, no I/O but the report.
// It runs at the priority it inherits and sets no affinity, class, QoS or nice value.
//
//   cpu_burn <threads> <seconds> <label>        run; a report every second on stdout
//   cpu_burn <threads> <seconds> <label> check  print the limits it would obey and exit, no work
//
// The time limit is in this code: every worker reads the clock after every unit and returns at the
// deadline, whoever started it and whatever became of them. <seconds> above kHardLimitSeconds is cut
// to it, so no argument can make it run longer. It handles no signal and needs nobody to stop it.
//
// Tab-separated. Cumulative per-thread rows, one a second:
//   units    work units finished
//   cpu_us   this thread's CPU time as its own OS counts it
//   cycles   QueryThreadCycleTime on Windows, 0 in WSL (the guest has no equivalent call here)
//   cpu      processor it was on at its last look (Windows LP or guest vCPU)
//   moves    how many times that changed, looked at every kLookEvery units
// and bycpu: cumulative units per processor, 20 cells, so a rate per LP or per vCPU can be taken.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#pragma warning(disable : 4324)  // alignas padding is the point
#else
#include <sched.h>
#include <time.h>
#endif

namespace {

constexpr int kHardLimitSeconds = 120;  // longest case in run.sh is below this
constexpr int kMaxThreads = 64;
constexpr int kStepsPerUnit = 200000;   // a fraction of a millisecond
constexpr uint32_t kLookEvery = 64;     // units between looks at the CPU number and thread time
constexpr int kCells = 64;

using clock_type = std::chrono::steady_clock;

long long unix_ms() {
#ifdef _WIN32
  FILETIME f;
  GetSystemTimePreciseAsFileTime(&f);
  const unsigned long long t = (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
  return static_cast<long long>(t / 10000ULL) - 11644473600000LL;
#else
  timespec s;
  clock_gettime(CLOCK_REALTIME, &s);
  return static_cast<long long>(s.tv_sec) * 1000 + s.tv_nsec / 1000000;
#endif
}

int current_cpu() {
#ifdef _WIN32
  return static_cast<int>(GetCurrentProcessorNumber());
#else
  return sched_getcpu();
#endif
}

long long thread_cpu_us() {
#ifdef _WIN32
  FILETIME a, b, k, u;
  if (!GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u)) return -1;
  const unsigned long long ku = (static_cast<unsigned long long>(k.dwHighDateTime) << 32) | k.dwLowDateTime;
  const unsigned long long uu = (static_cast<unsigned long long>(u.dwHighDateTime) << 32) | u.dwLowDateTime;
  return static_cast<long long>((ku + uu) / 10);
#else
  timespec s;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &s) != 0) return -1;
  return static_cast<long long>(s.tv_sec) * 1000000 + s.tv_nsec / 1000;
#endif
}

unsigned long long thread_cycles() {
#ifdef _WIN32
  ULONG64 c = 0;
  QueryThreadCycleTime(GetCurrentThread(), &c);
  return c;
#else
  return 0;
#endif
}

// A serial chain: each step needs the one before, so the rate follows time on the CPU and its clock
// and two threads on one core barely get in each other's way.
uint64_t unit(uint64_t x) {
  for (int i = 0; i < kStepsPerUnit; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    x *= 0x9E3779B97F4A7C15ULL;
  }
  return x;
}

struct alignas(64) worker {
  std::atomic<unsigned long long> units{0}, cycles{0};
  std::atomic<long long> cpu_us{0};
  std::atomic<int> cpu{-1};
  std::atomic<unsigned long long> moves{0};
  std::atomic<unsigned long long> sink{0};
};

struct alignas(64) cell {
  std::atomic<unsigned long long> units{0};
};

std::vector<worker> workers;
std::vector<cell> cells;

void burn(worker& w, clock_type::time_point deadline, uint64_t seed) {
  uint64_t x = seed;
  unsigned long long done = 0;
  uint32_t since_look = 0;
  int cpu = current_cpu();
  w.cpu.store(cpu, std::memory_order_relaxed);
  while (clock_type::now() < deadline) {
    x = unit(x);
    ++done;
    w.units.store(done, std::memory_order_relaxed);
    if (++since_look == kLookEvery) {
      since_look = 0;
      if (cpu >= 0 && cpu < kCells) cells[static_cast<size_t>(cpu)].units.fetch_add(kLookEvery, std::memory_order_relaxed);
      const int now = current_cpu();
      if (now != cpu) {
        w.moves.fetch_add(1, std::memory_order_relaxed);
        cpu = now;
        w.cpu.store(cpu, std::memory_order_relaxed);
      }
      w.cpu_us.store(thread_cpu_us(), std::memory_order_relaxed);
      w.cycles.store(thread_cycles(), std::memory_order_relaxed);
      w.sink.store(x, std::memory_order_relaxed);
    }
  }
  w.cpu_us.store(thread_cpu_us(), std::memory_order_relaxed);
  w.cycles.store(thread_cycles(), std::memory_order_relaxed);
  w.sink.store(x, std::memory_order_relaxed);
}

template <typename Get>
void row(const char* name, int seq, long long t, Get get) {
  printf("%s\t%d\t%lld", name, seq, t);
  for (auto& w : workers) printf("\t%lld", static_cast<long long>(get(w)));
  printf("\n");
}

void report(int seq) {
  const long long t = unix_ms();
  row("units", seq, t, [](worker& w) { return w.units.load(std::memory_order_relaxed); });
  row("cpu_us", seq, t, [](worker& w) { return w.cpu_us.load(std::memory_order_relaxed); });
  row("cycles", seq, t, [](worker& w) { return w.cycles.load(std::memory_order_relaxed); });
  row("cpu", seq, t, [](worker& w) { return w.cpu.load(std::memory_order_relaxed); });
  row("moves", seq, t, [](worker& w) { return w.moves.load(std::memory_order_relaxed); });
  printf("bycpu\t%d\t%lld", seq, t);
  for (int i = 0; i < 20; ++i) printf("\t%llu", cells[static_cast<size_t>(i)].units.load(std::memory_order_relaxed));
  printf("\n");
  fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: cpu_burn <threads> <seconds> <label> [check]\n");
    return 2;
  }
  const int threads = std::min(std::max(atoi(argv[1]), 1), kMaxThreads);
  const int seconds = std::min(std::max(atoi(argv[2]), 0), kHardLimitSeconds);
  printf("start\t%s\t%d\t%d\t%lld\thard_limit\t%d\tsteps_per_unit\t%d\n", argv[3], threads, seconds, unix_ms(), kHardLimitSeconds, kStepsPerUnit);
  fflush(stdout);
  if (argc > 4 && strcmp(argv[4], "check") == 0) return 0;

  const auto begin = clock_type::now();
  const auto deadline = begin + std::chrono::seconds(seconds);
  std::vector<worker> w(static_cast<size_t>(threads));
  std::vector<cell> c(kCells);
  workers.swap(w);
  cells.swap(c);
  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i)
    pool.emplace_back(burn, std::ref(workers[static_cast<size_t>(i)]), deadline, 0x1234567ULL + static_cast<uint64_t>(i));

  int seq = 0;
  report(seq++);
  for (;;) {
    const auto next = begin + std::chrono::seconds(seq);
    if (next >= deadline) break;
    std::this_thread::sleep_until(next);
    report(seq++);
  }
  for (auto& t : pool) t.join();
  report(seq);
  printf("end\t%s\t%lld\n", argv[3], unix_ms());
  fflush(stdout);
  return 0;
}
