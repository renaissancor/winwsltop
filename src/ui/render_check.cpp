// Checks the renderer on hand-built snapshots. render_check <busy|bare> W H prints a frame as text,
// the one overall screen with no history; add overall, gpu or cpu for that panel over a made-up minute.
// render_check <busy|bare> draws on the console until Ctrl+C; add live for the panels, keys 1 2 3 q.
// render_check quick runs everything but the size sweeps, about a second, for mutating the renderer.
// Build: scripts/build.sh src/ui/render_check.cpp   (it includes the sources it tests)
#include "ui/render.cpp"
#include "ui/history.h"
#include "core/wsl_view.cpp"
#ifdef _WIN32
#include "ui/console.cpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>

using namespace wwt;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

namespace {

int g_failed = 0, g_checks = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) { ++g_failed; printf("FAIL %s\n", what.c_str()); }
}

constexpr uint64_t kG = 1ull << 30;
constexpr int64_t kNow = 1793450632000;

std::string busy_json() {
  std::string p;
  static const char* const names[] = {"python3", "ollama", "node", "cargo", "clang++", "rustc", "ld.lld", "sshd", "systemd", "dbus-daemon"};
  for (int i = 0; i < 26; ++i) {
    p += "{\"pid\":" + std::to_string(2000 + i) + ",\"name\":\"" + names[i % 10] + "\",\"state\":\"" + (i == 7 ? "D" : "S") +
         "\",\"rss\":" + std::to_string((27 - i) * 90000000ll) + ",\"cores\":" + std::to_string((26 - i) * 0.04) + ",\"on_gpu\":false},";
  }
  p += R"({"pid":4121,"name":"python3","state":"R","rss":6442450944,"cores":3.21,"on_gpu":true},)"
       R"({"pid":4388,"name":"한글추론기","state":"S","rss":2147483648,"cores":0.1,"on_gpu":true},)"
       R"({"pid":4400,"name":"\u001b]0;pwned\u0007\u001b[31mevil","state":"S","rss":1000,"cores":0.0,"on_gpu":false})";
  std::string cores;
  for (int i = 0; i < 20; ++i) cores += std::to_string(i == 6 ? 97.0 : (i * 7) % 40 + 2.0) + (i < 19 ? "," : "");
  return R"({"schema":1,"distro":"Ubuntu-24.04","unix_s":1793450631.8,"mono_s":812.1,"kernel":"6.6.114.1","busy_pct":23.4,"core_pct":[)" + cores +
         R"(],"steal_pct":0.3,"memory":{"total":33520000000,"available":25700000000,"cached":7516192768,"swap_used":0},)"
         R"("pressure":{"cpu":1.2,"memory":0.0,"io":24.5},"process_count":64,"processes":[)" + p +
         R"(],"dxg_pids":[4121,4388],"dxg_denied":36,"cost_ms":3.46})";
}

snapshot busy() {
  snapshot s;
  s.unix_ms = kNow - 1200;
  s.interval_s = 1.0;
  for (int i = 0; i < 20; ++i) s.cpu.core_pct.push_back(i == 3 ? 96.0 : (i * 11) % 55 + 3.0);
  s.cpu.busy_pct = 38.2;
  s.memory = {68449929216, 39 * kG, 31 * kG, 80 * kG, 12 * kG};
  s.vm = {7120, 11736, 10898438144, 3.1};
  const auto proc = [&](uint32_t pid, const char* name, double cores, uint64_t ws) {
    process_row p;
    p.pid = pid; p.name = name; p.cores = cores; p.working_set = ws;
    s.processes.push_back(p);
  };
  proc(0, "Idle", 11.6, 8192);
  proc(4, "System", 0.42, 1 << 20);
  proc(7120, "vmmemWSL", 3.1, 10898438144);
  proc(11736, "vmwp.exe", 0.31, 90000000);
  proc(9001, "카카오톡.exe", 0.22, 410000000);
  proc(9002, "Game.Shipping-Win64-Test-with-a-very-long-executable-name.exe", 2.84, 7 * kG);
  proc(9003, "\xED\x95\x9C\xFF\x1B[2Jbad.exe", 0.02, 1000);
  for (uint32_t i = 0; i < 40; ++i) proc(10000 + i * 4, "svchost.exe", 0.2 - i * 0.004, (40 - i) * 1000000ull);
  s.disks = {{"0 C:", 3100000, 1200000, 12}, {"1 D: E:", 240000000, 90000000, 96}, {"2 데이터", 0, 0, 0}};
  s.nets = {{"이더넷 2", 920000, 3100000, 1000000000, true},
            {"Wi-Fi", 1000, 300, 866700000, true},
            {"vEthernet (WSL (Hyper-V firewall))", 42000, 1800, 10000000000, false},
            {"한글이아주아주아주아주아주아주아주아주긴인터페이스이름", 0, 0, 0, false}};
  s.gpu_engines = {{11736, "3D", 52}, {9002, "3D", 36}, {9002, "Copy", 4}, {5120, "3D", 1}};
  s.gpu_device = {true, "NVIDIA GeForce RTX 4070", true, true, true, true, true, 91, 187, 200, 71, 2730, 9 * kG, 12 * kG};
  s.gpu_memory = {{11736, 4509715660, 100000, 4509715660}, {9002, 1503238553, 200000, 1503238553}};
  s.gpu_split = {true, 52, 36, 4509715660, 1503238553};
  s.wsl_json = busy_json();
  s.wsl_age_s = 0.17;
  s.wsl_clock_known = false;
  return s;
}

snapshot bare() {
  snapshot s;
  s.unix_ms = kNow - 800;
  s.interval_s = 1.0;
  for (int i = 0; i < 8; ++i) s.cpu.core_pct.push_back(4.0 + i);
  s.cpu.busy_pct = 7.5;
  s.memory = {16 * kG, 11 * kG, 6 * kG, 20 * kG, 3 * kG};
  process_row p;
  p.pid = 812; p.name = "explorer.exe"; p.cores = 0.06; p.working_set = 180000000;
  s.processes.push_back(p);
  s.disks = {{"0 C:", 0, 4096, 1}};
  s.nets = {{"이더넷", 0, 0, 1000000000, true}};
  return s;
}

bool has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

void check_size(const snapshot& s, const char* name) {
  for (const int w : {1, 2, 3, 10, 40, 80, 100, 118, 131, 132, 200, 300}) {
    for (const int h : {1, 2, 5, 24, 40, 66}) {
      const auto frame = render(s, w, h, kNow);
      const std::string where = std::string(name) + " " + std::to_string(w) + "x" + std::to_string(h);
      check(static_cast<int>(frame.size()) <= h, where + " too many lines");
      for (const line& l : frame) {
        int cols = 0;
        for (const span& p : l) cols += display_width(p.text);
        if (cols > w) printf("  %d columns: %s\n", cols, as_text({l}).c_str());
        check(cols <= w, where + " line too wide");
      }
    }
  }
  check(render(s, 0, 40).empty() && render(s, 100, 0).empty() && render(s, -5, -5).empty(), "no size, no lines");
}

// Only colour, cursor and erase sequences, and no control character.
bool vt_clean(const std::string& vt) {
  bool clean = !has(vt, "\x1b[2J") && !has(vt, "\x1b]0");
  for (size_t i = 0; i < vt.size(); ++i) {
    const auto c = static_cast<unsigned char>(vt[i]);
    if (c == 0x1B) {
      clean = clean && i + 1 < vt.size() && vt[i + 1] == '[';
      size_t e = i + 2;
      while (e < vt.size() && (vt[e] == ';' || vt[e] == '?' || (vt[e] >= '0' && vt[e] <= '9'))) ++e;
      clean = clean && e < vt.size() && (vt[e] == 'm' || vt[e] == 'H' || vt[e] == 'K' || vt[e] == 'J');
      i = e;
    } else if (c < 0x20 || c == 0x7F) {
      clean = false;
    }
  }
  return clean;
}

void check_honesty() {
  for (const int w : {80, 100, 131, 200}) {
    const std::string n = " at " + std::to_string(w);
    const std::string t = as_text(render(busy(), w, 66, kNow));
    check(has(t, "VM level"), "split labelled VM level" + n);
    check(has(t, "WSL VM"), "WSL shown as the VM" + n);
    check(has(t, "dxg_denied 36"), "dxg_denied noted" + n);
    check(has(t, "no usage measured"), "holders carry no usage" + n);
    check(has(t, "한글추론기"), "a Korean holder is named" + n);  // In the WSL list at any width
    check(!has(t, "88%"), "52 + 36 is never shown" + n);
    check(!has(t, " Idle"), "the idle process is not a process" + n);
    check(t.find("WSL VM") != std::string::npos && has(t, "includes the WSL VM"), "Windows totals say they include the VM" + n);
    check(has(t, "1.2 s old"), "title gives the data age" + n);

    const std::string b = as_text(render(bare(), w, 60, kNow));
    check(has(b, "device  unavailable"), "no GPU says so" + n);
    check(has(b, "split   unavailable"), "no split says so" + n);
    check(has(b, "dxg     unavailable"), "no holders says so" + n);
    check(has(b, "WSL data unavailable"), "no WSL says so" + n);
    check(!has(b, "dxg_denied"), "dxg_denied hidden at zero" + n);
    check(b.find("  0%") == std::string::npos || b.find("  0%") > b.find("WINDOWS"), "no GPU zero before WINDOWS" + n);
    check(!has(b.substr(0, b.find("WINDOWS")), "0%"), "the GPU section shows no zero" + n);
  }

  // The two process lists stay apart: every Linux name sits after the Windows list.
  const auto wide = render(busy(), 131, 60, kNow);
  bool joined = false;
  for (const line& l : wide) {
    const std::string t = as_text({l});
    if (has(t, "ollama") || has(t, "svchost.exe")) {
      const size_t bar = t.find("│");
      check(bar != std::string::npos, "side-by-side rows have a divider");
      if (has(t, "ollama")) check(t.find("ollama") > bar, "Linux names right of the divider");
      if (has(t, "svchost.exe")) check(t.find("svchost.exe") < bar, "Windows names left of the divider");
      joined = true;
    }
  }
  check(joined, "the lists were drawn");
  const std::string narrow = as_text(render(busy(), 80, 66, kNow));
  check(narrow.rfind("svchost.exe") < narrow.find("ollama", narrow.find("PROCESSES")), "stacked lists do not interleave");

  snapshot only_vm = busy();  // The bridge can restart while the VM is still up
  only_vm.wsl_json.clear();
  only_vm.wsl_restarts = 2;
  const std::string t = as_text(render(only_vm, 131, 60, kNow));
  check(has(t, "restarted 2 times") && has(t, "vmmemWSL"), "a dead collector still shows the VM from Windows");
}

void check_edges() {
  snapshot s = busy();
  s.gpu_device.has_util = s.gpu_device.has_power = s.gpu_device.has_temp = s.gpu_device.has_clock = false;
  std::string t = as_text(render(s, 131, 40, kNow));
  check(has(t, "util n/a") && has(t, "power n/a") && has(t, "temp n/a"), "a missing reading is n/a, not 0");

  s = busy();
  s.gpu_split = {true, 112, 3, 1, 1};  // Measured peak: bars clamp, numbers do not
  s.cpu.busy_pct = std::numeric_limits<double>::quiet_NaN();
  s.cpu.core_pct[0] = std::numeric_limits<double>::infinity();
  s.memory.available = s.memory.total * 2;
  s.nets[0].in_bytes_s = -1;
  t = as_text(render(s, 131, 40, kNow));
  check(has(t, "112%"), "over 100% is printed as measured");
  check_size(s, "hostile numbers");

  s = busy();
  s.wsl_age_s = -1;
  check(has(as_text(render(s, 131, 40, 0)), "data age unknown"), "no clock, no invented age");
  check(has(as_text(render(s, 131, 40, kNow)), "collector age unknown"), "unknown WSL age says so");

  s = busy();
  s.wsl_json = R"({"schema":2,"busy_pct":1,"memory":{}})";
  check(has(as_text(render(s, 131, 40, kNow)), "newer collector schema"), "a newer schema is not guessed at");
  s.wsl_json = "{\"schema\":1,\"busy";
  check(has(as_text(render(s, 131, 40, kNow)), "could not be read"), "a cut line is not guessed at");

  check(vt_clean(vt_frame(render(busy(), 131, 40, kNow), 40)), "only our own sequences reach the terminal");
}

void check_width() {
  check(display_width("abc") == 3, "ascii");
  check(display_width("한글") == 4, "hangul");
  check(display_width("e\xCC\x81") == 1, "combining mark");
  check(display_width("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB") == 2, "conjoining jamo");
  check(display_width("━─·°…⣿") == 6, "drawing characters");
  check(display_width("\xFF\xED\x95") == 3, "bad bytes are one column each");
  check(display_width("😀") == 2, "emoji");
  check(fit("한글한글", 5) == "한글…", "a cut wide character leaves no half");
  check(pad("한글", 6) == "한글  ", "pad counts columns");
  check(pad_left("한", 3) == " 한", "pad_left counts columns");
  check(clip({{"한글한글", tone::text, false}}, 4)[0].text == "한…", "clip marks the cut");
}

void check_reader() {
  wsl_view v = read_wsl(busy_json());
  check(v.ok && v.schema == 1 && v.distro == "Ubuntu-24.04", "reads the busy line");
  check(v.core_pct.size() == 20 && v.mem_total == 33520000000ull, "reads numbers");
  check(v.dxg_pids.size() == 2 && v.dxg_denied == 36 && v.process_count == 64, "reads the dxg fields");
  check(v.processes.size() == 29 && v.processes[27].name == "한글추론기", "reads \\u escapes");
  check(v.psi_io && *v.psi_io == 24.5, "reads pressure");

  const char* base = R"({"schema":1,"busy_pct":1,"memory":{"total":1},"pressure":{"cpu":null,"memory":2,"io":null},"x":[{"y":[1,"\"]",null,true]}],"processes":[{"pid":1,"name":"😀\ud800x"}]})";
  v = read_wsl(base);
  check(v.ok, "skips unknown fields");
  check(!v.psi_cpu && v.psi_memory && !v.psi_io, "null pressure stays unknown");
  check(v.processes.size() == 1 && v.processes[0].name == "😀\xEF\xBF\xBDx", "surrogate pair and lone surrogate");

  // The CPU keys are additive: a line without them, or with a null, reads, and the figure stays unknown.
  v = read_wsl(busy_json());
  check(v.ok && !v.cpu_user && !v.cpu_sys && !v.cpu_iowait && !v.load[0] && !v.processes[0].threads, "a line without the CPU keys reads");
  v = read_wsl(R"({"schema":1,"busy_pct":1,"memory":{},"cpu_split":{"user":99.5,"sys":0.5,"iowait":0},"load":[19.8,12.25,6,7],"processes":[{"pid":1,"threads":21}]})");
  check(v.ok && v.cpu_user == 99.5 && v.cpu_sys == 0.5 && v.cpu_iowait == 0.0, "reads cpu_split, and a measured 0 is kept");
  check(v.load[0] == 19.8 && v.load[1] == 12.25 && v.load[2] == 6.0, "reads load, and a longer list does not write past it");
  check(v.processes.size() == 1 && v.processes[0].threads == 21u, "reads threads");
  v = read_wsl(R"({"schema":1,"busy_pct":1,"memory":{},"cpu_split":{"user":null,"sys":2,"iowait":null},"load":[null,2,null],"processes":[{"pid":1,"threads":null}]})");
  check(v.ok && !v.cpu_user && v.cpu_sys == 2.0 && !v.cpu_iowait, "a null in cpu_split stays unknown");
  check(!v.load[0] && v.load[1] == 2.0 && !v.load[2], "a null in load stays unknown");
  check(v.processes.size() == 1 && !v.processes[0].threads, "a null threads stays unknown");
  v = read_wsl(R"({"schema":1,"busy_pct":1,"memory":{},"cpu_split":null,"load":null})");
  check(v.ok && !v.cpu_user && !v.load[0], "a null cpu_split or load does not fail the line");

  for (const char* bad : {"", "{", "[]", "null", "{\"schema\":1}", "{\"schema\":1,\"busy_pct\":1,\"memory\":{}} x", "{\"schema\":1,\"busy_pct\":nan,\"memory\":{}}",
                          "{\"schema\":1,\"busy_pct\":-inf,\"memory\":{}}", "{\"schema\":1,\"busy_pct\":1,\"memory\":{},\"distro\":\"a\\q\"}",
                          "{\"schema\":1,\"busy_pct\":1,\"memory\":{},\"distro\":\"a\\u12\"}", "{\"schema\":\"1\",\"busy_pct\":1,\"memory\":{}}"}) {
    check(!read_wsl(bad).ok, std::string("rejects ") + bad);
  }
  check(!read_wsl(std::string(100000, '[')).ok, "deep nesting is refused, not recursed into");
  check(read_wsl("{\"schema\":1,\"busy_pct\":1,\"memory\":{\"total\":-5},\"process_count\":1e300}").mem_total == 0, "out of range numbers do not wrap");
}

// ---- History and panels --------------------------------------------------------------------------

// A sample at a time, on a snapshot's other fields. wsl_busy below 0 keeps the snapshot's own line.
snapshot sample_at(snapshot s, int64_t t, double device, double vm, double win, double wsl_busy = -1) {
  s.unix_ms = t;
  s.gpu_device.util_pct = device;
  s.gpu_split.vm_pct = vm;
  s.gpu_split.windows_pct = win;
  s.cpu.busy_pct = device * 0.6;
  const size_t at = s.wsl_json.find("\"busy_pct\":");
  if (wsl_busy >= 0 && at != std::string::npos) {
    s.wsl_json.replace(at, s.wsl_json.find(',', at) - at, "\"busy_pct\":" + std::to_string(wsl_busy));
  }
  return s;
}

// A made-up minute for reading frames: load in waves, the measured 112% peak, a 5 s stall.
history demo_minute(const snapshot& base) {
  history h;
  for (int age = 60; age >= 0; --age) {
    if (age >= 20 && age < 25) continue;
    const double wave = 50 + 45 * std::sin(age / 7.0);
    snapshot s = sample_at(base, kNow - age * 1000, wave, age == 42 ? 112 : wave * 0.6, (100 - wave) * 0.4, 30 + wave / 2);
    if (base.gpu_device.present) {
      s.gpu_device.mem_used = static_cast<uint64_t>((6 + wave / 25) * static_cast<double>(kG));
      s.gpu_device.power_w = 40 + wave * 1.5;
    }
    h.push(s);
  }
  return h;
}

int count_braille(const std::string& t) {  // U+2801 to U+28FF: a blank cell is a space
  int n = 0;
  for (size_t i = 0; i + 2 < t.size(); ++i) {
    const auto a = static_cast<unsigned char>(t[i]), b = static_cast<unsigned char>(t[i + 1]), c = static_cast<unsigned char>(t[i + 2]);
    if (a == 0xE2 && b >= 0xA0 && b <= 0xA3 && !(b == 0xA0 && c == 0x80)) ++n;
  }
  return n;
}

// Only the graph rows of a frame: the core rows are braille too.
std::string graph_rows(const std::string& t) {
  std::string out;
  for (size_t from = 0; from < t.size();) {
    const size_t nl = std::min(t.find('\n', from), t.size());
    const std::string l = t.substr(from, nl - from);
    if (l.rfind("100│", 0) == 0 || l.rfind("  0│", 0) == 0 || l.rfind("   │", 0) == 0) out += l + "\n";
    from = nl + 1;
  }
  return out;
}

// What is drawn in one panel of a row of panels: text after the nth gutter, up to the next.
std::string panel_cells(const std::string& rows, int nth) {
  static constexpr std::string_view kBar = "│";
  std::string out;
  for (size_t from = 0; from < rows.size();) {
    const size_t nl = std::min(rows.find('\n', from), rows.size());
    size_t at = from;
    bool found = true;
    for (int k = 0; k <= nth && found; ++k) {
      at = rows.find(kBar, at);
      found = at < nl;
      if (found) at += kBar.size();
    }
    if (found) out += rows.substr(at, std::min(rows.find(kBar, at), nl) - at) + "\n";
    from = nl + 1;
  }
  return out;
}

std::string line_with(const std::vector<line>& frame, const char* what, int after = 0) {
  for (size_t i = 0; i < frame.size(); ++i) {
    const std::string t = as_text({frame[i]});
    if (has(t, what)) return i + static_cast<size_t>(after) < frame.size() ? as_text({frame[i + static_cast<size_t>(after)]}) : std::string();
  }
  return {};
}

// n /dev/dxg holders, named from the busy snapshot's processes.
snapshot with_holders(snapshot s, int n) {
  const std::string pids = "\"dxg_pids\":[4121,4388]";
  std::string list = "\"dxg_pids\":[";
  for (int i = 0; i < n; ++i) list += std::to_string(2000 + i) + (i + 1 < n ? "," : "]");
  s.wsl_json.replace(s.wsl_json.find(pids), pids.size(), list);
  return s;
}

snapshot no_wsl() {
  snapshot s = busy();
  s.wsl_json.clear();
  return s;
}

bool marks_hidden(const std::vector<line>& frame) {
  const std::string last = as_text({frame.back()});
  return has(last, "some rows hidden") || last.rfind("…", 0) == 0;
}

int known_columns(const std::vector<double>& cols) {
  int n = 0;
  for (const double c : cols) n += std::isnan(c) ? 0 : 1;
  return n;
}

constexpr int kDots = 240;  // 4 dots a second: every second has its own columns

std::vector<double> util_columns(const history& h, int64_t end = kNow) { return plot_columns(h, &history_sample::gpu_util, end, kDots); }

void check_time_axis() {
  const snapshot base = busy();
  history h;
  check(known_columns(util_columns(h)) == 0, "no history, nothing drawn");
  h.push(sample_at(base, kNow, 40, 1, 1));
  std::vector<double> c = util_columns(h);
  check(known_columns(c) == 1 && !std::isnan(c[kDots - 1]), "one sample is one column, at now");

  h = history();
  h.push(sample_at(base, kNow - 30000, 40, 1, 1));
  h.push(sample_at(base, kNow - 29000, 40, 1, 1));
  c = util_columns(h);
  check(!std::isnan(c[kDots / 2]) && !std::isnan(c[kDots / 2 + 3]) && std::isnan(c[kDots / 2 - 1]) && std::isnan(c[kDots / 2 + 5]) && std::isnan(c[kDots - 1]),
        "two samples 30 s ago sit in the middle: time places them, not their count");

  h = history();
  h.push(sample_at(base, kNow - 60000, 40, 1, 1));
  c = util_columns(h);
  check(!std::isnan(c[0]) && known_columns(c) == 1, "60 s ago is the left edge exactly");
  check(known_columns(util_columns(h, kNow + 1000)) == 0, "61 s ago is off the graph");

  h = history();
  for (int age = 10; age >= 0; --age) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  c = util_columns(h);
  check(known_columns(c) >= 40 && known_columns(c) <= 44, "a steady second fills its columns");
  check(std::isnan(c[kDots / 2]), "a short history leaves the rest blank, not at zero");

  h = history();
  for (const int age : {6, 5, 4, 2, 1, 0}) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  c = util_columns(h);
  const int skipped = kDots - 1 - 3 * 4;  // Where 3 s ago falls
  check(std::isnan(c[skipped]) && !std::isnan(c[skipped + 4]) && !std::isnan(c[skipped - 4]), "a skipped second is a gap");

  h = history();
  for (const int age : {30, 29, 28, 3, 2, 1, 0}) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  c = util_columns(h);
  int blank = 0;
  for (int k = kDots - 1 - 27 * 4 + 1; k < kDots - 1 - 3 * 4; ++k) blank += std::isnan(c[static_cast<size_t>(k)]) ? 1 : 0;
  check(blank == 24 * 4 - 1, "a stall is a gap as long as the stall");

  h = history();
  for (int age = 5; age >= 0; --age) {
    snapshot s = sample_at(base, kNow - age * 1000, 40, 1, 1);
    if (age == 3) { s.gpu_device.has_util = false; s.gpu_split.valid = false; s.gpu_device.has_power = false; }
    h.push(s);
  }
  c = util_columns(h);
  check(std::isnan(c[static_cast<size_t>(kDots - 1 - 3 * 4)]) && std::isnan(c[static_cast<size_t>(kDots - 1 - 2 * 4 - 1)]) && !std::isnan(c[kDots - 1]), "a missing reading is a gap, not 0");
  check(std::isnan(plot_columns(h, &history_sample::vm_pct, kNow, kDots)[kDots - 1 - 3 * 4]), "an invalid split is a gap");
  check(!std::isnan(plot_columns(h, &history_sample::gpu_mem_pct, kNow, kDots)[kDots - 1 - 3 * 4]), "one figure's gap is not another's");

  snapshot stale = base;
  stale.wsl_age_s = 8;
  h = history();
  h.push(sample_at(stale, kNow, 40, 1, 1));
  check(std::isnan(h.samples().back().wsl_cpu), "a stale collector line is a gap, not a flat line");
  const std::string t = as_text(render(stale, 131, 40, kNow, &h, view::cpu));
  check(has(t, "23%  WSL CPU") && has(t, "collector line 8.0 s old"), "a stale figure is printed with its age");

  // The clock steps back: spacing survives, nothing lands ahead of now, a graph is still sane.
  h = history();
  for (int age = 10; age >= 0; --age) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  h.push(sample_at(base, kNow - 3600000, 40, 1, 1));
  h.push(sample_at(base, kNow - 3599000, 40, 1, 1));
  bool ordered = true;
  const auto& all = h.samples();
  for (size_t i = 1; i < all.size(); ++i) ordered = ordered && all[i].unix_ms - all[i - 1].unix_ms == 1000;
  check(ordered && all.size() == 13 && all.back().unix_ms == kNow - 3599000, "a backward step keeps the spacing");
  check(known_columns(util_columns(h, kNow - 3599000)) >= 48, "the graph survives a backward step");
  check(graph_end(h, kNow - 7200000) == kNow - 3599000, "a clock behind the newest sample is not the graph's end");
  check(count_braille(graph_rows(as_text(render(busy(), 131, 40, kNow - 7200000, &h, view::gpu)))) > 0, "drawing behind the newest sample still draws");

  h = history();
  h.push(sample_at(base, kNow, 40, 1, 1));
  snapshot back = sample_at(base, kNow - 1000000, 40, 1, 1);
  back.interval_s = 7;
  h.push(back);
  check(h.samples().size() == 2 && h.samples()[1].unix_ms - h.samples()[0].unix_ms == 7000, "a backward step takes its spacing from interval_s");
  back.interval_s = 7200;
  h.push(sample_at(back, kNow - 2000000, 40, 1, 1));
  check(h.samples().size() == 1, "a backward step after a long stall drops what is off the span");
  check(known_columns(util_columns(h, kNow - 2000000)) == 1, "a long stall does not join as continuous");
  back.interval_s = 899;
  h.push(sample_at(back, kNow - 3000000, 40, 1, 1));
  check(h.samples().size() == 2 && h.samples()[1].unix_ms - h.samples()[0].unix_ms == 899000, "a stall under what is kept shifts");
  back.interval_s = 900;
  h.push(sample_at(back, kNow - 4000000, 40, 1, 1));
  check(h.samples().size() == 1, "a stall of exactly what is kept is off every graph too");

  // Gaps, at the threshold.
  h = history();
  h.push(sample_at(base, kNow - 1500, 40, 1, 1));
  h.push(sample_at(base, kNow, 40, 1, 1));
  check(known_columns(util_columns(h)) == 7, "samples 1.5 s apart join");
  h = history();
  h.push(sample_at(base, kNow - 1600, 40, 1, 1));
  h.push(sample_at(base, kNow, 40, 1, 1));
  check(known_columns(util_columns(h)) == 2, "samples 1.6 s apart do not");
  h = history();
  h.push(sample_at(base, kNow - 1501, 40, 1, 1));
  h.push(sample_at(base, kNow, 40, 1, 1));
  check(known_columns(util_columns(h)) == 2, "samples 1.501 s apart do not");

  // Highest in a column, in either order.
  for (const double first : {10.0, 90.0}) {
    h = history();
    h.push(sample_at(base, kNow - 500, first, 1, 1));
    h.push(sample_at(base, kNow, 100 - first, 1, 1));
    check(plot_columns(h, &history_sample::gpu_util, kNow, 4)[3] == 90, "a column shows its highest sample");
  }

  // A repeated collector line measured nothing new.
  h = history();
  h.push(sample_at(base, kNow - 2000, 40, 1, 1, 11));
  h.push(sample_at(base, kNow - 1000, 40, 1, 1, 11));
  check(std::isnan(h.samples().back().wsl_cpu), "a repeated line is a gap");
  check(!std::isnan(h.samples().back().gpu_util), "and only for the WSL figure");
  h.push(sample_at(base, kNow, 40, 1, 1, 12));
  check(!std::isnan(h.samples().back().wsl_cpu), "a new line is a sample again");
  h.push(sample_at(base, kNow + 1000, 40, 1, 1, 12));
  check(std::isnan(h.samples().back().wsl_cpu), "a repeat after a change is a gap too");
  h.push(sample_at(base, kNow + 2000, 40, 1, 1, 11));
  check(!std::isnan(h.samples().back().wsl_cpu), "an older line coming back is compared with the last one only");

  // The 5 s stale boundary, on lines that are all new.
  h = history();
  snapshot aged = sample_at(base, kNow, 40, 1, 1, 21);
  aged.wsl_age_s = 4.99;
  h.push(aged);
  check(!std::isnan(h.samples().back().wsl_cpu), "a line just under 5 s old is a sample");
  aged = sample_at(base, kNow + 1000, 40, 1, 1, 22);
  aged.wsl_age_s = 5;
  h.push(aged);
  check(std::isnan(h.samples().back().wsl_cpu), "a line 5 s old is a gap");

  // has_* and the unreadable line, in the graph cells as well as the text.
  h = history();
  for (int age = 3; age >= 0; --age) {
    snapshot s = sample_at(base, kNow - age * 1000, 40, 1, 1);
    s.gpu_device.has_mem = s.gpu_device.has_power = false;
    s.wsl_json = age % 2 ? "{\"schema\":1,\"busy" + std::to_string(age) : R"({"schema":2,"busy_pct":1,"memory":{},"n":)" + std::to_string(age) + "}";
    h.push(s);
  }
  check(std::isnan(h.samples().back().gpu_mem_pct), "has_mem false is a gap, whatever mem_total says");
  check(std::isnan(h.samples().back().gpu_power_pct), "has_power false is a gap, whatever power_w says");
  check(known_columns(plot_columns(h, &history_sample::wsl_cpu, kNow, kDots)) == 0, "an unreadable line is not a sample");
  snapshot unreadable = base;
  unreadable.wsl_json = "{\"schema\":1,\"busy";
  const std::string cells = graph_rows(as_text(render(unreadable, 131, 30, kNow, &h, view::cpu)));
  check(count_braille(panel_cells(cells, 0)) > 0, "the Windows CPU graph is drawn");
  check(count_braille(panel_cells(cells, 1)) == 0, "an unreadable line draws no cell in the WSL graph");

  snapshot same = base;
  h = history();
  for (int i = 0; i < 5; ++i) h.push(same);
  check(h.samples().size() == 5 && h.samples().front().unix_ms < h.samples().back().unix_ms, "an unmoved clock still advances the samples");

  same.unix_ms = 0;
  h = history();
  h.push(same);
  same.unix_ms = std::numeric_limits<int64_t>::max();
  h.push(same);
  check(h.samples().empty(), "a time that cannot be placed is not stored");

  h = history();
  for (int i = 0; i < 2000; ++i) h.push(sample_at(base, kNow + i * 1000, 40, 1, 1));
  check(h.samples().size() == 901 && h.samples().back().unix_ms - h.samples().front().unix_ms == 900000, "the ring keeps 15 minutes, both ends in");
  h = history();
  for (int i = 0; i < 5000; ++i) h.push(sample_at(base, kNow + i, 40, 1, 1));
  check(h.samples().size() == 901, "the ring is bounded however fast it is pushed");
}

// What the large layout of panel 3 reads, sample by sample. Everything here is by value: no frame draws it yet.
void check_history_ring() {
  snapshot base = busy();
  const auto at = [&](snapshot s, int64_t t, double guest_busy) {
    s = sample_at(s, t, 40, 1, 1, guest_busy);
    return s;
  };

  // Retention and the drawing span are two things.
  history h;
  for (int age = 400; age >= 0; --age) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  check(h.samples().size() == 401, "400 s are all kept");
  check(known_columns(util_columns(h)) <= kDots && !std::isnan(util_columns(h)[0]), "60 s still fill a 60 s graph");
  check(plot_columns(h, &history_sample::gpu_util, kNow, kDots, 300000)[0] == 40, "the span is an argument");
  check(std::isnan(plot_columns(h, &history_sample::gpu_util, kNow, kDots, 900000)[kDots / 2 - 2]), "and 400 s of 900 fill under half of it");
  h = history();
  for (int age = 2000; age >= 0; age -= 100) h.push(sample_at(base, kNow - age * 1000, 40, 1, 1));
  check(h.samples().size() == 11 && h.samples().front().unix_ms == kNow - 1000000, "kept for 900 s, and the one sample before them");

  // The slots never move once the first push has sized the ring.
  h = history();
  h.push(sample_at(base, kNow, 40, 1, 1));
  const history_sample* const ring = &h.samples().front();
  for (int i = 1; i < 3000; ++i) h.push(sample_at(base, kNow + i * 1000, 40, 1, 1));
  bool same_block = h.samples().size() == 901;
  for (const history_sample& p : h.samples()) same_block = same_block && &p >= ring && &p < ring + 901;
  check(same_block, "a full ring writes over its own slots: nothing is allocated per sample");

  // Per-thread values, for each side, and NaN past what the sample read.
  h = history();
  h.push(at(base, kNow, 11));
  const history_sample one = h.samples().back();
  check(one.windows_threads == 20 && one.wsl_threads == 20, "both sides' thread counts");
  check(one.windows_core[3] == 96.0f && one.wsl_core[6] == 97.0f, "both sides' per-thread values");
  check(std::isnan(one.windows_core[20]) && std::isnan(one.wsl_core[63]), "past the count is NaN, not 0");
  check(one.wsl_busy == 11.0f && !std::isnan(one.wsl_ram) && !std::isnan(one.windows_ram), "the guest's busy and both sides' RAM");

  // A slot written over keeps nothing of what was there.
  snapshot wide = base;
  wide.cpu.core_pct.assign(64, 50.0);
  snapshot narrow = base;
  narrow.cpu.core_pct.assign(2, 50.0);
  h = history();
  for (int i = 0; i < 901; ++i) h.push(sample_at(wide, kNow + i * 1000, 40, 1, 1));
  h.push(sample_at(narrow, kNow + 901000, 40, 1, 1));
  check(h.samples().back().windows_threads == 2 && std::isnan(h.samples().back().windows_core[2]) && std::isnan(h.samples().back().windows_core[63]), "a reused slot keeps no old thread");
  snapshot many = base;
  many.cpu.core_pct.assign(128, 50.0);
  h = history();
  h.push(many);
  check(h.samples().back().windows_threads == 64 && h.samples().back().windows_core[63] == 50.0f, "past 64 threads a sample keeps 64");

  // Lanes: unknown is NaN, and vmmem_cores_known decides, not the number.
  snapshot lanes = base;
  lanes.vm.vmmem_cores_known = true;
  lanes.cpu_derived.own_kernel_pct = 6;
  lanes.cpu_derived.vm_cpu_pct = 29;
  lanes.lost = {lost_state::known, 13.96};
  lanes.wsl_json.insert(lanes.wsl_json.size() - 1, R"(,"cpu_split":{"user":99,"sys":1,"iowait":3})");
  h = history();
  h.push(lanes);
  check(h.samples().back().own_kernel == 6.0f && h.samples().back().vm_cpu == 29.0f && h.samples().back().wsl_iowait == 3.0f, "the lanes' figures");
  check(std::fabs(h.samples().back().lost_vcpus - 13.96f) < 0.001f, "the lost figure, in vCPUs");
  lanes.vm.vmmem_cores_known = false;
  lanes.lost = {lost_state::guest_under_99, 13.96};
  lanes.wsl_json = base.wsl_json;
  h = history();
  h.push(lanes);
  check(std::isnan(h.samples().back().own_kernel) && std::isnan(h.samples().back().vm_cpu), "vmmem_cores_known false is a gap, whatever the figures say");
  check(std::isnan(h.samples().back().lost_vcpus), "only the known state has a lost figure");
  check(std::isnan(h.samples().back().wsl_iowait), "no cpu_split, no iowait");

  snapshot failed = base;
  failed.cpu = {};
  failed.memory = {};
  h = history();
  h.push(failed);
  check(h.samples().back().windows_threads == 0 && std::isnan(h.samples().back().windows_core[0]) && std::isnan(h.samples().back().windows_ram), "a failed read keeps no 0");

  // A repeated line carries for one sample in these fields, and is still a gap in the old one.
  h = history();
  h.push(at(base, kNow - 3000, 11));
  h.push(at(base, kNow - 2000, 11));
  check(h.samples().back().wsl_busy == 11.0f && h.samples().back().wsl_core[6] == 97.0f && !std::isnan(h.samples().back().wsl_ram), "one repeat carries the last value");
  check(std::isnan(h.samples().back().wsl_cpu), "and is still a gap for the small layout and panel 2");
  h.push(at(base, kNow - 1000, 11));
  check(std::isnan(h.samples().back().wsl_busy) && std::isnan(h.samples().back().wsl_core[6]) && h.samples().back().wsl_threads == 0 && std::isnan(h.samples().back().wsl_ram), "two repeats in a row are a gap");
  h.push(at(base, kNow, 12));
  check(h.samples().back().wsl_busy == 12.0f, "a new line starts again");
  h.push(at(base, kNow + 1000, 12));
  check(h.samples().back().wsl_busy == 12.0f, "and one repeat after it carries again");

  snapshot stale = at(base, kNow, 13);
  stale.wsl_age_s = 5;
  h = history();
  h.push(stale);
  check(std::isnan(h.samples().back().wsl_busy) && h.samples().back().wsl_threads == 0, "a stale line is a gap here too");
  stale.wsl_json = "{\"schema\":1,\"busy";
  stale.wsl_age_s = 0.1;
  h = history();
  h.push(stale);
  check(std::isnan(h.samples().back().wsl_busy) && h.samples().back().wsl_threads == 0, "an unreadable line is a gap");

  snapshot huge = base;
  huge.cpu.core_pct[0] = 1e308;
  huge.cpu.core_pct[1] = kInf;
  h = history();
  h.push(huge);
  check(std::isnan(h.samples().back().windows_core[0]) && std::isnan(h.samples().back().windows_core[1]), "what a float cannot hold is not a height");
}

void check_scale() {
  const auto cols = [](double v) { return std::vector<double>(40, v); };
  std::vector<std::string> rows = plot_rows(cols(100), 5);
  check(count_braille(rows[0]) == 20 && has(rows[0], "⣿"), "100 reaches the top row");
  rows = plot_rows(cols(50), 4);
  check(count_braille(rows[0]) == 0 && count_braille(rows[1]) == 0 && has(rows[2], "⣿") && has(rows[3], "⣿"), "50 fills half: the scale is fixed");
  rows = plot_rows(cols(1), 4);
  check(count_braille(rows[0]) == 0 && count_braille(rows[2]) == 0 && count_braille(rows[3]) == 20, "a small value stays small: no auto-scale");
  rows = plot_rows(cols(0), 4);
  check(has(rows[3], "⣀") && count_braille(rows[2]) == 0, "a measured 0 is a line, not a gap");
  rows = plot_rows(cols(kNaN), 4);
  check(count_braille(rows[0] + rows[1] + rows[2] + rows[3]) == 0, "a gap is empty");

  history h;
  snapshot s = busy();
  s.gpu_split.vm_pct = 112;
  s.unix_ms = kNow;
  h.push(s);
  const std::vector<double> c = plot_columns(h, &history_sample::vm_pct, kNow, kDots);
  check(c[kDots - 1] == 100, "over 100 clamps in the graph");
  check(h.samples().back().vm_pct == 112, "the sample keeps what was measured");
  const std::string t = as_text(render(s, 131, 50, kNow, &h, view::gpu));
  check(has(t, "112%  WSL VM"), "the printed number stays as measured");

  s = busy();
  s.gpu_split = {true, 1e300, -5, 1, 1};
  s.unix_ms = kNow;
  history huge;
  huge.push(s);
  check(plot_columns(huge, &history_sample::vm_pct, kNow, kDots)[kDots - 1] == 100 && plot_columns(huge, &history_sample::windows_pct, kNow, kDots)[kDots - 1] == 0, "out of range clamps both ways");
  check(has(as_text(render(s, 131, 50, kNow, &huge, view::gpu)), "1e+300%"), "a huge number is printed, short enough to fit");

  for (const view v : {view::gpu, view::cpu}) {
    for (const auto& [w, h2] : {std::pair{131, 50}, std::pair{80, 24}}) {
      const history demo = demo_minute(busy());
      const std::string f = as_text(render(busy(), w, h2, kNow, &demo, v));
      const std::string n = " " + std::to_string(w) + "x" + std::to_string(h2);
      check(has(f, "100│") && has(f, "  0│") && has(f, "-60s") && has(f, " now"), "axes are labelled" + n);
      check(count_braille(graph_rows(f)) > 0, "a minute draws" + n);
    }
  }
}

void check_panels() {
  const snapshot full = busy();
  const history empty, demo = demo_minute(full), demo_bare = demo_minute(bare());

  // Without a history there are no panels, whatever view is asked for. This compares the code with
  // itself: it cannot show that the frames match any older version.
  for (const auto& s : {busy(), bare()}) {
    const std::string plain = as_text(render(s, 131, 50, kNow));
    check(as_text(render(s, 131, 50, kNow, nullptr, view::gpu)) == plain && as_text(render(s, 131, 50, kNow, nullptr, view::cpu)) == plain, "no history, no panels");
    check(!has(plain, "[q] quit"), "no history, no footer");
    // The overall panel is the screen without a history, one row shorter, then the footer.
    const auto with = render(s, 131, 50, kNow, &demo, view::overall);
    const auto without = render(s, 131, 49, kNow);
    check(with.size() == without.size() + 1 && as_text({with.begin(), with.end() - 1}) == as_text(without), "the overall panel is the screen without a history");
    check(count_braille(graph_rows(as_text(with))) == 0 && !has(as_text(with), "-60s"), "the overall panel has no graphs");
  }

  const auto footer_of = [&](view v, int w = 131, int h = 50) {
    const auto f = render(full, w, h, kNow, &demo, v);
    return f.back();
  };
  const struct { view v; const char* name; } keys[] = {{view::overall, "[1] overall"}, {view::gpu, "[2] gpu"}, {view::cpu, "[3] cpu"}};
  for (const auto& k : keys) {
    const line f = footer_of(k.v);
    const std::string text = as_text({f});
    check(has(text, "[1] overall  [2] gpu  [3] cpu  [q] quit"), "the footer names the keys");
    for (const auto& o : keys) {
      bool on = false;
      for (const span& p : f) on = on || (has(p.text, o.name) && p.bold && p.color == tone::title);
      check(on == (o.v == k.v), std::string("only the active panel is highlighted: ") + o.name);
    }
    check(render(full, 131, 50, kNow, &demo, k.v).size() <= 50, "the footer is counted in height");
    check(has(as_text({render(full, 131, 1, kNow, &demo, k.v).back()}), "[1]"), "one row is the footer");
  }

  // GPU panel labels, at a width where each title is on one line, then wrapped.
  const std::string g = as_text(render(full, 200, 60, kNow, &demo, view::gpu));
  for (const char* label : {"device · NVML", "WSL VM · engine counters, VM level, not exact shares", "Windows · engine counters", "GPU memory · NVML, used of total", "power · NVML, of limit", "/dev/dxg holders, no usage measured"}) {
    check(has(g, label), std::string("labelled: ") + label);
  }
  for (const int w : {40, 80, 112, 131}) {
    const std::string t = as_text(render(full, w, 66, kNow, &demo, view::gpu));
    const std::string n = " at " + std::to_string(w);
    check(has(t, "device · NVML") && has(t, "engine counters,") && has(t, "VM level, not exact shares") && has(t, "Windows · engine counters"), "wrapped titles stay whole" + n);
    check(has(t, "dxg_denied 36") && has(t, "no usage measured"), "holders carry no usage" + n);
    check(!has(t, "88%"), "52 + 36 is never shown" + n);
    check(has(t, "unexplained") && !has(t, "different measures") && !has(t, "expected to match"), "device and engine: only that the difference is unexplained" + n);
  }
  check(has(g, "9.0/12.0 GiB") && has(g, "187/200 W") && !has(g, "75%") && !has(g, "94%"), "bytes and watts stay in their units");

  // The GPU lists: Windows by GPU%, holders apart, no Linux figure.
  const auto frame = render(full, 131, 50, kNow, &demo, view::gpu);
  bool seen_right = false;
  size_t vmwp = 0, game = 0, n = 0;
  for (const line& l : frame) {
    const std::string t = as_text({l});
    ++n;
    if (has(t, "vmwp.exe")) vmwp = n;
    if (has(t, "Game.Shipping")) game = n;
    if (has(t, "한글추론기") || has(t, "python3")) {
      const size_t bar = t.find(" │ ");
      check(bar != std::string::npos && t.find(has(t, "python3") ? "python3" : "한글추론기") > bar, "holders right of the divider");
      check(!has(t.substr(bar), "%") && !has(t.substr(bar), "on GPU"), "no per-Linux-process GPU figure");
      seen_right = true;
    }
  }
  check(seen_right && vmwp && game && vmwp < game, "Windows GPU users sorted by GPU%");
  snapshot resort = full;
  resort.gpu_engines = {{11736, "3D", 5}, {9002, "3D", 80}};
  const std::string rs = as_text(render(resort, 131, 50, kNow, &demo, view::gpu));
  check(rs.find("Game.Shipping") < rs.find("vmwp.exe"), "the GPU list follows GPU%, not cores");
  check(!has(as_text(render(full, 131, 50, kNow, &demo, view::gpu)), "svchost.exe"), "no process without GPU use in that list");
  const std::string stacked = as_text(render(full, 60, 66, kNow, &demo, view::gpu));
  check(stacked.rfind("vmwp.exe") < stacked.find("한글추론기", stacked.find("PROCESSES")), "stacked GPU lists do not interleave");

  // The CPU panel.
  for (const int w : {80, 131, 200}) {
    const std::string t = as_text(render(full, w, 60, kNow, &demo, view::cpu));
    const std::string where = " at " + std::to_string(w);
    check(has(t, "whole machine, includes the WSL VM") && has(t, "guest view, inside the VM"), "CPU graphs name their source" + where);
    check(has(t, "hottest core 3 at 96%") && has(t, "hottest core 6 at 97%"), "both sets of core rows" + where);
    check(!has(t, "62%") && !has(t, "61.6"), "Windows and WSL busy are not added" + where);
    check(has(t, "top ") && has(t, "ollama") && has(t, "svchost.exe"), "both process lists" + where);
  }
  // The title rows themselves: the WINDOWS and WSL rule rows hold the same words.
  for (const int w : {131, 200}) {
    const auto f = render(full, w, 60, kNow, &demo, view::cpu);
    const std::string title = line_with(f, "%  Windows CPU");
    check(has(title, "Windows CPU · whole machine, includes the WSL VM"), "the Windows graph title names its source");
    check(has(title, "WSL CPU · guest view, inside the VM"), "the WSL graph title names its source");
    check(has(as_text(f), "two systems' own views of overlapping work, never added"), "the CPU rule says the views overlap");
  }
  const auto wrapped = render(full, 80, 60, kNow, &demo, view::cpu);
  const std::string second = line_with(wrapped, "%  Windows CPU", 1);
  check(has(second, "includes the WSL VM") && has(second, "guest view, inside the VM"), "wrapped titles keep their source");
  const auto cpu = render(full, 131, 60, kNow, &demo, view::cpu);
  for (const line& l : cpu) {
    const std::string t = as_text({l});
    if (has(t, "ollama")) check(t.find("ollama") > t.find(" │ "), "CPU panel: Linux names right of the divider");
    if (has(t, "svchost.exe")) check(t.find("svchost.exe") < t.find(" │ "), "CPU panel: Windows names left of the divider");
  }

  // Missing is n/a, in every panel, and history does not fill in for it.
  const std::string bg = as_text(render(bare(), 131, 50, kNow, &demo_bare, view::gpu));
  for (const char* v : {"n/a  device · NVML", "n/a  WSL VM ·", "n/a  Windows · engine counters", "n/a  GPU memory", "n/a  power"}) check(has(bg, v), std::string("missing: ") + v);
  check(count_braille(graph_rows(bg)) == 0, "no measurement, a blank graph");
  check(has(bg, "device  unavailable") && has(bg, "unavailable: no GPU engine data") && has(bg, "unavailable: no WSL data"), "bare GPU panel says what is missing");
  check(!has(bg.substr(0, bg.find("PROCESSES")), " 0%"), "the GPU panel shows no zero");

  const std::string bc = as_text(render(bare(), 131, 50, kNow, &demo_bare, view::cpu));
  check(has(bc, "n/a  WSL CPU") && has(bc, "WSL data unavailable"), "no WSL line is n/a");
  check(!has(bc, "  0%  WSL"), "no WSL line is not 0");
  snapshot no_cpu = bare();
  no_cpu.cpu = {};
  check(has(as_text(render(no_cpu, 131, 50, kNow, &demo_bare, view::cpu)), "n/a  Windows CPU"), "a failed CPU read is n/a");
  const std::string no_cpu_screen = as_text(render(no_cpu, 131, 50, kNow));
  const std::string windows_block = no_cpu_screen.substr(no_cpu_screen.find("WINDOWS"));
  check(has(windows_block, "CPU   n/a") && !has(windows_block.substr(0, windows_block.find("cores")), "0%"), "a failed CPU read is n/a on the overall screen too");
  check(has(as_text(render(bare(), 131, 50, kNow)), "CPU   ━"), "a CPU read keeps its bar");
  history nc;
  no_cpu.unix_ms = kNow;
  nc.push(no_cpu);
  check(std::isnan(nc.samples().back().windows_cpu), "a failed CPU read is a gap");

  snapshot part = busy();
  part.gpu_device.has_util = part.gpu_device.has_power = false;
  part.gpu_device.has_mem = false;
  part.gpu_split.valid = false;
  const std::string pt = as_text(render(part, 131, 50, kNow, &demo, view::gpu));
  for (const char* v : {"n/a  device · NVML", "n/a  WSL VM", "n/a  Windows ·", "n/a  GPU memory", "n/a  power"}) check(has(pt, v), std::string("has_* false: ") + v);
  part = busy();
  part.gpu_device.power_limit_w = 0;
  part.unix_ms = kNow;
  history nl;
  nl.push(part);
  check(has(as_text(render(part, 131, 50, kNow, &nl, view::gpu)), "187 W") && std::isnan(nl.samples().back().gpu_power_pct), "no limit: watts printed, no height invented");

  // Blank with little history.
  const std::string eg = as_text(render(full, 131, 50, kNow, &empty, view::gpu));
  check(count_braille(graph_rows(eg)) == 0 && has(eg, "-60s") && has(eg, "91%"), "an empty history is blank graphs and the live number");

  // Short windows: graphs shrink, then lower blocks go, the footer stays.
  const std::string tall = as_text(render(full, 131, 50, kNow, &demo, view::gpu));
  check(has(tall, "device  util") && has(tall, "PROCESSES") && !has(tall, "rows hidden"), "a tall window shows everything");
  const std::string mid = as_text(render(full, 131, 12, kNow, &demo, view::gpu));
  check(has(mid, "100│") && has(mid, "some rows hidden") && has(mid, "[q] quit"), "a short window says it dropped something");
  panel one{{"title"}, "1%", tone::text, &history_sample::gpu_util};
  const std::vector<panel> pair = {one, one};
  std::vector<grid_row> fitted;
  add_panels(fitted, pair, 131);
  const int whole = rows_height(fitted);
  check(!fit_rows(fitted, whole) && fitted[0].plot, "what fits is not hidden");
  check(fit_rows(fitted, whole - 1) && !fitted[0].plot, "a plot that does not fit is reported");
  // The rule row is the first to go; it counts as hidden too.
  const auto no_rule = render(full, 80, 23, kNow, &demo, view::gpu);
  check(!has(as_text(no_rule), "GPU ─") && has(as_text(no_rule), "vmwp.exe") && marks_hidden(no_rule), "a dropped GPU rule is marked");
  // Too narrow for the note: a marker the right edge cannot cut.
  for (const int w : {1, 10, 36, 40, 48, 59}) {
    for (const view v : {view::gpu, view::cpu}) {
      const auto f = render(full, w, 8, kNow, &demo, v);
      check(as_text({f.back()}).rfind("…", 0) == 0, "hidden is marked at width " + std::to_string(w));
    }
  }
  check(has(as_text({render(full, 60, 8, kNow, &demo, view::gpu).back()}), "[q] quit   … some rows hidden"), "60 columns hold the note");
  check(!marks_hidden(render(full, 40, 66, kNow, &demo, view::cpu)), "nothing hidden, no marker");

  // /dev/dxg holders: a count, and the warning whenever the block is drawn.
  const snapshot many = with_holders(full, 22);
  const auto crowded = render(many, 131, 50, kNow, &demo, view::gpu);
  check(has(as_text(crowded), "of 22") && has(as_text(crowded), "dxg_denied 36") && marks_hidden(crowded), "a cut holder list counts itself");
  check(has(as_text(render(full, 131, 50, kNow, &demo, view::gpu)), "top 2 of 2"), "a whole holder list counts too");
  for (const int w : {30, 40, 50, 63, 64, 80, 100, 131, 200}) {
    for (int rows = 1; rows <= 66; ++rows) {
      const auto f = render(many, w, rows, kNow, &demo, view::gpu);
      const std::string text = as_text(f);
      const std::string at = " at " + std::to_string(w) + "x" + std::to_string(rows);
      if (!has(text, "no usage measured")) continue;
      check(has(text, "dxg_denied 36"), "the warning survives" + at);
      check(has(text, "of 22"), "the count survives" + at);
      check(marks_hidden(f), "the cut is marked" + at);
      check(!has(text, "top 0 of 22"), "a drawn list has a name in it" + at);
    }
  }
  check(!has(as_text(render(no_wsl(), 131, 50, kNow, &demo, view::gpu)), "dxg_denied"), "no WSL line, no warning row");
  bool no_room_cut = false;
  const wsl_view many_view = read_wsl(many.wsl_json);
  check(has(as_text(dxg_holder_list(many_view, 60, 0, no_room_cut)), "dxg_denied 36") && no_room_cut, "the warning needs no name row");

  // An unreadable line that names dxg_denied is no WSL data: it reserves no warning row.
  snapshot unread = busy();
  unread.wsl_json = R"({"schema":2,"busy_pct":1,"memory":{},"dxg_denied":36})";
  for (const int w : {60, 131}) {
    for (int rows = 1; rows <= 66; ++rows) {
      const bool drawn = has(as_text(render(unread, w, rows, kNow, &demo, view::gpu)), "PROCESSES");
      check(drawn == has(as_text(render(no_wsl(), w, rows, kNow, &demo, view::gpu)), "PROCESSES"), "no warning row without WSL data, " + std::to_string(w) + "x" + std::to_string(rows));
    }
  }

  // More GPU users than rows: a cut Windows list is marked, and stacked lists split the rows.
  snapshot users = with_holders(full, 22);
  users.gpu_engines.clear();
  for (uint32_t i = 0; i < 15; ++i) users.gpu_engines.push_back({10000 + i * 4, "3D", 50.0 - i});
  snapshot few_holders = users;
  few_holders.wsl_json = full.wsl_json;
  const auto cut_users = render(few_holders, 131, 50, kNow, &demo, view::gpu);
  check(has(as_text(cut_users), "of 15 by GPU%") && has(as_text(cut_users), "top 2 of 2") && marks_hidden(cut_users), "a cut Windows list is marked");
  const auto top_of = [](const std::string& t, const char* tail) {
    const size_t end = t.find(tail);
    return end == std::string::npos ? -1 : atoi(t.c_str() + t.rfind("top ", end) + 4);
  };
  int odd = 0;
  for (int rows = 1; rows <= 66; ++rows) {
    const std::string t = as_text(render(users, 60, rows, kNow, &demo, view::gpu));
    const int windows = top_of(t, " of 15 by GPU%"), holders = top_of(t, " of 22");
    if (windows < 0 || holders < 0) continue;
    check(windows - holders == 0 || windows - holders == 1, "stacked lists split the rows evenly at 60x" + std::to_string(rows) + ": " + std::to_string(windows) + "," + std::to_string(holders));
    odd += windows - holders;
  }
  check(odd > 0, "an odd row goes to the first list");

  // A process at zero is no GPU use.
  snapshot zero = busy();
  zero.gpu_engines.push_back({4, "3D", 0});
  const auto zf = render(zero, 131, 50, kNow, &demo, view::gpu);
  check(has(as_text(zf), "top 3 of 3 by GPU%") && !has(as_text(zf), "System"), "an engine at 0% is not a GPU user");

  // Without a limit the watts stand alone and the title says why the graph is blank.
  snapshot limitless = busy();
  limitless.gpu_device.power_limit_w = 0;
  check(has(as_text(render(limitless, 200, 50, kNow, &empty, view::gpu)), "no limit reported, not graphed"), "no limit is named in the title");
  const std::string low = as_text(render(full, 131, 6, kNow, &demo, view::gpu));
  check(has(low, "91%  device · NVML") && has(low, "52%  WSL VM") && has(low, "[q] quit") && !has(low, "100│"), "a very short window keeps the values and labels");
  const std::string narrow_low = as_text(render(full, 50, 12, kNow, &demo, view::gpu));
  check(has(narrow_low, "100│") && has(narrow_low, "36%  Windows ·") && marks_hidden(render(full, 50, 12, kNow, &demo, view::gpu)), "a stack that cannot fit keeps every value it can");
  const std::string cpu_low = as_text(render(full, 80, 24, kNow, &demo, view::cpu));
  check(has(cpu_low, "svchost.exe") || has(cpu_low, "vmmemWSL"), "at 80x24 the CPU panel's Windows list has rows");
  const std::string gpu_low = as_text(render(full, 80, 24, kNow, &demo, view::gpu));
  check(has(gpu_low, "vmwp.exe") && has(gpu_low, "python3") && has(gpu_low, "dxg_denied"), "at 80x24 the GPU panel's lists have names");

  // Names, as a Windows GPU user and as a /dev/dxg holder.
  snapshot evil = busy();
  evil.gpu_engines.push_back({9003, "3D", 60});
  const std::string pids = "\"dxg_pids\":[4121,4388";
  evil.wsl_json.replace(evil.wsl_json.find(pids), pids.size(), pids + ",4400");
  for (const view v : {view::overall, view::gpu, view::cpu}) {
    check(vt_clean(vt_frame(render(evil, 131, 50, kNow, &demo, v), 50)), "names stay safe in every panel");
    check(vt_clean(vt_frame(render(evil, 60, 66, kNow, &demo, v), 66)), "names stay safe when stacked");
  }
  check(has(as_text(render(evil, 131, 50, kNow, &demo, view::gpu)), "bad.exe"), "the hostile name is on the panel");
}

// 1, 1 + step, ... and top itself.
std::vector<int> sizes(int top, int step) {
  std::vector<int> out;
  for (int n = 1; n < top; n += step) out.push_back(n);
  out.push_back(top);
  return out;
}

// Widths 1 to 300 and heights 1 to 66, every view: one check a frame. A frame costs about 0.4 ms, and
// layout turns on the size and the width of the numbers, not on the shape of the history. So every size
// goes to the hostile numbers, and every third size to the rest.
// The dxg_denied warning is drawn whole: a row cut at its panel's edge cannot end in the wording, and
// a wording that names a root process as a fact is not one of these two.
void check_denied_note() {
  const snapshot base = busy();
  const history demo = demo_minute(base);
  const auto row_of = [](const std::string& text, const char* word) {
    const size_t at = text.find(word);
    if (at == std::string::npos) return std::string();
    const size_t start = text.rfind('\n', at) + 1;  // No newline before it wraps to 0
    return text.substr(start, text.find('\n', at) - start);
  };
  const auto ends = [](const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
  };
  for (const int denied : {38, 100, 999}) {
    snapshot s = base;
    const std::string key = "\"dxg_denied\":36";
    s.wsl_json.replace(s.wsl_json.find(key), key.size(), "\"dxg_denied\":" + std::to_string(denied));
    const std::string head = "dxg_denied " + std::to_string(denied) + ": ";
    const std::string full = head + "may miss a root holder", brief = head + "may miss root";

    for (int w = 37; w <= 300; ++w) {
      const std::string at = " at " + std::to_string(w) + ", " + head;
      const int list = w >= 64 ? w - 3 - (w - 3) / 2 : w;  // The holders' column, stacked below 64
      const std::string gpu = row_of(as_text(render(s, w, 66, kNow, &demo, view::gpu)), "dxg_denied");
      const std::string overall = row_of(as_text(render(s, w, 66, kNow, &demo, view::overall)), "dxg_denied");
      check(ends(gpu, static_cast<int>(full.size()) <= list ? full : brief), "the warning is whole in the GPU panel" + at);
      check(ends(overall, static_cast<int>(full.size()) <= w - 8 ? full : brief), "the warning is whole in the overall panel" + at);
    }

    // The clear wording where it has room, on the frames people read.
    for (const int w : {80, 100, 131, 200}) {
      const std::string at = " at " + std::to_string(w) + "x24, " + head;
      check(ends(row_of(as_text(render(s, w, 24, kNow, &demo, view::gpu)), "dxg_denied"), full), "the clear wording fits the GPU panel" + at);
      check(ends(row_of(as_text(render(s, w, 24, kNow, &demo, view::overall)), "dxg_denied"), full), "the clear wording fits the overall panel" + at);
      check(ends(row_of(as_text(render(s, w, 24, kNow)), "dxg_denied"), full), "the clear wording fits the plain screen" + at);
    }
  }
  bool cut = false;
  const wsl_view read = read_wsl(base.wsl_json);
  check(ends(as_text(dxg_holder_list(read, 39, 2, cut)), "dxg_denied 36: may miss a root holder\n"), "39 columns, the list at 80 wide, hold it");
  check(ends(as_text(dxg_holder_list(read, 37, 2, cut)), "dxg_denied 36: may miss a root holder\n"), "37 columns are the least for the clear wording");
  check(ends(as_text(dxg_holder_list(read, 36, 2, cut)), "dxg_denied 36: may miss root\n"), "36 columns take the shorter one");
  check(denied_note(1000, 31) == "dxg_denied 1000: may miss root", "31 columns hold four digits");
}

// "on GPU" read as usage. A process here has /dev/dxg open and nothing more is known, so no panel at any
// width says otherwise, and the two places that name holders carry the label while they can.
void check_holder_wording() {
  const auto row_of = [](const std::string& text, const char* word) {
    const size_t at = text.find(word);
    if (at == std::string::npos) return std::string();
    const size_t start = text.rfind('\n', at) + 1;  // No newline before it wraps to 0
    return text.substr(start, text.find('\n', at) - start);
  };
  const auto after_bar = [](const std::string& row) { return row.substr(row.find(" │ ") == std::string::npos ? 0 : row.find(" │ ") + 5); };

  const snapshot base = busy(), many = with_holders(busy(), 22), gone = no_wsl();
  const snapshot none = [] {
    snapshot s = busy();
    const std::string pids = "\"dxg_pids\":[4121,4388]";
    s.wsl_json.replace(s.wsl_json.find(pids), pids.size(), "\"dxg_pids\":[]");
    return s;
  }();
  const snapshot long_distro = [] {
    snapshot s = busy();
    const std::string key = "\"distro\":\"Ubuntu-24.04\"";
    s.wsl_json.replace(s.wsl_json.find(key), key.size(), "\"distro\":\"" + std::string(90, 'd') + "\"");
    return s;
  }();
  const history demo = demo_minute(base);

  // Every state, every width, every panel: the words never come back, whole or cut.
  for (const snapshot* s : {&base, &none, &many, &gone, &long_distro}) {
    for (int w = 1; w <= 300; ++w) {
      for (const int rows : {24, 66}) {
        std::string frames = as_text(render(*s, w, rows, kNow));
        for (const view v : {view::overall, view::gpu, view::cpu}) frames += as_text(render(*s, w, rows, kNow, &demo, v));
        check(!has(frames, "on GPU") && !has(frames, "on G…") && !has(frames, "on GP…"), "holders are never called on GPU at " + std::to_string(w) + "x" + std::to_string(rows));
      }
    }
  }

  // The overall row in each of its states, as it reads at a normal width.
  check(has(as_text(render(base, 131, 40, kNow)), "dxg     python3 4121  한글추론기 4388    /dev/dxg holders, no usage measured\n"), "the holders row names /dev/dxg");
  check(has(as_text(render(none, 131, 40, kNow)), "dxg     no holder seen    /dev/dxg holders, no usage measured\n"), "an empty holders row keeps its label");
  check(has(as_text(render(gone, 131, 40, kNow)), "dxg     unavailable: no WSL data  /dev/dxg holders, no usage measured\n"), "an unavailable holders row keeps its label");

  // The label is whole down to the narrowest panel: names and the long wording go first.
  for (int w = 36; w <= 300; ++w) {
    for (const snapshot* s : {&base, &many}) {
      const std::string row = row_of(as_text(render(*s, w, 66, kNow)), "dxg  ");
      check(row.size() >= 18 && row.compare(row.size() - 18, 18, " no usage measured") == 0, "the holders row ends in its label at " + std::to_string(w));
    }
    if (w >= 43) {
      const std::string row = row_of(as_text(render(none, w, 66, kNow)), "dxg  ");
      check(has(row, "no holder seen") && has(row, " no usage measured"), "the empty row keeps its label at " + std::to_string(w));
    }
    if (w >= 51) {
      const std::string row = row_of(as_text(render(gone, w, 66, kNow)), "dxg  ");
      check(has(row, "unavailable: no WSL data  ") && has(row, " no usage measured"), "the unavailable row keeps its label at " + std::to_string(w));
    }
  }

  // The list under PROCESSES, side by side or stacked, both panels that draw it.
  for (int w = 41; w <= 300; ++w) {
    for (const snapshot* s : {&base, &long_distro}) {
      for (const view v : {view::overall, view::cpu}) {
        const std::string t = as_text(render(*s, w, 66, kNow, &demo, v)), at = " at " + std::to_string(w) + " view " + std::to_string(static_cast<int>(v));
        const std::string head = after_bar(row_of(t, " of 64,"));
        if (head.empty()) continue;  // The panel has no room for the list
        check(head.size() >= 18 && head.compare(head.size() - 18, 18, " no usage measured") == 0, "the WSL list says no usage is measured" + at);
        const std::string below = t.substr(t.find(" of 64,"));
        const std::string cols = after_bar(row_of(below, "RSS GiB"));
        check(cols.size() >= 5 && cols.compare(cols.size() - 5, 5, "  dxg") == 0, "the column names /dev/dxg, not the GPU" + at);
        const std::string held = after_bar(row_of(below, "python3"));
        check(held.size() >= 8 && held.compare(held.size() - 8, 8, "  holder") == 0, "a holder's cell is whole" + at);
      }
    }
  }
  snapshot hostile = base;
  const std::string name = "\"distro\":\"Ubuntu-24.04\"";
  hostile.wsl_json.replace(hostile.wsl_json.find(name), name.size(), R"("distro":"\u001b]0;pwned\u0007\u202e)" + std::string(60, 'd') + "\"");
  for (const int w : {60, 100, 131}) check(vt_clean(vt_frame(render(hostile, w, 66, kNow), 66)), "a cut distro stays safe at " + std::to_string(w));

  const auto head_at = [&](int w) { return as_text({wsl_list(read_wsl(base.wsl_json), w, 3)[0]}); };
  check(has(head_at(100), "WSL Ubuntu-24.04  top 3 of 64, /dev/dxg holders first, then cores, no usage measured\n"), "the longest wording where it fits");
  check(has(head_at(64), "WSL Ubuntu-24.04  top 3 of 64, holders first, no usage measured\n"), "the 131-column list keeps the order");
  check(has(head_at(49), "WSL Ubuntu-…  top 3 of 64, dxg: no usage measured\n"), "the distro gives way to the label");
}

void check_sweep(const snapshot& s, const history& h, const char* name, int step = 3) {
  for (const view v : {view::overall, view::gpu, view::cpu}) {
    for (const int w : sizes(300, step)) {
      for (const int rows : sizes(66, step)) {
        const auto frame = render(s, w, rows, kNow, &h, v);
        bool fits = static_cast<int>(frame.size()) <= rows && !frame.empty();
        for (const line& l : frame) {
          int cols = 0;
          for (const span& p : l) cols += display_width(p.text);
          fits = fits && cols <= w;
        }
        check(fits, std::string(name) + " view " + std::to_string(static_cast<int>(v)) + " " + std::to_string(w) + "x" + std::to_string(rows));
      }
    }
  }
}

void check_sizes_with_history() {
  const snapshot full = busy();
  const history empty, demo = demo_minute(full);
  check_sweep(full, empty, "empty history");
  check_sweep(bare(), demo_minute(bare()), "bare, nothing to graph");

  history partial;
  for (int age = 4; age >= 0; --age) partial.push(sample_at(full, kNow - age * 1000, 30, 40, 5));
  check_sweep(full, partial, "partial history");

  history gaps;
  for (const int age : {59, 58, 40, 39, 12, 3, 0}) gaps.push(sample_at(full, kNow - age * 1000, 30, 40, 5));
  check_sweep(full, gaps, "gaps");

  snapshot s = busy();
  s.gpu_device.util_pct = kNaN;
  s.gpu_device.power_w = kInf;
  s.gpu_device.power_limit_w = 1e-300;
  s.gpu_split = {true, 1e308, kNaN, 1, 1};
  s.cpu.busy_pct = kNaN;
  s.cpu.core_pct[0] = kInf;
  s.interval_s = 1e308;
  s.wsl_age_s = 1e308;
  s.memory.available = s.memory.total * 2;
  s.nets[0].in_bytes_s = -1;
  s.gpu_engines.push_back({9001, "3D", kNaN});
  s.gpu_engines.push_back({9002, "3D", 1e308});
  s.gpu_device.mem_used = ~0ull;
  history wild;
  for (int age = 30; age >= 0; --age) {
    snapshot t = s;
    t.unix_ms = kNow - age * 1000;
    t.gpu_device.util_pct = age % 3 == 0 ? kNaN : age % 3 == 1 ? 1e308 : -1e308;
    t.gpu_split.vm_pct = age % 2 ? kInf : -kInf;
    wild.push(t);
  }
  check_sweep(s, wild, "hostile numbers", 1);
}

#ifdef _WIN32
// A real failed write ends a redirected run; nothing else may. stdout is swapped by hand, because the
// console reads it in its constructor. printf is not affected: the C runtime keeps its own handle.
void check_write_failed() {
  const HANDLE saved = GetStdHandle(STD_OUTPUT_HANDLE);
  const std::vector<line> frame = render(busy(), 80, 24, kNow);

  HANDLE reader = nullptr, writer = nullptr;
  check(CreatePipe(&reader, &writer, nullptr, 1 << 20) != 0, "a pipe for the check");
  SetStdHandle(STD_OUTPUT_HANDLE, writer);
  {
    console c;
    check(!c.is_terminal() && !c.write_failed(), "nothing has failed before the first write");
    c.draw(frame);
    check(!c.write_failed(), "a pipe with a reader takes a frame");
    c.draw({});
    check(!c.write_failed(), "an empty frame writes nothing and fails nothing");
    CloseHandle(reader);
    c.draw(frame);
    check(c.write_failed(), "a pipe whose reader has gone is a failed write");
  }
  CloseHandle(writer);

  HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  SetStdHandle(STD_OUTPUT_HANDLE, nul);
  {
    console c;
    c.draw(frame);
    check(!c.write_failed(), "output sent to NUL is not a failure");
  }
  CloseHandle(nul);

  nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  SetStdHandle(STD_OUTPUT_HANDLE, nul);
  {
    console c;
    c.draw(frame);
    check(c.write_failed(), "any refused write counts, not only a pipe");
  }
  CloseHandle(nul);

  SetStdHandle(STD_OUTPUT_HANDLE, nullptr);
  {
    console c;
    c.draw(frame);
    check(c.write_failed(), "no stdout at all is a failed write");
  }

  HANDLE screen = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (screen == INVALID_HANDLE_VALUE) {
    printf("console output: no console here, live console checks skipped\n");
  } else {
    SetStdHandle(STD_OUTPUT_HANDLE, screen);
    {
      console c;
      check(c.is_terminal(), "CONOUT$ is a terminal");
      c.draw(frame);
      c.draw({});
      check(!c.write_failed(), "a live console does not trip it");
    }
    CloseHandle(screen);
    // Read-only, so VT cannot turn on and the plain branch writes to a console, and WriteFile refuses.
    screen = CreateFileW(L"CONOUT$", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    SetStdHandle(STD_OUTPUT_HANDLE, screen);
    {
      console c;
      check(!c.is_terminal(), "without write access the console takes the plain branch");
      c.draw(frame);
      check(!c.write_failed(), "a console in the plain branch does not trip it either");
    }
    CloseHandle(screen);
    printf("console output: live console checks ran\n");
  }
  SetStdHandle(STD_OUTPUT_HANDLE, saved);
}

// Puts real events in the console's input buffer, where there is one; a piped run has none.
void check_console() {
  HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  DWORD before = 0;
  if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &before)) {
    console c;
    check(c.poll_key() == 0, "no console, no key");
    printf("console input: no console here, key checks skipped\n");
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    return;
  }
  const DWORD start = before | ENABLE_VIRTUAL_TERMINAL_INPUT;  // So that clearing it is seen
  check(SetConsoleMode(in, start) != 0, "VT input can be set for the check");
  {
    console c;
    DWORD during = 0;
    GetConsoleMode(in, &during);
    check(!(during & ENABLE_ECHO_INPUT) && !(during & ENABLE_LINE_INPUT), "echo and line input off");
    check((during & ENABLE_PROCESSED_INPUT) != 0, "Ctrl+C stays processed");
    check(!(during & ENABLE_VIRTUAL_TERMINAL_INPUT), "VT input off");
    // Removing a handler fails unless it is registered. Under check.sh stdout is a pipe, the case
    // where the handler once came too late.
    check(SetConsoleCtrlHandler(on_ctrl, FALSE) != 0, "Ctrl+C is caught whatever stdout is");
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    FlushConsoleInputBuffer(in);
    check(c.poll_key() == 0, "no key waiting, no wait");
    const auto key = [](WORD vk, WCHAR ch, BOOL down = TRUE, DWORD state = 0) {
      INPUT_RECORD r{};
      r.EventType = KEY_EVENT;
      r.Event.KeyEvent.bKeyDown = down;
      r.Event.KeyEvent.wRepeatCount = 1;
      r.Event.KeyEvent.wVirtualKeyCode = vk;
      r.Event.KeyEvent.uChar.UnicodeChar = ch;
      r.Event.KeyEvent.dwControlKeyState = state;
      return r;
    };
    INPUT_RECORD other{};
    other.EventType = FOCUS_EVENT;
    const INPUT_RECORD events[] = {
        other, key('Q', L'q'), key('Q', L'q', FALSE),
        key(VK_LEFT, 0), key(0, 0xD55C), key('2', L'2'), key('2', L'2', FALSE),
        key('Q', 0x3142),                                       // q under Hangul input mode
        key(VK_DELETE, 0x7F), key(VK_PACKET, 0xD83D), key(VK_PACKET, 0xDE00),  // Delete, a surrogate pair
        key(0, L'q'), key('X', L'x'), key('Q', 0x11, TRUE, LEFT_CTRL_PRESSED),  // A character alone, another key, Ctrl+Q
        key(VK_NUMPAD3, L'3'), key('1', L'!', TRUE, SHIFT_PRESSED), key('Q', L'Q', TRUE, SHIFT_PRESSED)};
    const DWORD total = static_cast<DWORD>(std::size(events));
    DWORD put = 0;
    WriteConsoleInputW(in, events, total, &put);
    check(put == total, "events written");
    check(c.poll_key() == 'q', "a key comes back");
    check(c.poll_key() == '2', "releases, focus, arrows and non-ASCII are passed over");
    check(c.poll_key() == 'q', "q under Hangul input mode");
    check(c.poll_key() == '3', "Delete, a surrogate pair, Ctrl+Q and a bare character are passed over; the numeric keypad counts");
    check(c.poll_key() == '1', "Shift on a digit, for the layouts that need it");
    check(c.poll_key() == 'q', "Shift on q");
    check(c.poll_key() == 0, "then nothing");
  }
  DWORD after = 0;
  GetConsoleMode(in, &after);
  check(after == start, "the input mode is restored");
  check(SetConsoleCtrlHandler(on_ctrl, FALSE) == 0, "the Ctrl+C handler is removed");
  SetConsoleMode(in, before);
  CloseHandle(in);
  printf("console input: key checks ran\n");

  // A failed CONIN$ open. Only with stdout elsewhere: this process keeps no console afterwards.
  DWORD out_mode = 0;
  if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &out_mode) && FreeConsole()) {
    HANDLE gone = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    check(gone == INVALID_HANDLE_VALUE, "CONIN$ cannot be opened without a console");
    if (gone != INVALID_HANDLE_VALUE) CloseHandle(gone);
    console c;
    check(c.poll_key() == 0 && !c.is_terminal(), "a failed CONIN$ open gives no key");
    printf("console input: CONIN$ failure checked\n");
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc == 2 && strcmp(argv[1], "quick") == 0;
#ifdef _WIN32
  { console c; int w = 0, h = 0; check(!c.is_terminal() || c.size(w, h), "console opens"); }
#endif
#ifdef _WIN32
  if (argc == 3) {  // The panels on the real console, keys as the program has them
    const snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    const history demo = demo_minute(s);
    console c;
    view v = view::overall;
    int w = 131, h = 40;
    for (char k = 0; k != 'q' && !console::stop_requested(); k = c.poll_key()) {
      if (k == '1') v = view::overall;
      if (k == '2') v = view::gpu;
      if (k == '3') v = view::cpu;
      c.size(w, h);
      c.draw(render(s, w, h, kNow, &demo, v));
      if (!c.is_terminal()) break;
      Sleep(50);
    }
    return 0;
  }
  if (argc == 2 && !quick) {  // A frame on the real console, until Ctrl+C
    const snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    console c;
    int w = 131, h = 40;
    while (!console::stop_requested()) {
      c.size(w, h);
      c.draw(render(s, w, h, kNow));
      if (!c.is_terminal()) break;
      Sleep(200);
    }
    return 0;
  }
#endif
  if (argc == 4 || argc == 5) {
    const snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    const history demo = demo_minute(s);
    const view v = argc == 4 || strcmp(argv[4], "overall") == 0 ? view::overall : strcmp(argv[4], "gpu") == 0 ? view::gpu : view::cpu;
    const std::string t = as_text(render(s, atoi(argv[2]), atoi(argv[3]), kNow, argc == 5 ? &demo : nullptr, v));
    fwrite(t.data(), 1, t.size(), stdout);
    return 0;
  }
#ifdef _WIN32
  check_write_failed();  // Before check_console, which can leave this process without a console
  check_console();
#endif
  check_width();
  check_reader();
  if (!quick) {
    check_size(busy(), "busy");
    check_size(bare(), "bare");
  }
  check_honesty();
  check_edges();
  check_time_axis();
  check_history_ring();
  check_scale();
  check_panels();
  if (!quick) {
    check_denied_note();
    check_holder_wording();
    check_sizes_with_history();
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
