// Checks lost_tracker through the real code: hand-built samples for each clause of the gate, then a replay
// of experiment 05's record through read_wsl and update(). No load, no live machine, same result each run.
// Usage: lost_check <experiments/05-cpu-contention/results>   (scripts/check.sh --windows runs it)
// Build: scripts/build.sh scripts/lost_check.cpp   (it includes the sources it tests)
//
// The slim record lacks what the experiment's binary did not write or slim_record.py cut, so the replay fills in:
//   vmmem_cores_known  true when the pid equals the last record's, which is the collector's rule less the create time
//   schema, memory     absent from the slim guest line and required by read_wsl, so an empty pair is put back
//   kernel_pct, ready_threads, vm_ready_threads   not in the record: only the hand-built cases check cpu_derived
#include "analysis/lost.cpp"
#include "core/wsl_view.cpp"
#include "record/jsonl.cpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace wwt;

namespace {

int g_failed = 0, g_checks = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) { ++g_failed; printf("FAIL %s\n", what.c_str()); }
}

bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }

// ---- Hand-built samples ---------------------------------------------------------------------------

struct feed {
  lost_tracker tracker;
  snapshot s;
  int line = 0;  // Stands in for mono_s, so a new number is a new line

  feed() {
    s.interval_s = 1.0;
    s.cpu.busy_pct = 100;
    s.cpu.core_pct.assign(20, 100.0);
    s.cpu.kernel_pct = 90;
    s.vm.vmmem_pid = 7;
    s.vm.vmmem_cores_known = true;
    s.vm.vmmem_cores = 6;
    s.wsl_age_s = 0.5;
    s.ready_threads = 40;
    s.vm_ready_threads = 13;
    guest(100);
  }
  // One sample as main.cpp would see it: a new guest line unless repeat says otherwise.
  int vcpus = 20;
  void guest(double busy) {
    std::string cores;
    for (int i = 0; i < vcpus; ++i) cores += i ? ",0" : "0";
    s.wsl_json = "{\"schema\":1,\"mono_s\":" + std::to_string(++line) + ",\"busy_pct\":" + std::to_string(busy) +
                 ",\"core_pct\":[" + cores + "],\"memory\":{}}";
  }
  lost_state step(double busy = 100) { guest(busy); tracker.update(s); return s.lost.state; }
  lost_state repeat() { tracker.update(s); return s.lost.state; }
  void fill(int n, double busy = 100) { for (int i = 0; i < n; ++i) step(busy); }
};

void gate_cases() {
  {
    feed f;
    for (int i = 0; i < 4; ++i) {
      check(f.step() == lost_state::collecting, "collecting until 5 samples");
      check(std::isnan(f.s.lost.vcpus), "collecting is NaN, not 0");
    }
    check(f.step() == lost_state::known, "known on the fifth");
    check(near(f.s.lost.vcpus, 14, 1e-9), "20 vCPUs less 6 cores is 14");
    f.s.vm.vmmem_cores = 25;
    f.fill(5);
    check(f.s.lost.state == lost_state::known && f.s.lost.vcpus == 0, "clamped at 0, and a clamped 0 is known");
  }
  {
    feed f;
    check(f.step(50) == lost_state::collecting, "an idle guest still says collecting first");
    f.fill(3, 100);
    check(f.step() == lost_state::guest_under_99, "one guest sample under 99 in the 5");
    check(std::isnan(f.s.lost.vcpus), "gated is NaN");
    feed g;
    g.fill(4, 99);
    check(g.step(99) == lost_state::known, "99 is inside the gate");
    feed h;
    h.fill(4, 100);
    check(h.step(98.99) == lost_state::guest_under_99, "98.99 is outside it");
    h.fill(4);
    check(h.s.lost.state == lost_state::guest_under_99, "the low sample holds for the rest of its window");
    check(h.step() == lost_state::known, "and leaves after five");
  }
  {
    feed f;
    f.s.cpu.busy_pct = 20;
    f.fill(4);
    f.s.cpu.busy_pct = 95;
    check(f.step() == lost_state::known, "only the newest Windows busy gates, and 95 is inside");
    f.s.cpu.busy_pct = 94.99;
    check(f.step() == lost_state::windows_under_95, "94.99 is outside");
    check(std::isnan(f.s.lost.vcpus), "gated is NaN");
    f.s.cpu.busy_pct = 100;
    check(f.step() == lost_state::known, "the Windows gate holds no window of its own");
  }
  {
    feed f;
    f.fill(5);
    check(f.repeat() == lost_state::known, "one repeat carries");
    check(near(f.s.lost.vcpus, 14, 1e-9), "and the value stays");
    check(f.repeat() == lost_state::no_data, "two in a row reset");
    check(std::isnan(f.s.lost.vcpus), "reset is NaN");
    check(f.repeat() == lost_state::no_data, "a third is still nothing");
    check(f.step() == lost_state::collecting, "a new line starts again");
    f.fill(3);
    check(f.s.lost.state == lost_state::collecting, "from 1, not from where it was");
    check(f.step() == lost_state::known, "five new samples later");

    feed g;  // The carried value is the guest's: Windows' figure still moves in that sample.
    g.fill(2);
    g.fill(1, 100);
    g.s.vm.vmmem_cores = 16;
    g.repeat();
    g.s.vm.vmmem_cores = 6;
    g.fill(1);
    g.fill(1);
    check(g.s.lost.state == lost_state::known && near(g.s.lost.vcpus, 20 - 8, 1e-9), "a repeat counts as a sample with the VM's new figure");

    feed h;  // Nothing before the reset is carried across it.
    h.fill(3);
    h.s.interval_s = 3;
    h.repeat();
    h.s.interval_s = 1;
    check(h.s.lost.state == lost_state::collecting, "a repeat with nothing to carry is not a sample");
    h.fill(4);
    check(h.s.lost.state == lost_state::collecting, "so 4 more are still short of 5");
    check(h.step() == lost_state::known, "and the fifth is known");
  }
  {
    feed f;
    f.fill(5);
    f.s.wsl_age_s = 5;
    check(f.step() == lost_state::no_data, "age at kStaleS is stale");
    f.s.wsl_age_s = 4.99;
    check(f.step() == lost_state::collecting, "stale reset, and under kStaleS counts again");
    f.s.wsl_age_s = -1;
    check(f.step() == lost_state::no_data, "no line yet");
    f.s.wsl_age_s = 0.5;

    const char* const bad[] = {"", "{", "{\"schema\":2,\"busy_pct\":100,\"core_pct\":[0],\"memory\":{}}",
                               "{\"schema\":1,\"busy_pct\":100,\"core_pct\":[],\"memory\":{}}"};
    for (const char* line : bad) {
      feed g;
      g.fill(5);
      g.s.wsl_json = line;
      g.tracker.update(g.s);
      check(g.s.lost.state == lost_state::no_data && std::isnan(g.s.lost.vcpus), std::string("unreadable line: ") + line);
      check(g.step() == lost_state::collecting, "and it reset");
    }
  }
  {
    feed f;
    f.fill(5);
    f.s.interval_s = 2.5;
    check(f.step() == lost_state::known, "2500 ms is not a gap");
    f.s.interval_s = 2.501;
    check(f.step() == lost_state::collecting, "over it is, and the late sample is the first of the new run");
    f.s.interval_s = 1;
    f.fill(3);
    check(f.s.lost.state == lost_state::collecting, "4 since the gap");
    check(f.step() == lost_state::known, "5 since the gap");
  }
  {
    feed f;
    f.fill(5);
    f.s.vm.vmmem_pid = 8;  // A pid change alone: the known flag is the collector's and is checked below
    check(f.step() == lost_state::collecting, "a vmmem pid change resets");
    f.fill(5);
    f.s.vm.vmmem_cores_known = false;
    check(f.step() == lost_state::no_data, "an unmeasured VM figure is not 0");
    check(std::isnan(f.s.lost.vcpus), "and not a value");
    f.s.vm.vmmem_cores_known = true;
    check(f.step() == lost_state::collecting, "it reset");
    f.fill(5);
    f.s.cpu.core_pct.clear();
    check(f.step() == lost_state::no_data, "a failed Windows read");
  }
}

void derived_cases() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  {
    feed f;
    f.s.cpu.kernel_pct = 40;
    f.step();
    check(near(f.s.cpu_derived.vm_cpu_pct, 30, 1e-9), "6 cores of 20 is 30%");
    check(near(f.s.cpu_derived.own_kernel_pct, 10, 1e-9), "kernel 40 less VM 30");
    f.s.cpu.kernel_pct = 20;
    f.step();
    check(f.s.cpu_derived.own_kernel_pct == 0, "clamped at 0");
    f.s.cpu.kernel_pct = nan;
    f.step();
    check(std::isnan(f.s.cpu_derived.own_kernel_pct), "unknown kernel is NaN, which std::max would make 0");
    check(near(f.s.cpu_derived.vm_cpu_pct, 30, 1e-9), "and the VM figure stands");
    f.s.cpu.kernel_pct = 40;
    f.s.vm.vmmem_cores_known = false;
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_cpu_pct) && std::isnan(f.s.cpu_derived.own_kernel_pct), "unknown VM is NaN in both");
    f.s.vm.vmmem_cores_known = true;
    f.s.cpu.core_pct.clear();
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_cpu_pct), "no logical CPUs read, no percent");
  }
  {
    feed f;
    for (int i = 0; i < 4; ++i) {
      f.step();
      check(std::isnan(f.s.cpu_derived.ready_threads_mean) && std::isnan(f.s.cpu_derived.vm_ready_threads_mean), "NaN until 5 samples");
    }
    f.step();
    check(near(f.s.cpu_derived.ready_threads_mean, 40, 1e-9) && near(f.s.cpu_derived.vm_ready_threads_mean, 13, 1e-9), "the means");
    f.s.ready_threads = 90;
    f.step();
    check(near(f.s.cpu_derived.ready_threads_mean, 50, 1e-9), "the oldest leaves");
    f.s.vm_ready_threads.reset();
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_ready_threads_mean), "unknown VM count is NaN");
    check(std::isfinite(f.s.cpu_derived.ready_threads_mean), "and leaves the machine's count alone");
    f.s.vm_ready_threads = 13;
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_ready_threads_mean), "4 more before it is back");
    f.s.ready_threads.reset();
    f.step();
    check(std::isnan(f.s.cpu_derived.ready_threads_mean), "unknown count is NaN");
    f.s.ready_threads = 40;
    for (int i = 0; i < 5; ++i) f.step();
    check(std::isfinite(f.s.cpu_derived.ready_threads_mean), "back after 5");
    f.s.interval_s = 2.6;
    f.step();
    check(std::isnan(f.s.cpu_derived.ready_threads_mean) && std::isnan(f.s.cpu_derived.vm_ready_threads_mean), "a gap breaks both");
    f.s.interval_s = 1;
    for (int i = 0; i < 5; ++i) f.step();
    check(std::isfinite(f.s.cpu_derived.vm_ready_threads_mean), "both back");
    f.s.vm.vmmem_pid = 8;
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_ready_threads_mean) && std::isfinite(f.s.cpu_derived.ready_threads_mean), "a new VM breaks only its own");
  }
}

void record_cases() {
  feed f;
  f.step();
  std::string line = to_json_line(f.s);
  check(line.find("\"lost_cpu_inferred\":{\"state\":\"collecting\",\"vcpus\":null,\"source\":\"inferred\"}") != std::string::npos, "unknown is null in the record");
  f.fill(4);
  line = to_json_line(f.s);
  check(line.find("\"lost_cpu_inferred\":{\"state\":\"known\",\"vcpus\":14.000,\"source\":\"inferred\"}") != std::string::npos, "known in the record");
  const size_t derived = line.find("\"cpu_derived\":{");
  check(derived != std::string::npos && line.find("\"source\":\"derived\"", derived) < line.find('}', derived), "cpu_derived's own source says derived");

  const auto named = [](feed& g, const char* state) {
    return to_json_line(g.s).find(std::string("\"lost_cpu_inferred\":{\"state\":\"") + state + "\"") != std::string::npos;
  };
  feed under; under.fill(5, 98);
  check(under.s.lost.state == lost_state::guest_under_99 && named(under, "guest_under_99"), "guest_under_99 by name");
  feed quiet; quiet.s.cpu.busy_pct = 90; quiet.fill(5);
  check(quiet.s.lost.state == lost_state::windows_under_95 && named(quiet, "windows_under_95"), "windows_under_95 by name");
  feed none; none.s.wsl_age_s = 60; none.step();
  check(none.s.lost.state == lost_state::no_data && named(none, "no_data"), "no_data by name");
  feed few; few.vcpus = 4; few.step();
  check(few.s.lost.state == lost_state::vcpus_differ && named(few, "vcpus_differ"), "vcpus_differ by name");
}

// The window and a reset, with values that change, so a stale value in a mean shows.
void window_cases() {
  {
    feed f;
    f.fill(5);
    check(near(f.s.lost.vcpus, 14, 1e-9), "20 less 6");
    f.s.wsl_age_s = 60;
    f.step();
    f.s.wsl_age_s = 0.5;
    f.s.vm.vmmem_cores = 2;
    f.fill(4);
    check(f.s.lost.state == lost_state::collecting, "a reset empties every window, so 4 are not enough");
    f.step();
    check(f.s.lost.state == lost_state::known && near(f.s.lost.vcpus, 18, 1e-9), "nothing from before the reset is in the mean");
  }
  {
    feed f;
    for (const double busy : {99.0, 100.0, 99.0, 100.0, 99.0}) f.step(busy);
    check(f.s.lost.state == lost_state::known && near(f.s.lost.vcpus, 19.88 - 6, 1e-9), "the guest side is a mean, not the newest");
  }
  {
    feed f;
    f.vcpus = 4;
    for (int i = 0; i < 8; ++i) check(f.step() == lost_state::vcpus_differ, "4 vCPUs on 20 logical CPUs never count");
    check(std::isnan(f.s.lost.vcpus), "and have no figure");
    f.vcpus = 20;
    f.fill(4);
    check(f.s.lost.state == lost_state::collecting, "a change of vCPUs starts the count again");
    f.step();
    check(f.s.lost.state == lost_state::known && near(f.s.lost.vcpus, 14, 1e-9), "with nothing from the other count in the mean");
  }
  {
    feed f;
    f.fill(5);
    check(std::isfinite(f.s.cpu_derived.vm_ready_threads_mean), "VM ready mean");
    f.s.vm.vmmem_cores_known = false;
    f.step();
    check(std::isnan(f.s.cpu_derived.vm_ready_threads_mean), "a VM on its first sample starts the ready mean again");
  }
}

// ---- The replay -----------------------------------------------------------------------------------

[[noreturn]] void die(const std::string& why) {
  printf("FAIL lost_check: %s\n", why.c_str());
  printf("lost_check: stopped, 1 failed\n");
  exit(1);
}

double field(const std::string& line, const std::string& name, size_t from) {
  const std::string k = "\"" + name + "\":";
  const size_t at = line.find(k, from);
  if (at == std::string::npos) die("no " + name + " in a record");
  return strtod(line.c_str() + at + k.size(), nullptr);
}

size_t after(const std::string& line, const char* text) {
  const size_t at = line.rfind(text);
  if (at == std::string::npos) die(std::string("no ") + text + " in a record");
  return at;
}

struct replayed {
  double t = 0, windows_busy = 0, guest_busy = 0, interval_s = 0, vcpus = 0;
  lost_state state = lost_state::no_data;
  bool repeated = false;
};

// t is on the clock of the marks: the WSL clock, so Windows time less the recorded offset.
std::vector<replayed> replay(const std::string& path) {
  std::ifstream in(path);
  if (!in) die("cannot read " + path);
  std::vector<replayed> out;
  lost_tracker tracker;
  snapshot s;
  std::string line, last_data;
  uint32_t last_pid = 0;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const size_t wsl = after(line, "\"wsl\":{\"age_s\":");
    const size_t data = line.find("\"data\":", wsl);
    if (data == std::string::npos || line.size() < 2 || line.compare(line.size() - 2, 2, "}}") != 0) die("a record of another shape");
    s.interval_s = field(line, "interval_s", 0);
    s.cpu.busy_pct = field(line, "busy_pct", line.find("\"cpu\":{"));
    const size_t cores = line.find("\"core_pct\":[");
    s.cpu.core_pct.assign(1 + static_cast<size_t>(std::count(line.begin() + static_cast<std::ptrdiff_t>(cores), line.begin() + static_cast<std::ptrdiff_t>(line.find(']', cores)), ',')), 0.0);
    const size_t vm = line.find("\"vm\":{");
    s.vm.vmmem_pid = static_cast<uint32_t>(field(line, "vmmem_pid", vm));
    s.vm.vmmem_cores = field(line, "vmmem_cores", vm);
    s.vm.vmmem_cores_known = s.vm.vmmem_pid != 0 && s.vm.vmmem_pid == last_pid;
    last_pid = s.vm.vmmem_pid;
    s.wsl_age_s = field(line, "age_s", wsl);
    const std::string body = line.substr(data + 7, line.size() - 2 - (data + 7));
    s.wsl_json = body == "null" ? std::string() : "{\"schema\":1,\"memory\":{}," + body.substr(1);
    tracker.update(s);

    replayed r;
    r.t = field(line, "unix_ms", 0) / 1000.0 - field(line, "clock_offset_s", wsl);
    r.windows_busy = s.cpu.busy_pct;
    r.interval_s = s.interval_s;
    r.guest_busy = read_wsl(s.wsl_json).busy_pct;
    r.vcpus = s.lost.vcpus;
    r.state = s.lost.state;
    r.repeated = !s.wsl_json.empty() && s.wsl_json == last_data;
    last_data = s.wsl_json;
    out.push_back(r);
  }
  return out;
}

std::map<std::string, double> read_marks(const std::string& path) {
  std::ifstream in(path);
  if (!in) die("cannot read " + path);
  std::map<std::string, double> m;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("mark\t", 0) != 0) continue;
    const size_t tab = line.find('\t', 5);
    m[line.substr(5, tab - 5)] = atof(line.c_str() + tab + 1);
  }
  return m;
}

bool fired(const replayed& r) { return r.state == lost_state::known && r.vcpus >= 1; }  // Under 1 prints `<1`: design section 4, "Floor"

void replay_cases(const std::string& dir) {
  const std::vector<replayed> rows = replay(dir + "/run1-record-2026-11-01.txt");
  std::map<std::string, double> m = read_marks(dir + "/run1-marks-2026-11-01.txt");
  check(rows.size() == 296, "the record has its 296 samples");
  check(m.size() >= 30, "the marks are there");

  const auto count_in = [&](double from, double to, const auto& test) {
    int n = 0;
    for (const replayed& r : rows) if (r.t >= from && r.t < to && test(r)) ++n;
    return n;
  };
  // A case that must never fire. Also prints how often it was inside the gate, under the floor.
  for (const char* name : {"baseline", "wsl-1", "wsl-2", "wsl-5", "wsl-10", "wsl-20", "settle-1", "win-20", "settle-2", "both-before", "settle-3"}) {
    const double from = m.at(name), to = m.count(std::string(name) + "-end") ? m.at(std::string(name) + "-end") : m.at("both-win-5");  // both-before has no end mark
    const int samples = count_in(from, to, [](const replayed&) { return true; });
    const int under_floor = count_in(from, to, [](const replayed& r) { return r.state == lost_state::known; });
    const int fires = count_in(from, to, fired);
    printf("  %-12s %3d samples, %2d inside the gate, %d at 1 vCPU or more\n", name, samples, under_floor, fires);
    check(samples > 0 && fires == 0, std::string(name) + " fired");
  }
  check(count_in(m.at("baseline"), m.at("wsl-20"), [](const replayed& r) { return r.state == lost_state::known; }) == 0, "never even known below 20 threads");
  check(count_in(m.at("win-20"), m.at("win-20-end"), [](const replayed& r) { return r.state != lost_state::guest_under_99; }) == 0, "win-20 is gated by the guest throughout");

  // Where the guest's load stops, Windows goes quiet while the means still hold it.
  for (const char* stop : {"wsl-20-end", "win-20-end", "both-after-end"}) {
    const int fires = count_in(m.at(stop), m.at(stop) + 10, fired);
    printf("  10 s after %-15s %d fired\n", stop, fires);
    check(fires == 0, std::string("fired within 10 s of ") + stop);
  }
  // The two the design names, by their own Windows figure: the first is the old gate's 3.65.
  for (const double busy : {30.799, 20.35}) {
    int found = 0;
    for (const replayed& r : rows) {
      if (!near(r.windows_busy, busy, 0.006)) continue;
      ++found;
      printf("  Windows %.3f, guest %.2f: state %s\n", r.windows_busy, r.guest_busy, state_name(r.state).c_str());
      check(r.state != lost_state::known, "a false fire of the old gate came back");
    }
    check(found >= 1, "the record holds the sample the design names");
  }

  // A Windows load that stops while the guest stays full. Windows stays at 100, so the gate holds, and the
  // figure falls as the contended samples leave the mean: the lag, pinned here so a change shows.
  // Not "no firing": contracts/core.md, decision log.
  std::vector<double> tail;
  for (const replayed& r : rows) if (r.t >= m.at("both-win-20-end") && r.t < m.at("both-after-end")) tail.push_back(r.vcpus);
  printf("  after both-win-20 stops, guest still full:");
  for (const double v : tail) printf(" %.3f", v);
  printf("\n");
  check(tail.size() == 8, "both-after has 8 samples");
  for (size_t i = 1; i < tail.size(); ++i) check(tail[i] < tail[i - 1], "the tail falls");
  check(tail.size() == 8 && tail[6] >= 1 && tail[7] < 1, "the tail is under the floor by the eighth sample, 8 s after the stop");

  // The contended cases, leaving out the case's first 2 s and last 1 s as the experiment's means do, and any
  // sample whose 5-sample mean reaches outside that.
  const struct { const char* name; double want; } contended[] = {{"both-win-5", 5.521}, {"both-win-10", 10.372}, {"both-win-20", 14.244}};
  for (const auto& c : contended) {
    const double from = m.at(c.name) + 2, to = m.at(std::string(c.name) + "-end") - 1;
    double sum = 0, low = 1e9, high = 0;
    int n = 0, whole = 0;
    size_t run = 0;
    for (const replayed& r : rows) {
      const bool inside = r.t - r.interval_s >= from && r.t <= to;
      run = inside ? run + 1 : 0;
      whole += inside;
      if (run < 5 || r.state != lost_state::known) continue;
      sum += r.vcpus;
      low = std::min(low, r.vcpus);
      high = std::max(high, r.vcpus);
      ++n;
    }
    const double mean = n ? sum / n : 0;
    printf("  %-12s mean %.3f over %d of %d samples, range %.3f to %.3f, ledger %.3f\n", c.name, mean, n, whole, low, high, c.want);
    check(n >= 4, std::string(c.name) + " has too few samples");
    check(near(mean, c.want, 0.1), std::string(c.name) + " is off the ledger's mean");
    check(low >= 1, std::string(c.name) + " dropped under the floor inside the case");
  }

  // The rest of what the design says of this record.
  int repeats = 0, doubles = 0, gaps = 0, no_data = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    repeats += rows[i].repeated;
    doubles += i && rows[i].repeated && rows[i - 1].repeated;
    gaps += rows[i].interval_s * 1000.0 > kGapMs;
    no_data += rows[i].state == lost_state::no_data;
  }
  const int windows_gated = static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const replayed& r) { return r.state == lost_state::windows_under_95; }));
  printf("  %d repeated lines, %d twice in a row, %d gaps, %d samples no_data, %d stopped only by the Windows clause\n", repeats, doubles, gaps, no_data, windows_gated);
  const int held = count_in(m.at("both-win-20") + 6, m.at("both-win-20-end"), [](const replayed& r) { return r.state == lost_state::known; });
  const int asked = count_in(m.at("both-win-20") + 6, m.at("both-win-20-end"), [](const replayed&) { return true; });
  printf("  both-win-20 held the figure through its late sample: %d of %d\n", held, asked);
  check(asked > 40 && held == asked, "lost held through the late sample in both-win-20");
  check(doubles == 0, "no line repeated twice in a row");
  check(no_data == 1, "only the first record, with no pid before it, cannot count");
  check(gaps == 1, "one late sample over the graph's gap, in both-win-20, which lost keeps through");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) die("usage: lost_check <experiments/05-cpu-contention/results>");
  gate_cases();
  derived_cases();
  record_cases();
  window_cases();
  replay_cases(argv[1]);
  printf("lost_check: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
