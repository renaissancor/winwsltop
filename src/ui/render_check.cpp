// Checks the renderer on hand-built snapshots. render_check <busy|bare> W H prints a frame as text,
// the one overall screen with no history; add overall, gpu or cpu for that panel over a made-up minute.
// render_check <busy|bare|contended|idle|stale|noisy> live draws the panels on the console, keys 1 2 3 t q.
// render_check <busy|bare> draws the plain screen on the console until Ctrl+C.
// render_check <contended|idle|stale|noisy> W H [span_s] prints the large layout of panel 3.
// render_check vt <fixture> W H FILE [span_s] writes one frame's VT bytes to FILE, to render as an image.
// render_check measure prints frame bytes and the time to build a frame.
// render_check quick runs everything but the size sweeps, about a second, for mutating the renderer.
// Build: scripts/build.sh src/ui/render_check.cpp   (it includes the sources it tests)
#include "ui/render.cpp"
#include "ui/history.h"
#include "core/wsl_view.cpp"
#ifdef _WIN32
#include "ui/console.cpp"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
  s.vm = {7120, 11736, 10898438144, 3.1, true};
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
bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

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
  const std::string t = as_text(render(stale, 99, 40, kNow, &h, view::cpu));
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
  back.interval_s = 599;
  h.push(sample_at(back, kNow - 3000000, 40, 1, 1));
  check(h.samples().size() == 2 && h.samples()[1].unix_ms - h.samples()[0].unix_ms == 599000, "a stall under what is kept shifts");
  back.interval_s = 600;
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

  // The mean in a column, in either order, and the maximum kept apart for the label and its dot.
  for (const double first : {10.0, 90.0}) {
    h = history();
    h.push(sample_at(base, kNow - 500, first, 1, 1));
    h.push(sample_at(base, kNow, 100 - first, 1, 1));
    const plotted d = plot_series(h, &history_sample::gpu_util, kNow, 4, history::kSpanMs);
    check(d.cols[3] == 50, "two samples in one column give their mean");
    check(d.stats.max == 90 && d.stats.max_at == 3 && d.stats.max_count == 2 && d.stats.avg == 50, "the column keeps the window's maximum and how many samples it holds");
  }
  // A joined gap of two columns takes the later sample in both: it stands for the time since the one before.
  h = history();
  h.push(sample_at(base, kNow - 1000, 20, 1, 1));
  h.push(sample_at(base, kNow, 80, 1, 1));
  std::vector<double> joined = plot_columns(h, &history_sample::gpu_util, kNow, 121);  // 2 columns a second
  check(joined[120] == 80 && joined[119] == 80 && joined[118] == 20 && known_columns(joined) == 3, "a joined gap of two columns takes the later sample in both");

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
  const std::string cells = graph_rows(as_text(render(unreadable, 99, 30, kNow, &h, view::cpu)));
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
  check(h.samples().size() == 601 && h.samples().back().unix_ms - h.samples().front().unix_ms == 600000, "the ring keeps 10 minutes, both ends in");
  h = history();
  for (int i = 0; i < 5000; ++i) h.push(sample_at(base, kNow + i, 40, 1, 1));
  check(h.samples().size() == 601, "the ring is bounded however fast it is pushed");
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
  check(h.samples().size() == 8 && h.samples().front().unix_ms == kNow - 700000, "kept for 600 s, and the one sample before them");

  // The slots never move once the first push has sized the ring.
  h = history();
  h.push(sample_at(base, kNow, 40, 1, 1));
  const history_sample* const ring = &h.samples().front();
  for (int i = 1; i < 3000; ++i) h.push(sample_at(base, kNow + i * 1000, 40, 1, 1));
  bool same_block = h.samples().size() == 601;
  for (const history_sample& p : h.samples()) same_block = same_block && &p >= ring && &p < ring + 601;
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
  for (int i = 0; i < 601; ++i) h.push(sample_at(wide, kNow + i * 1000, 40, 1, 1));
  h.push(sample_at(narrow, kNow + 601000, 40, 1, 1));
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
  snapshot old_line = lanes;
  old_line.wsl_json.insert(old_line.wsl_json.size() - 1, R"(,"cpu_split":{"user":99,"sys":1,"iowait":3})");
  old_line.wsl_age_s = 60;
  h = history();
  h.push(old_line);
  check(std::isnan(h.samples().back().wsl_iowait), "a stale line paints no iowait");

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

// ---- Colour and time buckets ---------------------------------------------------------------------

void check_vt_colour() {
  check(ramp256(kNaN) == -1 && ramp256(kInf) == -1, "nothing measured has no colour");
  const struct { double pct; int colour; } steps[] = {{0, 236}, {4.99, 236}, {5, 60}, {29.99, 60}, {30, 67}, {59.99, 67}, {60, 110}, {89.99, 110}, {90, 179}, {100, 179}, {1000, 179}};
  for (const auto& s : steps) check(ramp256(s.pct) == s.colour, "ramp at " + std::to_string(s.pct));

  // A change of tone inside one row reaches the terminal: amber must not take the colour of what is beside it.
  const std::string toned = vt_frame({{{"a", tone::text}, {"b", tone::watch}, {"c", tone::text}}}, 1);
  check(has(toned, "212;184;96"), "amber beside plain text keeps its colour");
  check(toned.find("200;200;200", toned.find("212;184;96")) != std::string::npos, "and the next span gets its own back");

  // clip() keeps the colours, on a whole span and on a cut one.
  const line painted = {{"▀▀▀▀", tone::text, false, 179, 60}};
  check(clip(painted, 80)[0].fg256 == 179 && clip(painted, 80)[0].bg256 == 60, "clip keeps a span's colours");
  check(clip(painted, 2)[0].fg256 == 179 && clip(painted, 2)[0].bg256 == 60, "clip keeps them on a cut span");

  const std::string one = vt_frame({{{"▀", tone::text, false, 179, 60}}}, 1);
  check(has(one, "\x1b[0;38;5;179;48;5;60m▀"), "both colours are emitted");
  check(!has(vt_frame({{{"▀", tone::text, false, 179}}}, 1), "48;5"), "no background, none emitted");
  check(has(vt_frame({{{"x", tone::text, false, -1, 60}}}, 1), "48;5;60m"), "a background alone keeps the tone for its text");
  check(has(vt_frame({{{"x", tone::watch, true}}}, 1), "\x1b[0;1;38;2;212;184;96m"), "a tone is what it was");

  // Equal neighbours say their colour once. Different ones say only what changed.
  const auto sgr = [](const std::string& vt) {
    int n = 0;
    for (size_t i = vt.find("\x1b["); i != std::string::npos; i = vt.find("\x1b[", i + 1)) {
      size_t e = i + 2;
      while (vt[e] == ';' || (vt[e] >= '0' && vt[e] <= '9')) ++e;
      n += vt[e] == 'm' ? 1 : 0;
    }
    return n;
  };
  const span a{"▀", tone::text, false, 60, 67}, b{"▀", tone::text, false, 110, 67}, c{"▄", tone::text, false, 60, 67}, plain{" ", tone::text, false};
  const int base = sgr(vt_frame({{a}}, 1));
  check(sgr(vt_frame({{a, a, a, a}}, 1)) == base, "equal neighbours are one colour change");
  check(sgr(vt_frame({{a, c}}, 1)) == base, "a different glyph in the same colours is not a change");
  check(sgr(vt_frame({{a, b, a}}, 1)) == base + 2, "a different cell is");
  check(has(vt_frame({{a, b}}, 1), "\x1b[38;5;110m"), "only what changed: the foreground");
  check(has(vt_frame({{a, {"▀", tone::text, false, 60, 110}}}, 1), "\x1b[48;5;110m"), "only what changed: the background");
  check(has(vt_frame({{a, plain, a}}, 1), "\x1b[49m "), "a blank cell after a coloured one clears the background: blank is the terminal's");
  const std::string after_tone = vt_frame({{{"x", tone::watch, true}, a}}, 1);
  check(has(after_tone, "\x1b[0;38;5;60;48;5;67m"), "leaving bold starts from a reset");
  check(has(vt_frame({{a, {"x", tone::dim, false}}}, 1), "\x1b[38;2;122;122;122;49m"), "leaving a colour for a tone clears the background");
  // A blank on the same background needs no foreground.
  check(sgr(vt_frame({{{"x", tone::dim, false}, plain, {"y", tone::dim, false}}}, 1)) == sgr(vt_frame({{{"x", tone::dim, false}}}, 1)), "spaces do not change the pen");
  // Rows do not leak: every row starts from a reset, and nothing merges across one.
  check(sgr(vt_frame({{a}, {a}}, 2)) == base + 2, "a row starts over: its own colour and its own reset");
  check(vt_clean(vt_frame({{a, b, plain, c}}, 1)), "256-colour sequences are our own");
}

// Sample at a time on one figure: the guest's busy, so a NaN can be a stale line. times are ms before kNow.
history bucket_history(const std::vector<std::pair<int64_t, double>>& points) {
  history h;
  snapshot s = busy();
  int seq = 0;
  for (const auto& [ago, v] : points) {
    s.unix_ms = kNow - ago;
    s.cpu.core_pct.assign(20, std::isnan(v) ? 1.0 : v);
    snapshot t = sample_at(s, s.unix_ms, 40, 1, 1, 1 + seq++ % 7);
    t.cpu.busy_pct = v;  // sample_at sets its own
    h.push(t);
  }
  return h;
}

double windows_busy(const history_sample& p) { return p.windows_cpu; }

void check_buckets() {
  // floor(t / bucket): a sample's bucket never depends on when the frame was drawn.
  history h = bucket_history({{0, 50}});
  time_axis a = make_axis(h, kNow, 60000, 60);
  check(a.bucket_ms == 1000, "60 s over 60 cells is 1 s a cell");
  const std::vector<double> first = bucket_cells(a, windows_busy);
  check(first[59] == 50 && known_columns(first) == 1, "a sample at the end is the right edge");
  for (int late = 0; late < 3000; late += 250) {
    const time_axis moved = make_axis(h, kNow + late, 60000, 60);
    const int64_t bucket = kNow / 1000, edge = (kNow + late) / 1000;
    const std::vector<double> cells = bucket_cells(moved, windows_busy);
    check(cells[static_cast<size_t>(59 - (edge - bucket))] == 50, "a sample stays in its bucket, the block moves a whole cell");
  }
  a = make_axis(h, kNow, 60000, 43);
  check(a.bucket_ms == 1396 && a.cells == 43, "a cell of 60 s over 43 is 1.4 s, in whole ms, and the cells cover the span");
  check(make_axis(h, kNow, 900000, 43).bucket_ms == 20931, "the span is an argument");
  check(make_axis(history(), 0, 60000, 43).samples.empty(), "no history, no samples");
  check(make_axis(h, kNow, 60000, 0).cells == 0 && make_axis(h, kNow, 60000, -3).cells == 0, "no cells");
  check(floor_div(-1, 1000) == -1 && floor_div(-1000, 1000) == -1 && floor_div(999, 1000) == 0, "floor, also below zero");

  // Peak and mean of one bucket: these four share the 4 s bucket starting at bucket_start.
  const int64_t bucket_start = kNow - kNow % 4000, into = kNow - bucket_start;
  h = bucket_history({{into, 10}, {into - 1000, 90}, {into - 2000, kNaN}, {into - 3000, 20}});
  a = make_axis(h, bucket_start + 3000, 60000, 15);
  check(a.bucket_ms == 4000, "4 s cells");
  const size_t edge = 14;
  check(bucket_cells(a, windows_busy)[edge] == 40, "a cell is the mean, and NaN is not in it");
  const window_stats st = bucket_stats(a, windows_busy);
  check(st.max == 90 && st.max_at == static_cast<int>(edge) && st.max_count == 3 && std::fabs(st.avg - 40) < 1e-9, "the window's maximum is its highest sample, in its cell, and NaN is not counted");

  // Over the scale: clamped in the cell, as bars are.
  h = bucket_history({{0, 300}});
  check(bucket_cells(make_axis(h, kNow, 60000, 60), windows_busy)[59] == 100, "over 100 clamps in a cell");

  // Cells narrower than a second: what is between two close samples takes the earlier one.
  h = bucket_history({{1000, 70}, {0, 30}});
  a = make_axis(h, kNow, 60000, 240);
  check(a.bucket_ms == 250, "4 cells a second");
  std::vector<double> cells = bucket_cells(a, windows_busy);
  check(cells[239] == 30 && cells[238] == 70 && cells[236] == 70 && cells[235] == 70 && std::isnan(cells[234]), "a wide terminal does not stripe");
  h = bucket_history({{1500, 70}, {0, 30}});
  check(known_columns(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)) == 7, "samples kGapMs apart join");
  h = bucket_history({{1501, 70}, {0, 30}});
  check(known_columns(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)) == 2, "samples further apart do not");
  h = bucket_history({{2000, 70}, {1000, kNaN}, {0, 30}});
  check(known_columns(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)) == 2, "a sample that measured nothing breaks the join");

  // A gap as wide as a cell shows; a narrower one is inside a cell.
  h = bucket_history({{40000, 70}, {39000, 70}, {3000, 30}, {2000, 30}});
  cells = bucket_cells(make_axis(h, kNow, 60000, 60), windows_busy);
  check(std::isnan(cells[40]) && !std::isnan(cells[59 - 3]) && !std::isnan(cells[59 - 40]), "a stall is blank cells, never 0");
  h = bucket_history({{20000, 70}, {16000, 70}});
  check(known_columns(bucket_cells(make_axis(h, kNow, 900000, 45), windows_busy)) == 1, "a gap inside a 20 s cell is not visible");

  // The left edge: the sample before it joins, and nothing is drawn from it.
  h = bucket_history({{60500, 70}, {59500, 20}});
  cells = bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy);
  check(cells[0] == 70 && cells[1] == 20 && known_columns(cells) == 2, "the left edge starts from the sample before it");
  h = bucket_history({{61500, 10}, {60500, 70}, {59500, 20}});
  check(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)[0] == 70, "the newest sample before it, not the oldest");
  h = bucket_history({{120000, 70}, {119000, 70}, {1000, 30}});
  a = make_axis(h, kNow, 60000, 60);
  check(a.samples.size() == 2 && a.cell[0] < 0, "one sample before the left edge is kept, no more");
  check(known_columns(bucket_cells(a, windows_busy)) == 1, "and a far one does not reach the axis");
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
    for (const auto& [w, h2] : {std::pair{99, 50}, std::pair{80, 24}}) {
      const history demo = demo_minute(busy());
      const std::string f = as_text(render(busy(), w, h2, kNow, &demo, v));
      const std::string n = " " + std::to_string(w) + "x" + std::to_string(h2);
      check(has(f, "100│") && has(f, "  0│") && has(f, "-60s") && has(f, " now"), "axes are labelled" + n);
      check(count_braille(graph_rows(f)) > 0, "a minute draws" + n);
    }
  }
}

struct cell {
  std::string text;
  tone color = tone::text;
};

// Each plot's cells in one row, left to right, with their tones. A plot starts after a faint "│" drawn
// right after a dim gutter label, so "  0│", the large layout's "  0 │" and "    │" all count, and ends at
// anything not in the bar or faint tone, or at the large layout's " │ " between its two columns.
std::vector<std::vector<cell>> plots_in(const line& l) {
  std::vector<cell> cps;
  for (const span& s : l) {
    for (size_t i = 0; i < s.text.size();) {
      const size_t from = i;
      next_cp(s.text, i);
      cps.push_back({s.text.substr(from, i - from), s.color});
    }
  }
  std::vector<std::vector<cell>> out;
  bool in = false;
  for (size_t k = 0; k < cps.size(); ++k) {
    const cell& c = cps[k];
    const bool bar_line = c.text == "│" && c.color == tone::faint;
    if (bar_line && k > 0 && cps[k - 1].color == tone::dim) { in = true; out.emplace_back(); continue; }
    if (!in) continue;
    if (bar_line || (c.color != tone::bar && c.color != tone::faint) || (c.text == " " && k + 1 < cps.size() && cps[k + 1].text == "│")) {
      in = false;
      continue;
    }
    out.back().push_back(c);
  }
  return out;
}

// The bottom plot row of the nth graph in a frame, counting graphs left to right and then down, cell by
// cell with its tone. Empty when there is no nth graph.
std::vector<cell> bottom_cells(const std::vector<line>& frame, int nth) {
  int seen = 0;
  for (const line& l : frame) {
    const std::string t = as_text({l});
    if (t.rfind("  0│", 0) != 0 && t.rfind("  0 │", 0) != 0) continue;
    for (std::vector<cell>& p : plots_in(l)) {
      if (seen++ == nth) return std::move(p);
    }
  }
  return {};
}

// The rows of a frame's graphs above the bottom one.
std::string upper_graph_rows(const std::string& t) {
  std::string out;
  for (size_t from = 0; from < t.size();) {
    const size_t nl = std::min(t.find('\n', from), t.size());
    const std::string l = t.substr(from, nl - from);
    if (l.rfind("100│", 0) == 0 || l.rfind("   │", 0) == 0) out += l + "\n";
    from = nl + 1;
  }
  return out;
}

// Every graph of both panels at value v, the WSL line new each time so it is never a repeat.
snapshot rule_sample(int64_t t, double v, int k) {
  snapshot s = sample_at(busy(), t, v, v, v, v);
  s.cpu.busy_pct = v;
  s.gpu_device.mem_used = static_cast<uint64_t>(static_cast<double>(s.gpu_device.mem_total) * v / 100);
  s.gpu_device.power_w = s.gpu_device.power_limit_w * v / 100;
  const size_t at = s.wsl_json.find("\"mono_s\":");
  s.wsl_json.replace(at, s.wsl_json.find(',', at) - at, "\"mono_s\":" + std::to_string(812 + k));
  return s;
}

// The graph-cell rule shared with sshvllmtop and machtop: a measured 0 is ⣀ in the bar tone, a sample
// missing inside the watched span is blank, and the part of the minute from before watching began is a
// faint · in every other cell, on the bottom row only.
void check_graph_rule() {
  const auto is_dot = [](const cell& c) { return c.text == "·" && c.color == tone::faint; };
  const auto dots_in = [&](const std::vector<cell>& cs) { return static_cast<int>(std::count_if(cs.begin(), cs.end(), is_dot)); };
  const struct { view v; int graphs; const char* name; } panels[] = {{view::gpu, 5, " gpu"}, {view::cpu, 2, " cpu"}};

  history fresh, zero, gap, empty;
  for (int age = 9; age >= 0; --age) fresh.push(rule_sample(kNow - age * 1000, 50, age));
  for (int age = 60; age >= 0; --age) zero.push(rule_sample(kNow - age * 1000, 0, age));
  for (int age = 60; age >= 0; --age) if (age > 40 || age < 20) gap.push(rule_sample(kNow - age * 1000, 0, age));
  check(fresh.watched_from_ms() == kNow - 9000, "watching began at the first sample");

  for (const auto& p : panels) {
    for (const auto& [w, h] : {std::pair{131, 40}, std::pair{80, 24}, std::pair{200, 60}}) {
      const std::string where = std::string(p.name) + " " + std::to_string(w) + "x" + std::to_string(h);
      const snapshot s = rule_sample(kNow, 50, 99);

      // Not watched yet: a fresh start, ten seconds of samples on the right.
      const std::vector<line> ff = render(s, w, h, kNow, &fresh, p.v);
      check(!has(upper_graph_rows(as_text(ff)), "·"), "the unwatched dots are on the bottom row only" + where);
      for (int g = 0; g < p.graphs; ++g) {
        const std::vector<cell> cs = bottom_cells(ff, g);
        const std::string n = where + " graph " + std::to_string(g);
        const int cells = static_cast<int>(cs.size());
        int first = 0;  // The first drawn cell
        while (first < cells && (cs[static_cast<size_t>(first)].text == " " || is_dot(cs[static_cast<size_t>(first)]))) ++first;
        check(cells > 9 && first < cells, "a fresh graph draws its first samples" + n);
        check(std::abs(first - cells * 51 / 60) <= 1, "the unwatched part is the time before the first sample" + n);
        bool alternate = true;
        for (int i = 0; i < first; ++i) {
          const cell& c = cs[static_cast<size_t>(i)];
          alternate = alternate && (i % 2 == 0 ? is_dot(c) : c.text == " ");
        }
        check(alternate, "a faint · in every other cell before watching began" + n);
        check(dots_in({cs.begin() + first, cs.end()}) == 0, "no · once watching began" + n);
        bool drawn = true;
        for (int i = first; i < cells; ++i) drawn = drawn && cs[static_cast<size_t>(i)].color == tone::bar && cs[static_cast<size_t>(i)].text != " ";
        check(drawn, "the watched part is drawn in the bar tone" + n);
      }

      // Measured 0, a whole minute of it: ⣀ in every cell, in the bar tone, and nothing above.
      const std::vector<line> zf = render(s, w, h, kNow, &zero, p.v);
      check(count_braille(upper_graph_rows(as_text(zf))) == 0, "a measured 0 is the bottom row only" + where);
      for (int g = 0; g < p.graphs; ++g) {
        const std::vector<cell> cs = bottom_cells(zf, g);
        bool line_of_zero = !cs.empty();
        for (const cell& c : cs) line_of_zero = line_of_zero && c.text == "⣀" && c.color == tone::bar;
        check(line_of_zero, "a measured 0 is ⣀ in the bar tone" + where + " graph " + std::to_string(g));
      }

      // A gap: samples 41 s and 19 s ago and none between, inside the watched minute: blank, not dotted
      // and not ⣀.
      const std::vector<line> gf = render(s, w, h, kNow, &gap, p.v);
      for (int g = 0; g < p.graphs; ++g) {
        const std::vector<cell> cs = bottom_cells(gf, g);
        const std::string n = where + " graph " + std::to_string(g);
        int run = 0, longest = 0;
        for (const cell& c : cs) {
          run = c.text == " " ? run + 1 : 0;
          longest = std::max(longest, run);
        }
        const int cells = static_cast<int>(cs.size());
        check(dots_in(cs) == 0, "a gap is not dotted" + n);
        check(longest >= cells * 20 / 60 - 1 && longest <= cells * 22 / 60 + 1, "a gap is blank for as long as it lasted" + n);
        check(!cs.empty() && cs.front().text == "⣀" && cs.back().text == "⣀", "around the gap, the measured 0s" + n);
      }

      // No sample yet: the whole minute is unwatched.
      const std::vector<line> ef = render(s, w, h, kNow, &empty, p.v);
      for (int g = 0; g < p.graphs; ++g) {
        const std::vector<cell> cs = bottom_cells(ef, g);
        check(!cs.empty() && dots_in(cs) == (static_cast<int>(cs.size()) + 1) / 2, "before the first sample every other cell is ·" + where);
      }
    }
  }

  // A minute watched with nothing measured is a gap all along: blank, no dots.
  const history watched_bare = demo_minute(bare());
  const std::string bare_gpu = graph_rows(as_text(render(bare(), 131, 40, kNow, &watched_bare, view::gpu)));
  check(!has(bare_gpu, "·") && count_braille(bare_gpu) == 0, "watched with nothing measured is blank, not dotted");
  const std::string bare_wsl = panel_cells(graph_rows(as_text(render(bare(), 131, 40, kNow, &watched_bare, view::cpu))), 1);
  check(!has(bare_wsl, "·") && count_braille(bare_wsl) == 0, "a watched minute with no WSL line is blank, not dotted");

  // When watching began, through the history's own changes.
  constexpr int kCells = 38;
  check(unwatched_cells(empty, 0, kCells) == kCells, "no sample: every cell unwatched");
  history one;
  one.push(rule_sample(kNow, 0, 0));
  check(unwatched_cells(one, kNow, kCells) == kCells - 1, "one sample now: all but its cell unwatched");
  check(unwatched_cells(one, kNow + 60000, kCells) == 0, "a minute later nothing is unwatched");
  history fast;
  for (int i = 0; i < 5000; ++i) fast.push(rule_sample(kNow + i, 0, i));
  check(fast.samples().front().unix_ms > kNow && fast.watched_from_ms() == kNow, "watching began at the first sample, after the ring drops it");
  history stepped;
  for (int age = 10; age >= 0; --age) stepped.push(rule_sample(kNow - age * 1000, 0, age));
  stepped.push(rule_sample(kNow - 3600000, 0, 11));
  check(stepped.watched_from_ms() == stepped.samples().front().unix_ms, "a backward step moves when watching began with the samples");
  // A stall of 120 s: under the 10 minutes kept, so the samples stay, placed 120 s back, for the longer
  // span; on the minute's graph it is a gap.
  snapshot stall = rule_sample(kNow - 7200000, 0, 12);
  stall.interval_s = 120;
  stepped.push(stall);
  const sample_range kept = stepped.samples();
  check(kept.size() == 13 && kept[11].unix_ms == kNow - 7200000 - 120000 && stepped.watched_from_ms() == kept.front().unix_ms,
        "a stall under what is kept keeps the samples, shifted with when watching began");
  check(unwatched_cells(stepped, kNow - 7200000, kCells) == 0, "a stall longer than the span is a gap, not unwatched");
  check(dots_in(bottom_cells(render(stall, 131, 40, kNow - 7200000, &stepped, view::gpu), 0)) == 0, "after a long stall no dots");
  // A stall of all that is kept clears the samples, and every span before it is watched with nothing measured.
  snapshot clear = rule_sample(kNow - 9000000, 0, 13);
  clear.interval_s = static_cast<double>(history::kKeepMs) / 1000.0;
  stepped.push(clear);
  check(stepped.samples().size() == 1 && stepped.watched_from_ms() <= kNow - 9000000 - history::kKeepMs && unwatched_cells(stepped, kNow - 9000000, kCells) == 0,
        "a stall that clears the history is a gap, not unwatched");
  check(dots_in(bottom_cells(render(clear, 131, 40, kNow - 9000000, &stepped, view::gpu), 0)) == 0, "after a stall of what is kept no dots");
  history forward;
  forward.push(rule_sample(kNow, 0, 0));
  forward.push(rule_sample(kNow + 600000, 0, 1));
  check(unwatched_cells(forward, kNow + 600000, kCells) == 0, "a forward step is a gap, not unwatched");

  // A stall of 120 s during which the wall clock stepped back by 100 s: t is still after the last
  // sample, but watching has lasted at least 130 s, so the whole graph is a gap. The wall clock moved
  // 100 s less than interval_s, so the samples move back that far and keep their place for the longer span.
  history partial_step;
  for (int age = 9; age >= 0; --age) partial_step.push(rule_sample(kNow - age * 1000, 0, age));
  snapshot late = rule_sample(kNow + 20000, 0, 10);
  late.interval_s = 120;
  partial_step.push(late);
  check(unwatched_cells(partial_step, kNow + 20000, kCells) == 0 && partial_step.samples().size() == 11 &&
            partial_step.samples()[9].unix_ms == kNow + 20000 - 120000 && partial_step.watched_from_ms() == kNow - 9000 - 100000,
        "a stall with a partial backward step is a gap, not unwatched");
  for (const auto& p : panels) {
    const std::vector<line> f = render(late, 131, 40, kNow + 20000, &partial_step, p.v);
    for (int g = 0; g < p.graphs; ++g) check(dots_in(bottom_cells(f, g)) == 0, std::string("a stall with a partial backward step has no dots") + p.name);
  }

  // Samples whose time cannot be placed are a gap: the program was watching.
  history rejected;
  snapshot unplaced = rule_sample(kNow, 0, 0);
  unplaced.unix_ms = 0;
  rejected.push(unplaced);
  rejected.push(unplaced);
  check(rejected.samples().empty() && rejected.watching() && unwatched_cells(rejected, 0, kCells) == 0, "a sample that cannot be placed is a gap, not unwatched");
  for (const auto& p : panels) {
    const std::vector<line> f = render(rule_sample(kNow, 0, 0), 131, 40, kNow, &rejected, p.v);
    check(dots_in(bottom_cells(f, 0)) == 0 && !bottom_cells(f, 0).empty(), std::string("after unplaceable samples no dots") + p.name);
  }
  rejected.push(rule_sample(kNow, 0, 1));
  check(unwatched_cells(rejected, kNow, kCells) == 0, "after unplaceable samples, the time before the first placed one is a gap");
  for (const auto& p : panels) {
    const std::vector<line> f = render(rule_sample(kNow, 0, 1), 131, 40, kNow, &rejected, p.v);
    check(!bottom_cells(f, 0).empty() && dots_in(bottom_cells(f, 0)) == 0, std::string("the first placed sample after unplaceable ones has no dots before it") + p.name);
  }

  // Now ahead of the newest sample, as when a key redraws between samples, and a series whose first
  // samples measured nothing (the WSL CPU before the collector's first line, the device before NVML
  // answers). The dots stop where the first sample is, as the series measured from the start shows it,
  // and the unmeasured start is a blank gap. The sub-second offsets move the boundary across cells.
  const struct { view v; int whole, partial; const char* name; } pairs[] = {{view::gpu, 1, 0, " device"}, {view::cpu, 0, 1, " WSL CPU"}};
  for (const int ahead : {1000, 3000, 5000}) {
    for (int frac = 0; frac < 1000; frac += 125) {
      history h;
      for (int k = 0; k < 10; ++k) {
        snapshot s = rule_sample(kNow - ahead - (9 - k) * 1000 - frac, 50, k);
        if (k < 4) { s.wsl_json.clear(); s.gpu_device.has_util = false; }
        h.push(s);
      }
      for (const auto& p : pairs) {
        const std::vector<line> f = render(rule_sample(kNow, 50, 99), 131, 40, kNow, &h, p.v);
        const std::vector<cell> whole = bottom_cells(f, p.whole), part = bottom_cells(f, p.partial);
        const std::string n = std::string(p.name) + " " + std::to_string(ahead) + " ms ahead +" + std::to_string(frac);
        size_t first = 0;
        while (first < whole.size() && (whole[first].text == " " || is_dot(whole[first]))) ++first;
        bool same = whole.size() == part.size() && first > 0 && first < part.size();
        for (size_t i = 0; same && i < first; ++i) same = (i % 2 == 0 ? is_dot(part[i]) && is_dot(whole[i]) : part[i].text == " " && whole[i].text == " ");
        check(same, "dots up to the first sample, measured or not" + n);
        check(first < part.size() && part[first].text == " " && dots_in({part.begin() + static_cast<long>(std::min(first, part.size())), part.end()}) == 0,
              "an unmeasured start is a blank gap, not dotted" + n);
      }
    }
  }

  // Only blank cells take a dot; a drawn cell keeps its braille.
  const line pl = plot_line("⣀ ⣀  ", 5);
  check(as_text({pl}) == "⣀ ⣀ ·\n", "a drawn cell is never covered by a dot");
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

  // The CPU panel's small layout, which is every window under 100 columns.
  for (const int w : {80, 90, 99}) {
    const std::string t = as_text(render(full, w, 60, kNow, &demo, view::cpu));
    const std::string where = " at " + std::to_string(w);
    check(has(t, "whole machine, includes the WSL VM") && has(t, "guest view, inside the VM"), "CPU graphs name their source" + where);
    check(has(t, "hottest core 3 at 96%") && has(t, "hottest core 6 at 97%"), "both sets of core rows" + where);
    check(!has(t, "62%") && !has(t, "61.6"), "Windows and WSL busy are not added" + where);
    check(has(t, "top ") && has(t, "ollama") && has(t, "svchost.exe"), "both process lists" + where);
  }
  // The title rows themselves: the WINDOWS and WSL rule rows hold the same words.
  for (const int w : {131, 200}) {  // 26 rows are under the large layout's floor, at any width
    const auto f = render(full, w, 26, kNow, &demo, view::cpu);
    const std::string title = line_with(f, "%  Windows CPU");
    check(has(title, "Windows CPU · whole machine, includes the WSL VM"), "the Windows graph title names its source");
    check(has(title, "WSL CPU · guest view, inside the VM"), "the WSL graph title names its source");
    check(has(as_text(f), "two systems' own views of overlapping work, never added"), "the CPU rule says the views overlap");
  }
  const auto wrapped = render(full, 80, 60, kNow, &demo, view::cpu);
  const std::string second = line_with(wrapped, "%  Windows CPU", 1);
  check(has(second, "includes the WSL VM") && has(second, "guest view, inside the VM"), "wrapped titles keep their source");
  const auto cpu = render(full, 131, 26, kNow, &demo, view::cpu);
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

  const std::string bc = as_text(render(bare(), 99, 50, kNow, &demo_bare, view::cpu));
  check(has(bc, "n/a  WSL CPU") && has(bc, "WSL data unavailable"), "no WSL line is n/a");
  check(!has(bc, "  0%  WSL"), "no WSL line is not 0");
  snapshot no_cpu = bare();
  no_cpu.cpu = {};
  check(has(as_text(render(no_cpu, 99, 50, kNow, &demo_bare, view::cpu)), "n/a  Windows CPU"), "a failed CPU read is n/a");
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
  check(has(graph_rows(eg), "· · ·"), "an empty history is not watched yet: dotted, not a gap");

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
        if (head.empty() || has(t, "vCPUs, not Windows cores")) continue;  // No room for the list; the large layout has its own check
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

// ---- Fixtures for the large layout of panel 3 ------------------------------------------------------
//
// contended is section 3 of docs/design/cpu-panel.md, from both-win-20. Each sample of a history has a new
// guest line, as a healthy collector gives: a line that does not change is a repeat.

struct guest_figures {
  double busy = 100, user = 99, sys = 1, iowait = 0, load = 19.8;
  double used_gib = 12.3;
  int seq = 0;
};

constexpr const char* kCpuProcesses =
    R"({"pid":4121,"name":"cpu_burn","state":"R","rss":107374182,"cores":19.72,"threads":21,"on_gpu":false},)"
    R"({"pid":4388,"name":"python3","state":"S","rss":2147483648,"cores":0.1,"threads":12,"on_gpu":true},)"
    R"({"pid":2000,"name":"node","state":"S","rss":429496729,"cores":0.04,"threads":11,"on_gpu":false},)"
    R"({"pid":2001,"name":"sshd","state":"S","rss":10000000,"cores":0.03,"threads":1,"on_gpu":false},)"
    R"({"pid":2002,"name":"systemd","state":"S","rss":10000000,"cores":0.02,"threads":1,"on_gpu":false},)"
    R"({"pid":2003,"name":"dbus-daemon","state":"S","rss":10000000,"cores":0.01,"threads":1,"on_gpu":false})";

std::string guest_line(const std::vector<double>& cores, const guest_figures& g, const std::string& processes = kCpuProcesses) {
  std::string list;
  for (size_t i = 0; i < cores.size(); ++i) list += std::to_string(cores[i]) + (i + 1 < cores.size() ? "," : "");
  const uint64_t total = static_cast<uint64_t>(31.2 * static_cast<double>(kG));
  const uint64_t available = total - static_cast<uint64_t>(g.used_gib * static_cast<double>(kG));
  return "{\"schema\":1,\"distro\":\"Ubuntu-24.04\",\"mono_s\":" + std::to_string(g.seq) + ",\"busy_pct\":" + std::to_string(g.busy) + ",\"core_pct\":[" + list +
         "],\"cpu_split\":{\"user\":" + std::to_string(g.user) + ",\"sys\":" + std::to_string(g.sys) + ",\"iowait\":" + std::to_string(g.iowait) +
         "},\"load\":[" + std::to_string(g.load) + ",1,1],\"steal_pct\":0,\"memory\":{\"total\":" + std::to_string(total) + ",\"available\":" + std::to_string(available) +
         ",\"cached\":0,\"swap_used\":0},\"pressure\":{\"cpu\":4,\"memory\":0,\"io\":0},\"process_count\":64,\"processes\":[" + processes + "],\"dxg_pids\":[4388],\"dxg_denied\":0}";
}

// docs/measurable.md, "CPU layout": 6 P-cores of two threads, then 8 E-cores of one.
cpu_layout_view layout_13600k() {
  cpu_layout_view l;
  l.known = true;
  for (uint32_t c = 0; c < 6; ++c) {
    l.rows.push_back({c, 0, 1});
    l.rows.push_back({c, 1, 1});
  }
  for (uint32_t c = 6; c < 14; ++c) l.rows.push_back({c, 0, 0});
  return l;
}

snapshot contended(int threads = 20, int vcpus = 0) {
  if (vcpus == 0) vcpus = threads;
  snapshot s;
  s.unix_ms = kNow - 400;
  s.interval_s = 1.0;
  s.cpu.core_pct.assign(static_cast<size_t>(threads), 100.0);
  s.cpu.busy_pct = 100;
  s.cpu.user_pct = 65;
  s.cpu.kernel_pct = 35;
  if (threads == 20) s.cpu_layout = layout_13600k();
  s.memory.total = static_cast<uint64_t>(63.7 * static_cast<double>(kG));
  s.memory.available = static_cast<uint64_t>(22.5 * static_cast<double>(kG));
  s.vm = {7120, 11736, static_cast<uint64_t>(10.1 * static_cast<double>(kG)), 5.76, true};
  s.ready_threads = 41;
  s.vm_ready_threads = 13;
  s.cpu_derived = {28.8, 6.2, 41.2, 13.1};
  s.lost = {lost_state::known, 13.96};
  const auto proc = [&](uint32_t pid, const char* name, double cores, uint32_t thr, double ws) {
    process_row p;
    p.pid = pid; p.name = name; p.cores = cores; p.threads = thr; p.working_set = static_cast<uint64_t>(ws * static_cast<double>(kG));
    s.processes.push_back(p);
  };
  proc(0, "Idle", 0, 20, 0);
  proc(31000, "cpu_burn.exe", 13.25, 21, 0.1);
  proc(7120, "vmmemWSL", 5.76, 22, 10.1);
  proc(4, "System", 0.42, 310, 0.04);
  proc(11736, "vmwp.exe", 0.31, 40, 0.1);
  proc(9001, "카카오톡.exe", 0.22, 60, 0.4);
  proc(10000, "svchost.exe", 0.20, 12, 0.04);
  for (uint32_t i = 0; i < 40; ++i) proc(20000 + i * 4, "svchost.exe", 0.01, 8, 0.01);
  s.wsl_json = guest_line(std::vector<double>(static_cast<size_t>(vcpus), 100.0), {});
  s.wsl_age_s = 0.2;
  return s;
}

// The same machine at rest, for the part of a history before a load, and for the idle fixture.
void quiet_moment(snapshot& s, int seq) {
  const size_t threads = std::max<size_t>(s.cpu.core_pct.size(), 1), vcpus = read_wsl(s.wsl_json).core_pct.size();
  s.cpu.core_pct.assign(threads, 3.0);
  s.cpu.busy_pct = 4;
  s.cpu.user_pct = 3;
  s.cpu.kernel_pct = 1;
  s.cpu_derived.vm_cpu_pct = 0.3;
  s.cpu_derived.own_kernel_pct = 0.7;
  s.cpu_derived.ready_threads_mean = 2;
  s.cpu_derived.vm_ready_threads_mean = 0;
  s.ready_threads = 2;
  s.vm_ready_threads = 0;
  s.vm.vmmem_cores = 0.06;
  s.lost = {lost_state::guest_under_99, kNaN};
  guest_figures g;
  g.busy = 3;
  g.user = 2;
  g.iowait = 0.5;
  g.load = 2.1;
  g.seq = seq;
  s.wsl_json = guest_line(std::vector<double>(vcpus, 2.0), g);
}

// Samples up to the newest, each changed by moment(snapshot, seconds ago, sample number).
template <class F> history build_history(const snapshot& base, int seconds, F moment) {
  history h;
  for (int age = seconds; age >= 0; --age) {
    snapshot s = base;
    s.unix_ms = base.unix_ms - age * 1000;
    moment(s, age, seconds - age + 1);
    h.push(s);
  }
  return h;
}

// One onset, 42% of the way along, as section 3 draws it.
history contended_history(const snapshot& base, int seconds = 60) {
  return build_history(base, seconds, [&](snapshot& s, int age, int seq) {
    if (age * 100 > seconds * 58) {
      quiet_moment(s, seq);
      return;
    }
    guest_figures g;
    g.seq = seq;
    s.wsl_json = guest_line(read_wsl(base.wsl_json).core_pct, g);
  });
}

snapshot idle_now(int threads = 20) {
  snapshot s = contended(threads);
  quiet_moment(s, 1000);
  return s;
}

history idle_history(const snapshot& base, int seconds = 60) {
  return build_history(base, seconds, [](snapshot& s, int, int seq) { quiet_moment(s, seq); });
}

// The collector went silent half a minute ago, an hour's worth of age on the newest line.
snapshot stale_now() {
  snapshot s = contended();
  s.wsl_age_s = 3600;
  s.lost = {lost_state::no_data, kNaN};
  return s;
}

history stale_history(const snapshot& base) {
  return build_history(base, 60, [&](snapshot& s, int age, int seq) {
    if (age < 30) {
      s.wsl_age_s = 3600;
      s.lost = {lost_state::no_data, kNaN};
    } else {
      guest_figures g;
      g.seq = seq;
      s.wsl_json = guest_line(read_wsl(base.wsl_json).core_pct, g);
      s.wsl_age_s = 0.2;
    }
  });
}

// The worst case for the bytes of a frame: every cell of every row a different colour from its neighbours.
uint32_t next_noise(uint32_t& state) {
  state = state * 1664525u + 1013904223u;  // A fixed sequence: the check must not move from run to run
  return state >> 16;
}

snapshot noisy_now(int threads = 20) { return contended(threads); }

history noisy_history(const snapshot& base, int seconds = 60) {
  uint32_t noise = 12345;
  const auto pct = [&] { return static_cast<double>(next_noise(noise) % 1000) / 10.0; };
  return build_history(base, seconds, [&](snapshot& s, int, int seq) {
    for (double& c : s.cpu.core_pct) c = pct();
    s.cpu.busy_pct = pct();
    s.cpu_derived.vm_cpu_pct = pct();
    s.cpu_derived.own_kernel_pct = pct();
    s.memory.available = static_cast<uint64_t>(pct() / 100.0 * static_cast<double>(s.memory.total));
    s.lost.vcpus = pct() / 5.0;
    std::vector<double> cores = read_wsl(base.wsl_json).core_pct;
    for (double& c : cores) c = pct();
    guest_figures g;
    g.seq = seq;
    g.busy = pct();
    g.iowait = pct();
    g.used_gib = pct() / 100.0 * 31.0;
    s.wsl_json = guest_line(cores, g);
  });
}

// A fixture by name, with the history that goes with it. seconds is how much of one to build.
bool cpu_fixture(const std::string& name, int seconds, snapshot& s, history& h) {
  if (name == "contended") { s = contended(); h = contended_history(s, seconds); }
  else if (name == "idle") { s = idle_now(); h = idle_history(s, seconds); }
  else if (name == "stale") { s = stale_now(); h = stale_history(s); }
  else if (name == "noisy") { s = noisy_now(); h = noisy_history(s, seconds); }
  else return false;
  return true;
}

render_options options_for(int span_s) {
  render_options o;
  o.span_ms = span_s * 1000;
  o.span_key = true;
  return o;
}

// ---- The large layout of panel 3, against docs/design/cpu-panel.md -----------------------------------

std::vector<line> large(const snapshot& s, const history& h, int w = 100, int rows = 34, const render_options& o = {}) {
  return render(s, w, rows, kNow, &h, view::cpu, o);
}

// Text of a row from column `from`, `cols` wide; wide characters count as they are drawn.
std::string columns(const std::string& text, int from, int cols = 1000) {
  std::string out;
  int at = 0;
  for (size_t i = 0; i < text.size() && text[i] != '\n';) {
    const char32_t c = next_cp(text, i);
    if (at >= from && at < from + cols) put_cp(out, c);
    at += cp_width(c);
  }
  return out;
}

struct painted { std::string glyph; int fg, bg; };

// One entry per column of a row, with its colours.
std::vector<painted> cells_of(const line& l) {
  std::vector<painted> out;
  for (const span& p : l) {
    for (size_t i = 0; i < p.text.size();) {
      const char32_t c = next_cp(p.text, i);
      std::string g;
      put_cp(g, c);
      for (int k = 0; k < cp_width(c); ++k) out.push_back({g, p.fg256, p.bg256});
    }
  }
  if (out.empty()) out.assign(300, {" ", -1, -1});  // A row that was not found: the check that looked for it says so
  return out;
}

int row_at(const std::vector<line>& f, int col, const std::string& start) {
  for (size_t i = 0; i < f.size(); ++i) {
    if (columns(as_text({f[i]}), col, display_width(start)) == start) return static_cast<int>(i);
  }
  return -1;
}

// That row, and a failed check rather than a bad index when it is not there.
const line& line_at(const std::vector<line>& f, int col, const std::string& start) {
  static const line kNone;
  const int i = row_at(f, col, start);
  check(i >= 0, "no row starts with " + start);
  return i < 0 ? kNone : f[static_cast<size_t>(i)];
}

int kRight(int width) { return (width - 3) / 2 + 3; }  // Column where the right side starts

bool is_large(const std::vector<line>& f) { return has(as_text(f), "vCPUs, not Windows cores"); }

bool frame_fits(const std::vector<line>& f, int w, int rows) {
  bool fits = static_cast<int>(f.size()) <= rows && !f.empty();
  for (const line& l : f) {
    int cols = 0;
    for (const span& p : l) cols += display_width(p.text);
    fits = fits && cols <= w;
  }
  return fits;
}

void check_large_mockup() {
  const snapshot s = contended();
  const history h = contended_history(s);
  const auto f = large(s, h);
  const std::string text = as_text(f);
  check(f.size() == 34 && frame_fits(f, 100, 34), "100 by 34 is 34 rows, none over 100");

  // Section 3's rows, character for character where the design gives the characters.
  const auto row_is = [&](size_t i, const std::string& want) {
    std::string got = as_text({f[i]});
    got.pop_back();
    while (!got.empty() && got.back() == ' ') got.pop_back();
    check(got == want, "row " + std::to_string(i) + ": " + got);
  };
  check(has(as_text({f[0]}), "winwsltop") && has(as_text({f[0]}), "data 0.4 s old  every 1.0 s"), "the title gives the age and the interval");
  check(has(as_text({f[1]}), "two systems' own views of overlapping work, never added"), "the rule says the views overlap");
  row_is(2, "100%  WINDOWS  user 65 · kernel 6 · VM 29        │ 100%  WSL  user 99 · sys 1 · iowait 0, as idle");
  row_is(3, "      whole machine, includes the WSL VM         │       guest view, inside the VM · Ubuntu-24.04");
  row_is(4, "VM ran 5.76 of 20 threads · ≈41 ready, 13 VM     │ WSL short ≈14 of 20 vCPUs, inferred · load 19.8");
  row_is(5, "RAM 41.2/63.7 GiB  free 22.5 GiB                 │ RAM 12.3/31.2 GiB · pressure cpu 4 mem 0 io 0");
  check(columns(as_text({f[6]}), 0, 5) == "100 │" && columns(as_text({f[6]}), 51, 5) == "100 │", "the busy plots start where the heads end");
  check(columns(as_text({f[8]}), 0, 5) == "  0 │" && columns(as_text({f[8]}), 51, 5) == "  0 │", "busy is 3 rows at 34");
  check(columns(as_text({f[9]}), 0, 5) == "RAM │" && columns(as_text({f[10]}), 0, 5) == "    │", "RAM is 2 rows at 34");
  check(columns(as_text({f[11]}), 0, 5) == "kern│" && columns(as_text({f[12]}), 0, 5) == "VM  │", "the Windows lanes");
  check(columns(as_text({f[11]}), 51, 5) == "iowt│" && columns(as_text({f[12]}), 51, 5) == "lost≈", "the WSL lanes, and lost's gutter is ≈");
  check(columns(as_text({f[13]}), 0, 24) == "threads · 20 of 20 ≥90% " && has(as_text({f[13]}), "idle █████ ≥90%"), "the caption's count and its five swatches");
  check(columns(as_text({f[13]}), 51) == "vCPUs, not Windows cores · 20 of 20 ≥90%", "the block says a vCPU is not a core");
  check(columns(as_text({f[9]}), 5, 1) == "⣀" && columns(as_text({f[10]}), 5, 1) == "⣿", "the Windows RAM plot is 65%, 5 dots of 8");
  check(columns(as_text({f[9]}), 56, 1) == " " && columns(as_text({f[10]}), 56, 1) == "⣶", "the WSL RAM plot is its own 39%, 3 dots of 8");
  int n = 14;
  for (const char* left : {"P0", "P1", "P2", "P3", "P4", "P5", "E0", "E2", "E4", "E6"}) {
    check(columns(as_text({f[static_cast<size_t>(n)]}), 0, 2) == left, std::string("row ") + left);
    ++n;
  }
  for (int r = 0; r < 10; ++r) check(columns(as_text({f[static_cast<size_t>(14 + r)]}), 51, 5) == pad("v" + std::to_string(2 * r), 4) + "│", "row v" + std::to_string(2 * r));
  check(columns(as_text({f[24]}), 5) == "-60s ───── busy avg 61% max 100% ────── now │      -60s ───── busy avg 61% max 100% ────── now", "the axis under both blocks, with each busy plot's average and maximum");
  row_is(25, "Windows  top 6 of 46 by cores                    │ WSL  top 6 of 64 by cores, dxg: no usage measured");
  row_is(26, " cores     pid name            thr WS GiB        │  cores     pid name        thr RSS GiB st dxg");
  row_is(27, " 13.25   31000 cpu_burn.exe     21    0.1        │  19.72    4121 cpu_burn     21     0.1  R");
  row_is(28, "  5.76    7120 vmmemWSL         22   10.1 WSL VM │   0.10    4388 python3      12     2.0  S holder");
  row_is(29, "  0.42       4 System          310    0.0        │   0.04    2000 node         11     0.4  S");
  row_is(30, "  0.31   11736 vmwp.exe         40    0.1 WSL VM │   0.03    2001 sshd          1     0.0  S");
  row_is(31, "  0.22    9001 카카오톡.exe     60    0.4        │   0.02    2002 systemd       1     0.0  S");
  row_is(32, "  0.20   10000 svchost.exe      12    0.0        │   0.01    2003 dbus-daemon   1     0.0  S");
  row_is(33, "[1] overall  [2] gpu  [3] cpu  [q] quit");

  // Both plots are as wide as the smaller column, whichever side is wider, and share the axis.
  for (const int w : {100, 101, 131, 132, 200}) {
    const auto g = large(s, h, w, 34);
    const int cells = (w - 3) / 2 - 5, right = kRight(w);
    check(static_cast<int>(cells_of(g[14]).size()) == right + 5 + cells, "the plots use the smaller width at " + std::to_string(w));
    check(columns(as_text({g[14]}), cells + 5, 3) == " │ ", "the divider sits where the left column ends at " + std::to_string(w));
  }

  // One onset: quiet to the left, amber to the right, on both sides and in the same cell of each.
  const auto left = cells_of(f[14]), right = cells_of(f[14]);
  check(left[5].fg == 236 && left[5].bg == 236 && left[47].fg == 179 && left[47].bg == 179, "the heat shows the onset");
  int onset_left = -1, onset_right = -1;
  for (int c = 0; c < 43; ++c) {
    if (onset_left < 0 && left[static_cast<size_t>(5 + c)].fg == 179) onset_left = c;
    if (onset_right < 0 && right[static_cast<size_t>(56 + c)].fg == 179) onset_right = c;
  }
  check(onset_left > 10 && onset_left == onset_right, "one onset lines up across the two blocks");
  check(left[5].glyph == "▀", "a heat cell is a half block");
  check(cells_of(f[11])[47].fg == 60 || cells_of(f[11])[47].fg == 67, "the kernel lane is a ramp colour");
  check(cells_of(f[12])[47].glyph == "█", "a lane is one full cell");
  check(cells_of(f[12])[56 + 42].fg == 179 && cells_of(f[12])[56 + 42].glyph == "█", "the lost lane is amber from 1 vCPU");
  check(columns(as_text({f[12]}), 56, 1) == " ", "a gated lost is a gap in its lane, not a cell");
  check(cells_of(f[11])[56 + 42].fg == 236, "the iowait lane is the iowait, 0 under a full guest");
}

void check_large_allocation() {
  struct want { int height, heat, busy, lanes, ram, lists; };
  for (const want& e : std::initializer_list<want>{{26, 0, 0, 0, 0, 0},  // Below the floor
                                                   {27, 10, 3, 0, 0, 3}, {28, 10, 3, 0, 0, 4}, {29, 10, 3, 2, 0, 3}, {30, 10, 3, 2, 1, 3},
                                                   {31, 10, 3, 2, 2, 3}, {32, 10, 3, 2, 2, 4}, {34, 10, 3, 2, 2, 6}, {35, 10, 4, 2, 2, 6},
                                                   {40, 10, 9, 2, 2, 6}, {43, 10, 12, 2, 2, 6}, {44, 10, 12, 2, 2, 7}, {66, 10, 12, 2, 2, 29}}) {
    cpu_rows p;
    const bool fits = plan_cpu_rows(10, e.height, p);
    const std::string at = " at " + std::to_string(e.height);
    if (e.heat == 0) { check(!fits, "below the floor" + at); continue; }
    check(fits && p.heat == e.heat && p.busy == e.busy && p.lanes == e.lanes && p.ram == e.ram && p.lists == e.lists, "the allocation" + at);
  }
  // More threads than rows: the lists get their 6 before the heat block takes more than 10.
  cpu_rows p;
  check(plan_cpu_rows(20, 27, p) && p.heat == 10 && p.lanes == 0, "20 natural rows have the same floor");
  check(plan_cpu_rows(20, 34, p) && p.heat == 10 && p.lists == 6, "and the same 34");
  check(plan_cpu_rows(20, 39, p) && p.heat == 15 && p.busy == 3 && p.lists == 6, "the heat block takes the next rows");
  check(plan_cpu_rows(20, 44, p) && p.heat == 20 && p.busy == 3, "up to 20 rows");
  check(plan_cpu_rows(32, 66, p) && p.heat == 20 && p.busy == 12, "never more");
  check(plan_cpu_rows(2, 19, p) && p.heat == 2 && !plan_cpu_rows(2, 18, p), "4 threads have a floor of 19");
  check(plan_cpu_rows(0, 17, p) && p.heat == 0, "no rows to draw leaves the fixed rows");

  const snapshot s = contended();
  const history h = contended_history(s);
  for (const int rows : {27, 28, 29, 30, 31, 34, 40, 43, 44, 50, 66}) {
    const auto f = large(s, h, 120, rows);
    const std::string at = " at 120x" + std::to_string(rows);
    check(static_cast<int>(f.size()) == rows && frame_fits(f, 120, rows), "the frame fills its rows" + at);
    check(has(as_text({f.back()}), "[q] quit"), "the footer is the last row" + at);
    check(is_large(f), "this layout" + at);
    check(has(as_text({f.back()}), "some rows hidden") == (rows < 30), "the footer says when lanes or RAM went" + at);
  }
  // Windows Terminal's default, from memory: all of it, with the RAM lane and 3 list rows.
  const auto wt = large(s, h, 120, 30);
  const std::string t = as_text(wt);
  check(has(t, "kern│") && has(t, "lost≈") && has(t, "top 3 of 46") && has(t, "top 3 of 64"), "120 by 30 keeps the lanes and 3 list rows");
  const int ram = row_at(wt, 0, "RAM │");
  check(ram > 0 && cells_of(wt[static_cast<size_t>(ram)])[8].glyph == "█" && cells_of(wt[static_cast<size_t>(ram)])[8].fg == 110, "at 30 rows RAM is a one-row lane on the ramp: 65% is the high step");
  check(row_at(wt, 0, "    │") == row_at(wt, 0, "100 │") + 1, "the only blank gutter is inside the busy plot");
  check(!has(as_text(large(s, h, 120, 29)), "RAM │"), "at 29 rows it has gone");
  check(has(as_text(large(s, h, 120, 29)), "kern│"), "and the lanes are still there");
  check(!has(as_text(large(s, h, 120, 28)), "kern│"), "at 28 the lanes have gone too");
  const auto at40 = large(s, h, 131, 40);
  check(row_at(at40, 0, "RAM │") - row_at(at40, 0, "100 │") == 9, "at 40 the busy graph is 9 rows");
}

void check_large_floors() {
  struct size_case { int threads, rows; bool highest; };
  // Numbered rows, two threads each, so the natural count is half the threads, rounded up.
  for (const auto& c : std::initializer_list<size_case>{{4, 2, false}, {8, 4, false}, {14, 7, false}, {20, 10, false}, {21, 10, true}, {40, 10, true}, {64, 10, true}}) {
    snapshot s = contended(c.threads);
    s.cpu_layout = {};
    const history h = contended_history(s);
    const int floor = 17 + c.rows;
    const std::string at = " at " + std::to_string(c.threads) + " threads";
    const auto f = large(s, h, 100, floor);
    check(is_large(f), "the floor is 17 plus heat rows" + at);
    check(!is_large(large(s, h, 100, floor - 1)), "one row less is the small layout" + at);
    check(!is_large(large(s, h, 99, 66)), "99 columns are the small layout" + at);
    check(has(as_text(large(s, h, 100, floor - 1)), "[q] quit"), "the small layout has its footer" + at);
    check(!has(as_text(large(s, h, 100, floor - 1)), "[t]"), "and no [t]" + at);
    check(frame_fits(f, 100, floor), "the floor fits" + at);
    check(has(as_text(f), "highest of each row") == c.highest, "the caption says when rows hold more" + at);
    for (const char* label : {"whole machine, includes the WSL VM", "guest view, inside the VM", "no usage measured", "WSL VM", "vCPUs, not Windows cores", "inferred"}) {
      check(has(as_text(f), label), std::string("labelled at the floor: ") + label + at);
      check(has(as_text(large(s, h, 131, 40)), label), std::string("labelled at 131 by 40: ") + label + at);
      check(has(as_text(large(s, h, 200, 66)), label), std::string("labelled at 200 by 66: ") + label + at);
    }
    check(frame_fits(large(s, h, 131, 40), 131, 40) && frame_fits(large(s, h, 300, 66), 300, 66), "wide windows fit" + at);
  }
  // With room, every row a pair, up to 20 rows; past 40 threads a row holds more, and says so.
  const auto tall = [](int threads) {
    snapshot s = contended(threads);
    s.cpu_layout = {};
    return large(s, contended_history(s), 131, 66);
  };
  check(row_at(tall(40), 0, "38") >= 0 && !has(as_text(tall(40)), "highest of each row"), "40 threads are 20 rows of pairs when there is room");
  check(row_at(tall(21), 0, "20") >= 0 && !has(as_text(tall(21)), "highest of each row"), "21 threads are 11 rows, the last half blank");
  check(has(as_text(tall(64)), "highest of each row") && row_at(tall(64), 0, "0 ") >= 0 && row_at(tall(64), 0, "62") < 0, "64 threads share 20 rows");
  check(row_at(tall(64), kRight(131), "v0 ") >= 0 && row_at(tall(64), kRight(131), "v3 ") >= 0, "and a shared row is named for its first thread");
  check(has(as_text(tall(64)), "vCPUs, not Windows cores · 64 of 64 ≥90% · highest of each row"), "the WSL caption says all of it where it fits");
  // A row shows its highest thread, however many it holds.
  snapshot hot_one = contended(64);
  hot_one.cpu_layout = {};
  hot_one.cpu.core_pct.assign(64, 2.0);
  hot_one.cpu.core_pct[1] = 95;
  history one_sample;
  one_sample.push(hot_one);
  const std::vector<line> hf = large(hot_one, one_sample, 131, 66);
  const std::vector<painted> shared_row = cells_of(line_at(hf, 0, "0 "));
  check(std::any_of(shared_row.begin(), shared_row.begin() + 64, [](const painted& c) { return c.fg == 179 || c.bg == 179; }), "a shared half shows the highest of its threads");
  // Fewer threads than pixels, more groups than rows: in order, each once, no blank pixel between.
  thread_order singles;
  for (int i = 0; i < 11; ++i) {
    singles.groups.push_back({i});
    singles.label.push_back(std::to_string(i));
  }
  bool shared = false;
  const std::vector<heat_row> packed = heat_rows(singles, 10, shared);
  check(packed.size() == 6 && packed[0].top == std::vector<int>{0} && packed[0].bottom == std::vector<int>{1} && packed[5].top == std::vector<int>{10} && packed[5].bottom.empty() && !shared, "threads that fit the pixels go in order");
  // Equal shares of 64 threads: 40 lines of pixels, each one or two threads, and every thread in one.
  bool highest = false;
  const std::vector<heat_row> rows = heat_rows(numbered_threads(64, "v"), 20, highest);
  std::vector<int> seen(64, 0);
  size_t most = 0;
  for (const heat_row& r : rows) {
    for (const int t : r.top) ++seen[static_cast<size_t>(t)];
    for (const int t : r.bottom) ++seen[static_cast<size_t>(t)];
    most = std::max({most, r.top.size(), r.bottom.size()});
  }
  check(rows.size() == 20 && highest && most == 2 && std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }), "every thread is in exactly one half");
  const auto no_read = heat_rows(numbered_threads(0, "v"), 10, highest);
  check(no_read.empty() && !highest, "no threads, no rows");
}

void check_large_layouts() {
  const auto labels = [](const cpu_layout_view& l, int n) {
    std::string out;
    const thread_order o = windows_order(l, n);
    for (const auto& g : o.groups) {
      out += o.label[static_cast<size_t>(g[0])];
      for (const int t : g) out += ":" + std::to_string(t);
      out += " ";
    }
    return out;
  };
  check(labels(layout_13600k(), 20) == "P0:0:1 P1:2:3 P2:4:5 P3:6:7 P4:8:9 P5:10:11 E0:12:13 E2:14:15 E4:16:17 E6:18:19 ", "P rows are siblings, E rows neighbours");
  check(labels(cpu_layout_view{}, 5) == "0:0:1 2:2:3 4:4 ", "layout unknown: rows like the guest, no P or E");
  check(labels(layout_13600k(), 16) == "0:0:1 2:2:3 4:4:5 6:6:7 8:8:9 10:10:11 12:12:13 14:14:15 ", "a layout of another size is not used");

  cpu_layout_view sibs;  // No hybrid cores, siblings not next to each other: bits, never index arithmetic
  sibs.known = true;
  for (uint32_t i = 0; i < 4; ++i) sibs.rows.push_back({i, 0, 0});
  for (uint32_t i = 0; i < 4; ++i) sibs.rows.push_back({i, 1, 0});
  check(labels(sibs, 8) == "C0:0:4 C1:1:5 C2:2:6 C3:3:7 ", "no hybrid cores: C, numbered by core, siblings by the layout");
  cpu_layout_view swapped;  // The first logical CPU of a core need not be its first thread
  swapped.known = true;
  swapped.rows = {{0, 1, 0}, {0, 0, 0}};
  check(labels(swapped, 2) == "C0:1:0 ", "a sibling's place comes from `thread`");
  cpu_layout_view flat;  // No SMT
  flat.known = true;
  for (uint32_t i = 0; i < 5; ++i) flat.rows.push_back({i, 0, 0});
  check(labels(flat, 5) == "C0:0:1 C2:2:3 C4:4 ", "no SMT: C, two neighbours a row");
  cpu_layout_view hybrid_flat;
  hybrid_flat.known = true;
  hybrid_flat.rows = {{0, 0, 0}, {1, 0, 0}, {2, 0, 1}, {3, 0, 1}};
  check(labels(hybrid_flat, 4) == "C0:2:3 C2:0:1 ", "hybrid with no SMT is C too, the fast cores first");
  cpu_layout_view three;  // Three classes, the highest not at the lowest numbers
  three.known = true;
  three.rows = {{0, 0, 0}, {1, 0, 1}, {2, 0, 2}, {2, 1, 2}, {3, 0, 2}, {3, 1, 2}, {4, 0, 1}};
  check(labels(three, 7) == "P0:2:3 P1:4:5 E0:1:6 E2:0 ", "three classes: P is the highest and E all others");

  // The frame reads the same order: the hot thread 4 is core 0's second thread, so row C0's bottom half.
  snapshot s = contended(8);
  s.cpu_layout = sibs;
  s.cpu.core_pct.assign(8, 1.0);
  s.cpu.core_pct[4] = 95;
  history h;
  h.push(s);
  const auto f = large(s, h, 100, 34);
  const std::vector<painted> c = cells_of(line_at(f, 0, "C0"));
  check(c[47].glyph == "▀" && c[47].fg == 236 && c[47].bg == 179, "the first thread is the top half and its sibling the bottom");
  check(row_at(f, 0, "C3") > 0 && row_at(f, 0, "P") < 0 && row_at(f, 0, "E") < 0, "no P or E on a machine without them");

  // Past the rows, labels still come from the layout, for the first thread of the row.
  snapshot small_room = contended(20);
  cpu_layout_view twenty_two = layout_13600k();
  small_room.cpu.core_pct.assign(22, 50.0);
  for (uint32_t c2 = 14; c2 < 16; ++c2) twenty_two.rows.push_back({c2, 0, 0});
  small_room.cpu_layout = twenty_two;
  const history h2 = contended_history(small_room);
  const auto f2 = large(small_room, h2, 100, 27);
  check(has(as_text(f2), "highest of each row") && row_at(f2, 0, "P0") > 0 && row_at(f2, 0, "E") > 0, "22 threads in 10 rows keep P and E");
}

// Cells of a block, from a history of known values.
void check_heat_cells() {
  // Blank never takes a colour: half a history, on a block with an odd thread, and a side with no data.
  const snapshot base = contended(21, 5);
  history h;
  for (int age = 20; age >= 0; --age) {
    snapshot s = base;
    s.unix_ms = kNow - age * 1000;
    guest_figures g;
    g.seq = age;
    s.wsl_json = guest_line(std::vector<double>(5, 50.0), g);
    h.push(s);
  }
  const std::vector<line> f = large(base, h, 131, 66);
  int blank = 0, coloured = 0;
  static const std::vector<int> kRamp = {236, 60, 67, 110, 179};
  const auto in_ramp = [](int c) { return std::find(kRamp.begin(), kRamp.end(), c) != kRamp.end(); };
  for (const line& l : f) {
    for (const span& p : l) {
      const bool spaces = p.text.find_first_not_of(' ') == std::string::npos;
      if (spaces) check(p.fg256 < 0 && p.bg256 < 0, "a blank cell is no ramp colour");
      if (p.fg256 >= 0) check(in_ramp(p.fg256), "only ramp colours are painted");
      if (p.bg256 >= 0) check(in_ramp(p.bg256), "only ramp colours fill a background");
      blank += spaces ? 1 : 0;
      coloured += p.fg256 >= 0 ? 1 : 0;
    }
  }
  check(blank > 0 && coloured > 0, "that frame had both");

  // Odd counts: the last thread has its top half and a blank below.
  const std::vector<painted> odd = cells_of(line_at(f, 0, "20"));
  check(odd[60].glyph == "▀" && odd[60].fg == 179 && odd[60].bg == -1, "an odd thread leaves a blank half");
  const std::vector<painted> odd_guest = cells_of(line_at(f, kRight(131), "v4"));
  check(odd_guest.back().glyph == "▀" && odd_guest.back().fg == 67 && odd_guest.back().bg == -1, "and so does an odd vCPU");

  // Only the second thread known: the lower half, in its colour, on nothing.
  snapshot second = contended(20);
  second.cpu.core_pct[0] = kNaN;
  second.cpu.core_pct[2] = 40;
  history one;
  one.push(second);
  const std::vector<line> g = large(second, one, 100, 34);
  std::vector<painted> c = cells_of(line_at(g, 0, "P0"));
  const size_t edge = 47;
  check(c[edge].glyph == "▄" && c[edge].fg == 179 && c[edge].bg == -1, "only the second known is the lower half");
  c = cells_of(line_at(g, 0, "P1"));
  check(c[edge].glyph == "▀" && c[edge].fg == 67 && c[edge].bg == 179, "both known: foreground first thread, background second");
  check(c[6].glyph == " " && c[6].fg == -1, "a cell with no sample is blank");

  // Neither known, by repeated lines: one carries, a second is a gap.
  const snapshot guest = contended();
  const auto with_repeats = [&](int repeats) {
    return build_history(guest, 5, [&](snapshot& s, int age, int seq) {
      guest_figures fig;
      fig.seq = age <= repeats ? 99 : seq;
      s.wsl_json = guest_line(read_wsl(guest.wsl_json).core_pct, fig);
    });
  };
  const auto newest = [&](const history& hist) {
    const std::vector<line> frame = large(guest, hist, 100, 34);
    return cells_of(line_at(frame, kRight(100), "v0")).back().glyph;
  };
  check(newest(with_repeats(0)) == "▀", "a new line is drawn");
  check(newest(with_repeats(1)) == "▀", "a repeated line carries for one sample: 13 of 58 under contention must not stripe");
  check(newest(with_repeats(2)) == " ", "two repeats in a row are a gap");

  // The block follows the newest sample, and a stall is blank cells.
  history stalled;
  for (const int age : {50, 49, 48, 3, 2, 1, 0}) {
    snapshot s = guest;
    s.unix_ms = kNow - age * 1000;
    guest_figures fig;
    fig.seq = age;
    s.wsl_json = guest_line(read_wsl(guest.wsl_json).core_pct, fig);
    stalled.push(s);
  }
  const std::vector<line> sf = large(guest, stalled, 100, 34);
  c = cells_of(line_at(sf, 0, "P0"));
  check(c[5 + 7].glyph == "▀" && c[5 + 25].glyph == " " && c[5 + 42].glyph == "▀", "a stall is a blank stretch in the block");

  // The clock steps back: the block survives.
  history stepped = contended_history(guest);
  snapshot back = guest;
  back.unix_ms = kNow - 3600000;
  back.interval_s = 1;
  stepped.push(back);
  const std::vector<line> bf = render(back, 100, 34, kNow - 3600000, &stepped, view::cpu);
  c = cells_of(line_at(bf, 0, "P0"));
  check(c[5 + 40].glyph == "▀" && c[5 + 42].fg == 179, "a clock that steps back keeps the block");

  // A busy cell's scale is fixed: 50 fills half, and over 100 reaches the top without changing the head.
  snapshot over = contended();
  over.cpu.busy_pct = 112;
  over.vm.vmmem_cores = 21.5;
  history oh;
  oh.push(over);
  const std::vector<line> of = large(over, oh, 100, 34);
  check(has(as_text(of), "112%  WINDOWS") && has(as_text(of), "VM ran 21.50 of 20 threads"), "over 100 is printed as measured");
  check(columns(as_text({line_at(of, 0, "100 ")}), 47, 1) == "⣿", "and the plot stops at its top");

  // Lanes: the kernel lane leaves the VM out, and a RAM plot is 2 rows of 0-100.
  snapshot lanes = contended();
  lanes.cpu_derived.own_kernel_pct = 2;
  lanes.cpu_derived.vm_cpu_pct = 70;
  lanes.memory.available = lanes.memory.total / 4;
  history lh;
  lh.push(lanes);
  const std::vector<line> lf = large(lanes, lh, 100, 34);
  check(cells_of(line_at(lf, 0, "kern"))[47].fg == 236, "the kern lane is kernel less the VM");
  check(cells_of(line_at(lf, 0, "VM  "))[47].fg == 110, "the VM lane is the VM");
  const int ram = row_at(lf, 0, "RAM │");
  check(columns(as_text({lf[static_cast<size_t>(ram)]}), 47, 1) == "⣤" && columns(as_text({lf[static_cast<size_t>(ram) + 1]}), 47, 1) == "⣿", "75% of 8 dots is 6");
  bool steel = false;
  for (const span& p : lf[static_cast<size_t>(ram)]) steel = steel || (has(p.text, "⣤") && p.color == tone::bar);
  check(steel, "RAM is steel");
  lanes.memory.available = lanes.memory.total / 50;
  bool red = false;
  const std::vector<line> low_ram = large(lanes, lh, 100, 34);
  for (const span& p : low_ram[5]) red = red || (has(p.text, "/63.7 GiB") && p.color == tone::act);
  check(red, "RAM uses left_tone");

  // The count is at 90%, the same line the top colour starts on.
  snapshot count = contended();
  count.cpu.core_pct.assign(20, 50.0);
  count.cpu.core_pct[0] = 89.9;
  count.cpu.core_pct[1] = 90;
  count.cpu.core_pct[2] = 112;
  check(has(as_text(large(count, one)), "threads · 2 of 20 ≥90%"), "the count is of threads at 90% or more");

  const std::vector<line> calm = large(idle_now(), idle_history(idle_now()), 100, 34);
  for (const line& l : calm) {
    for (const span& p : l) check(!(has(p.text, "▀") && p.fg256 == 179), "an idle block has no hot cell");
  }
}

void check_lost_text() {
  const auto third = [](const snapshot& s, const history& h) { return columns(as_text({large(s, h)[4]}), 51); };
  snapshot s = contended();
  const history h = contended_history(s);
  check(third(s, h) == "WSL short ≈14 of 20 vCPUs, inferred · load 19.8", "the contended head");

  const auto watch_on = [&](const snapshot& t) {
    const std::vector<line> f = large(t, h);
    for (const span& p : f[4]) {
      if (has(p.text, "WSL short")) return p.color == tone::watch;
    }
    return false;
  };
  check(watch_on(s), "WSL short is watch from 1 vCPU");
  s.lost.vcpus = 1;
  check(has(third(s, h), "≈1 of 20") && watch_on(s), "at exactly 1 vCPU too");
  s.lost.vcpus = 0.99;
  check(third(s, h) == "WSL short <1 vCPU, inferred · load 19.8", "under 1 vCPU it prints <1");
  check(!watch_on(s), "and is not watch");

  s = contended();
  s.lost = {lost_state::collecting, kNaN};
  check(third(s, h) == "load 19.8 · lost CPU collecting", "until 5 samples it says so");
  s.lost = {lost_state::guest_under_99, kNaN};
  check(third(s, h) == "load 19.8 · lost CPU n/a under 99% busy", "under 99% guest busy, n/a and why");
  s.lost = {lost_state::windows_under_95, kNaN};
  check(third(s, h) == "load 19.8 · lost CPU n/a, Windows under 95%", "under 95% Windows busy, n/a and why");
  s.lost = {lost_state::no_data, kNaN};
  check(third(s, h) == "load 19.8 · lost CPU n/a", "with a stale line or a reset, n/a");
  for (const lost_state state : {lost_state::collecting, lost_state::guest_under_99, lost_state::windows_under_95, lost_state::vcpus_differ, lost_state::no_data}) {
    s.lost = {state, 3.5};  // Only known has a figure, whatever vcpus holds
    const std::string t = as_text(large(s, h));
    check(!has(t, "WSL short") && !has(t, "inferred"), "a gated lost prints no figure and no claim");
  }
  s.lost = {lost_state::known, kNaN};
  check(!has(as_text(large(s, h)), "WSL short"), "known with no figure is no figure");
  s = contended(20, 4);
  {
    snapshot first_sample = busy();
    first_sample.vm.vmmem_cores_known = false;
    check(has(as_text(render(first_sample, 131, 40)), "vmmemWSL 10.1 GiB, n/a cores"), "the overall screen never prints an unmeasured 0 as cores");
    check(has(as_text(render(busy(), 131, 40)), "vmmemWSL 10.1 GiB, 3.10 cores"), "and prints a measured one");
  }
  s.lost = {lost_state::vcpus_differ, kNaN};
  check(has(third(s, h), "lost CPU n/a, vCPUs differ"), "a guest with fewer vCPUs than Windows has threads is outside the evidence");

  // When a load stops the figure is gone at once and the lane holds nothing new: never a 0.
  snapshot stopped = contended();
  stopped.lost = {lost_state::guest_under_99, kNaN};
  const history fading = build_history(stopped, 60, [&](snapshot& t, int age, int seq) {
    guest_figures g;
    g.seq = seq;
    t.wsl_json = guest_line(read_wsl(stopped.wsl_json).core_pct, g);
    t.lost = age > 10 ? lost_view{lost_state::known, 13.96} : lost_view{lost_state::guest_under_99, kNaN};
  });
  const std::vector<line> f = large(stopped, fading);
  const std::vector<painted> lane_cells = cells_of(line_at(f, kRight(100), "lost≈"));
  check(lane_cells[56 + 20].fg == 179 && lane_cells.size() > 56 + 40 && lane_cells[56 + 42].glyph == " " && lane_cells[56 + 42].fg == -1, "a stopped load leaves a gap in the lane");
  check(!has(as_text(f), "WSL short"), "and the head says nothing about it");

  // The lane under the floor is quiet, however large its share of a small guest.
  snapshot few = contended(4);
  few.lost = {lost_state::known, 0.9};
  history fh;
  fh.push(few);
  const std::vector<line> ff = large(few, fh);
  check(cells_of(line_at(ff, kRight(100), "lost≈")).back().fg == 236, "under 1 vCPU the lane is quiet");
  few.lost.vcpus = 1;
  fh = history();
  fh.push(few);
  const std::vector<line> at_one = large(few, fh);
  check(cells_of(line_at(at_one, kRight(100), "lost≈")).back().fg == 179, "at 1 vCPU it is amber");
  bool mark = false;
  for (const span& p : line_at(f, kRight(100), "lost≈")) mark = mark || (p.text == "≈" && p.color == tone::watch);
  check(mark, "the gutter marks the lane as inferred");

  // What gives way on a tight row: the load, the ready figure.
  s = contended();
  s.lost.vcpus = 128;
  s.wsl_json = guest_line(std::vector<double>(128, 100.0), [] { guest_figures g; g.load = 123456.7; return g; }());
  check(!has(as_text(large(s, h)), "load 123") && has(as_text(large(s, h)), "≈128 of 128 vCPUs, inferred"), "the head drops load, never the label");
  check(has(as_text(large(s, h, 131, 34)), "load 123456.7"), "a wider head keeps it");
  s = contended();
  s.cpu_derived.ready_threads_mean = 123456;
  s.cpu_derived.vm_ready_threads_mean = 123456;
  check(columns(as_text({large(s, h)[4]}), 0, 48).find("VM ran 5.76 of 20 threads · ≈123456 ready") == 0, "the ready line loses its VM count first");
  s = contended();
  s.cpu_derived.ready_threads_mean = s.cpu_derived.vm_ready_threads_mean = kNaN;
  check(has(as_text(large(s, h)), " · ready collecting"), "ready needs 5 samples and says so");
  s.ready_threads.reset();
  check(has(as_text(large(s, h)), " · ready n/a"), "an unread count is n/a");
  s = contended();
  s.cpu_derived.vm_ready_threads_mean = kNaN;
  check(has(as_text(large(s, h, 131, 34)), "≈41 ready, VM collecting"), "the VM's count can lag the machine's");
  s.vm_ready_threads.reset();
  check(has(as_text(large(s, h)), "≈41 ready, VM n/a"), "with no vmmem the machine's count stands alone");
}

void check_large_states() {
  const snapshot base = contended();
  const history full = contended_history(base);
  const auto axis_of = [](const std::vector<line>& f) {
    const int at = row_at(f, 0, "E6");
    return at < 0 ? std::string() : columns(as_text({f[static_cast<size_t>(at + 1)]}), 0, 48);
  };

  // Fresh launch.
  history young = build_history(base, 8, [&](snapshot& s, int, int seq) {
    guest_figures g;
    g.seq = seq;
    s.wsl_json = guest_line(read_wsl(base.wsl_json).core_pct, g);
  });
  snapshot launch = base;
  launch.unix_ms = base.unix_ms;
  launch.lost = {lost_state::collecting, kNaN};
  std::vector<line> f = large(launch, young);
  // The faint · says how much was watched; the axis no longer counts it (design/panel-grammar.md part 5).
  check(has(axis_of(f), "busy avg") && !has(as_text(f), "collecting:"), "a fresh launch labels what it has, and counts nothing");
  check(has(as_text(f), "lost CPU collecting") && has(as_text(f), "100%  WINDOWS") && has(as_text(f), "VM ran 5.76"), "instant figures are valid from the first sample");
  const std::string none = axis_of(large(launch, history()));
  check(has(none, "-60s ─") && has(none, "─ now") && !has(none, "avg") && !has(none, "collecting"), "no history yet: no label, no count");
  check(!has(as_text(large(base, full, 100, 34, options_for(300))), "collecting"), "a longer span counts nothing either");

  // WSL starting.
  snapshot starting = base;
  starting.wsl_json.clear();
  starting.wsl_age_s = -1;
  starting.lost = {lost_state::no_data, kNaN};
  const history waiting = build_history(starting, 4, [](snapshot&, int, int) {});
  f = large(starting, waiting);
  check(has(as_text(f), "WSL starting, no data yet: 4 s") && !has(as_text(f), "lost CPU"), "WSL starting says how long");
  check(has(as_text(f), "n/a   WSL  user n/a · sys n/a · iowait n/a"), "and every guest figure is n/a");
  check(has(as_text(f), "100%  WINDOWS"), "Windows carries on");

  // WSL stopped, or the collector failed.
  snapshot gone = base;
  gone.wsl_json.clear();
  gone.wsl_age_s = -1;
  gone.wsl_restarts = 2;
  gone.lost = {lost_state::no_data, kNaN};
  const history before_gone = build_history(gone, 60, [](snapshot&, int, int) {});
  f = large(gone, before_gone);
  std::string t = as_text(f);
  check(has(t, "WSL unavailable: the collector sent nothing") && has(t, "collector restarted 2 times"), "the head says why");
  check(!has(t, "lost CPU") && !has(t, "WSL short") && !has(t, "inferred"), "lost is not shown");
  check(row_at(f, kRight(100), "100 │") > 0 && columns(as_text({f[8]}), 56) == std::string(43, ' '), "the guest plots are gaps");
  check(has(t, "WSL  ") && has(t, "guest view, inside the VM"), "the descriptor stays");
  snapshot unread = base;
  unread.wsl_json = "{\"schema\":1,\"busy";
  unread.lost = {lost_state::no_data, kNaN};
  check(has(as_text(large(unread, before_gone)), "WSL unavailable: unreadable collector line"), "an unreadable line says so");
  unread.wsl_json = R"({"schema":2,"busy_pct":1,"memory":{}})";
  check(has(as_text(large(unread, before_gone)), "WSL unavailable: newer schema than this screen"), "a newer schema says so");
  unread.wsl_json = R"({"schema":2,"distro":"Refused","busy_pct":77,"core_pct":[77,77,77,77,77,77],"cpu_split":{"user":66,"sys":11,"iowait":3},"memory":{"total":1000,"available":10}})";
  t = as_text(large(unread, before_gone));
  check(!has(t, "66") && !has(t, "77") && !has(t, "Refused") && has(t, "n/a   WSL"), "a refused line lends the head nothing");
  check(row_at(large(unread, before_gone), kRight(100), "v0") < 0, "nor the block its rows");

  // Stale: appended to the second row, in watch, and the descriptor stays.
  const snapshot stale = stale_now();
  const history sh = stale_history(stale);
  f = large(stale, sh);
  t = columns(as_text({f[3]}), 51);
  check(t == "      guest view, inside the VM · 3600 s old", "the age is on the descriptor's row, and the distro gave way: " + t);
  check(has(as_text({large(stale, sh, 131, 34)[3]}), "guest view, inside the VM · Ubuntu-24.04 · 3600 s old"), "with room it stays");
  bool watched = false;
  for (const span& p : f[3]) watched = watched || (has(p.text, "3600 s old") && p.color == tone::watch);
  check(watched, "in watch");
  const std::vector<painted> stale_cells = cells_of(line_at(f, kRight(100), "v0"));
  check(stale_cells[56 + 10].glyph == "▀" && stale_cells.back().glyph == " ", "stale cells are blank, older ones stay");
  check(has(as_text(f), "lost CPU n/a") && !has(as_text(f), "WSL short"), "lost resets");
  check(has(as_text({line_at(f, 0, "threads")}), "vCPUs, not Windows cores · n/a ≥90%"), "an old line is not counted as now");
  snapshot just = contended();
  just.wsl_age_s = 4.99;
  check(!has(as_text({large(just, full)[3]}), " s old"), "under 5 s there is no age");
  just.wsl_age_s = 5;
  check(has(as_text({large(just, full)[3]}), "· 5 s old"), "at 5 s there is");
  just.wsl_age_s = -1;
  check(has(as_text({large(just, full)[3]}), "age unknown"), "unknown age says so");
  snapshot steal = contended();
  steal.wsl_json.replace(steal.wsl_json.find("\"steal_pct\":0"), 13, "\"steal_pct\":0.3");
  check(has(as_text({large(steal, full, 131, 34)[3]}), "· steal 0.3%"), "steal is shown above 0");
  check(!has(as_text({large(contended(), full, 131, 34)[3]}), "steal"), "and only above 0");
  snapshot long_name = stale_now();
  const std::string key = "\"distro\":\"Ubuntu-24.04\"";
  long_name.wsl_json.replace(long_name.wsl_json.find(key), key.size(), "\"distro\":\"\\u001b]0;pwned\\u0007\\u202e" + std::string(60, 'd') + "\"");
  t = as_text({large(long_name, full)[3]});
  check(has(t, "guest view, inside the VM ·") && has(t, "· 3600 s old"), "the distro is cut first");
  check(vt_clean(vt_frame(large(long_name, full), 34)), "a hostile distro stays safe");

  // Windows CPU read failed.
  snapshot failed = contended();
  failed.cpu = {};
  failed.cpu_derived = {};
  failed.lost = {lost_state::no_data, kNaN};
  const history fh = build_history(failed, 3, [](snapshot&, int, int) {});
  f = large(failed, fh);
  t = as_text(f);
  check(has(t, "n/a   WINDOWS  user n/a · kernel n/a · VM n/a"), "a failed Windows read is n/a");
  check(has(t, "100%  WSL") && has(t, "lost CPU n/a"), "the guest carries on, lost n/a");
  check(!has(columns(as_text({line_at(f, 0, "threads")}), 0, 40), "0 of"), "no count is made up");
  // Failing after a good minute keeps the rows: the gap is in the cells.
  history failing = contended_history(base);
  snapshot one_fail = failed;
  one_fail.unix_ms = base.unix_ms + 1000;
  failing.push(one_fail);
  // One failed second can share a 1.4 s cell with a good one, and design section 7 allows that. Two fill it.
  one_fail.unix_ms += 1000;
  failing.push(one_fail);
  one_fail.unix_ms += 1000;
  failing.push(one_fail);
  f = render(one_fail, 100, 34, kNow + 3000, &failing, view::cpu);
  check(row_at(f, 0, "P0") > 0, "a failed read keeps the block in place");
  check(cells_of(line_at(f, 0, "P0")).back().fg == -1 || cells_of(line_at(f, 0, "P0"))[47].glyph == " ", "and its newest cell is blank");

  // vmmem not found, or the first sample of a new one: a bare 0 is never a number.
  snapshot no_vm = contended();
  no_vm.vm.vmmem_cores = 0;
  no_vm.vm.vmmem_cores_known = false;
  no_vm.cpu_derived.vm_cpu_pct = no_vm.cpu_derived.own_kernel_pct = kNaN;
  no_vm.lost = {lost_state::no_data, kNaN};
  t = as_text(large(no_vm, full));
  check(has(t, "user 65 · kernel n/a · VM n/a"), "VM n/a");
  check(has(t, "VM ran n/a of 20 threads") && !has(t, "VM ran 0"), "the VM's cores are n/a, not 0");
  check(has(t, "  n/a    7120 vmmemWSL"), "and its row in the list");
  no_vm.vm.vmmem_pid = 0;
  no_vm.vm.vmwp_pid = 0;
  check(has(as_text(large(no_vm, full)), "VM ran n/a"), "not found reads the same");
  history vh;
  snapshot first_vm = contended();
  first_vm.vm.vmmem_cores_known = false;  // The figures are stale leftovers, and must not be believed
  vh.push(first_vm);
  check(has(as_text(large(first_vm, vh)), "VM n/a"), "vmmem_cores_known decides, not whether the number looks real");
  const std::vector<line> vf = large(first_vm, vh);
  check(cells_of(line_at(vf, 0, "VM  "))[47].glyph == " ", "and the lane is a gap");

  // Layout unknown, on the frame.
  snapshot unknown = contended();
  unknown.cpu_layout = {};
  f = large(unknown, full);
  check(row_at(f, 0, "0   ") > 0 && row_at(f, 0, "18  ") > 0 && row_at(f, 0, "P0") < 0, "layout unknown: numbered rows");
  unknown.cpu_layout = layout_13600k();
  unknown.cpu_layout.rows.pop_back();
  check(row_at(large(unknown, full), 0, "P0") < 0, "a layout that is one short is not trusted");

  // .wslconfig limits vCPUs: fewer rows, top-aligned, blank below, and the count says of 4.
  snapshot limited = contended(20, 4);
  limited.lost = {lost_state::no_data, kNaN};
  const history lh = contended_history(limited);
  f = large(limited, lh);
  t = as_text(f);
  check(row_at(f, kRight(100), "v0") == row_at(f, 0, "P0") && row_at(f, kRight(100), "v2") == row_at(f, 0, "P1") && row_at(f, kRight(100), "v4") < 0, "fewer rows, top-aligned");
  check(has(t, "4 of 4 ≥90%") && has(t, "20 of 20 ≥90%"), "each caption counts its own");
  check(columns(as_text({line_at(f, 0, "E6")}), 51).empty(), "blank below");
  check(has(axis_of(f), "now"), "and the axis is still both sides'");

  // vCPU count changes: rows follow the newest sample, and a vCPU that is gone is blank, not 0.
  snapshot grew = contended(20, 8);
  const history gh = build_history(grew, 20, [&](snapshot& s, int age, int seq) {
    guest_figures g;
    g.seq = seq;
    s.wsl_json = guest_line(std::vector<double>(age > 10 ? 4 : 8, 100.0), g);
  });
  f = large(grew, gh);
  std::vector<painted> c = cells_of(line_at(f, kRight(100), "v6"));
  check(c.back().fg == 179 && c[c.size() - 16].glyph == " ", "a new vCPU has no past");
  snapshot shrank = contended(20, 4);
  const history sk = build_history(shrank, 20, [&](snapshot& s, int age, int seq) {
    guest_figures g;
    g.seq = seq;
    s.wsl_json = guest_line(std::vector<double>(age > 10 ? 8 : 4, 100.0), g);
  });
  check(row_at(large(shrank, sk), kRight(100), "v4") < 0, "a lost vCPU's row goes");

  // More than 64 logical CPUs: Windows reports one group, and the head says only what it knows.
  snapshot sixty_four = contended(64);
  t = as_text(large(sixty_four, contended_history(sixty_four)));
  check(has(t, "VM ran 5.76 of 64 threads") && !has(t, "of 128"), "one group: no machine-wide count is in the snapshot");
  snapshot many_vcpus = contended(64, 128);
  many_vcpus.lost = {lost_state::no_data, kNaN};
  check(frame_fits(large(many_vcpus, contended_history(many_vcpus), 131, 40), 131, 40), "more vCPUs than a sample keeps still fit");
}

void check_large_lists() {
  snapshot s = contended();
  s.processes.clear();
  const auto proc = [&](uint32_t pid, double cores) {
    process_row p;
    p.pid = pid; p.name = "p" + std::to_string(pid); p.cores = cores; p.threads = 3;
    s.processes.push_back(p);
  };
  proc(900, 0.24);   // 0.2 once rounded, the same as the next two
  proc(800, 0.21);
  proc(700, 0.16);
  proc(600, 0.14);   // 0.1
  proc(100, 2.5);
  proc(0, 11.0);     // Idle
  proc(500, kNaN);   // A number that is not one sorts last
  const history h = contended_history(s);
  const std::string t = as_text(large(s, h, 100, 40));
  const size_t a = t.find("p100"), b = t.find("p700"), c = t.find("p800"), d = t.find("p900"), e = t.find("p600"), f = t.find("p500");
  check(a < b && b < c && c < d && d < e && e < f, "strict on cores rounded to 0.1, then PID");
  check(!has(t, "Idle") && has(t, "top 6 of 6 by cores"), "the idle process is not listed");
  check(cores_key(0.149) == 1 && cores_key(0.151) == 2 && cores_key(kNaN) == -1 && cores_key(kInf) == -1 && cores_key(1e300) == 10000000000, "the sort key is 0.1 cores");

  snapshot w = contended();
  w.wsl_json = guest_line(std::vector<double>(20, 100.0), {},
                          R"({"pid":30,"name":"zeta","state":"R","rss":1,"cores":0.24,"threads":2,"on_gpu":false},)"
                          R"({"pid":20,"name":"beta","state":"D","rss":1,"cores":0.16,"threads":null,"on_gpu":true},)"
                          R"({"pid":10,"name":"한글추론기","state":"S","rss":1,"cores":0.01,"on_gpu":false},)"
                          R"({"pid":40,"name":"]0;pwned[31mevil","state":"S","rss":1,"cores":3,"threads":1,"on_gpu":false})");
  const std::vector<line> frame = large(w, h, 100, 40);
  const std::string wt = as_text(frame);
  check(wt.find("pwned") < wt.find("beta") && wt.find("beta") < wt.find("zeta"), "the guest sorts the same way: rounded cores, then PID, and a holder has no place of its own");
  check(has(wt, "20 beta        n/a"), "a null thread count is n/a");
  check(has(wt, " holder") && !has(wt, "on GPU"), "a holder is a holder");
  check(vt_clean(vt_frame(frame, 40)), "hostile names stay safe");
  bool sides = false;
  for (const line& l : frame) {
    const std::string row_text = as_text({l});
    if (has(row_text, "svchost.exe")) { sides = true; check(columns(row_text, 0, 48).find("svchost") != std::string::npos, "Windows names stay left"); }
    if (has(row_text, "한글추론기")) check(columns(row_text, 51).find("한글추론기") != std::string::npos, "Linux names stay right");
  }
  check(!has(wt, "cpu_burn.exe") || wt.find("Windows  top") < wt.find("WSL  top"), "two lists");
  check(sides || has(wt, "p100") || has(wt, "cpu_burn.exe"), "the Windows list was drawn");

  // A long count keeps the label: `by cores` is the part that gives way.
  snapshot big = contended();
  big.wsl_json.replace(big.wsl_json.find("\"process_count\":64"), 18, "\"process_count\":1280");
  check(has(as_text(large(big, h)), "WSL  top 6 of 1280, dxg: no usage measured"), "the WSL head keeps no usage measured");
  check(has(as_text(large(big, h, 131, 34)), "WSL  top 6 of 1280 by cores, dxg: no usage measured"), "with room it keeps both");

  snapshot empty = contended();
  empty.processes.clear();
  const std::vector<line> sparse = large(empty, h, 100, 66);
  check(sparse.size() == 66 && has(as_text({sparse.back()}), "[q] quit") && has(as_text({sparse[60]}), "│ ") == false, "a short list leaves the footer on the last row");
  check(has(as_text(large(empty, h)), "Windows  ") && has(as_text(large(empty, h)), "unavailable"), "an empty list is unavailable");
  check(frame_fits(large(empty, h), 100, 34), "and fits");

  // More rows than processes: the footer stays on the last row.
  const auto tall = large(contended(), h, 100, 66);
  check(tall.size() == 66 && has(as_text({tall.back()}), "[q] quit"), "short lists leave the footer in place");
}

// Windows and WSL describe overlapping work, and no figure here adds them.
void check_no_sums() {
  snapshot s = contended();
  s.cpu.busy_pct = 37;
  s.cpu.user_pct = 21;
  s.cpu_derived = {13, 4, 7, 3};
  s.vm.vmmem_cores = 2.60;
  s.vm.vmmem_cores_known = true;
  s.memory.total = 40 * kG;
  s.memory.available = 30 * kG;
  guest_figures g;
  g.busy = 41;
  g.user = 40;
  g.used_gib = 7;
  s.wsl_json = guest_line(std::vector<double>(20, 41.0), g);
  s.lost = {lost_state::guest_under_99, kNaN};
  const history h = idle_history(s);
  for (const int w : {100, 131}) {
    const std::string t = as_text(large(s, h, w, 40));
    for (const char* sum : {"78", "61", "17.0", "71.2", "9.60"}) check(!has(t, sum), std::string("no sum of the two sides: ") + sum);
  }
}

void check_large_bytes() {
  const snapshot noisy = noisy_now(), calm = contended();
  const history nh = noisy_history(noisy), ch = contended_history(calm);
  const std::string vt = vt_frame(large(noisy, nh, 131, 40), 40), quiet = vt_frame(large(calm, ch, 131, 40), 40);
  printf("frame bytes at 131 by 40: noisy %zu, contended %zu\n", vt.size(), quiet.size());
  check(vt.size() < 48 * 1024, "a noisy frame is under 48 KiB: " + std::to_string(vt.size()));
  check(quiet.size() < vt.size() / 2 + 2000, "a calm one is far under it: equal cells are merged");
  check(vt.size() > 24 * 1024, "the noisy fixture is noisy, or this check proves nothing");
  // That fixture has a colour change in most cells.
  const auto rows = large(noisy, nh, 131, 40);
  const std::vector<painted> c = cells_of(line_at(rows, 0, "P0"));
  int changes = 0;
  std::vector<int> colours;
  for (size_t i = 6; i < 64 && i < c.size(); ++i) {
    changes += c[i].fg != c[i - 1].fg || c[i].bg != c[i - 1].bg ? 1 : 0;
    colours.push_back(c[i].fg);
    colours.push_back(c[i].bg);
  }
  for (const int colour : {236, 60, 67, 110, 179}) check(std::count(colours.begin(), colours.end(), colour) > 0, "every step of the ramp: " + std::to_string(colour));
  check(changes > 40, "changing in nearly every cell: " + std::to_string(changes));
  const snapshot forty = noisy_now(40);
  const history fh = noisy_history(forty);
  check(vt_frame(large(forty, fh, 131, 40), 40).size() < 48 * 1024, "40 noisy threads are under it too");
  check(vt_clean(vt), "a noisy frame is only our own sequences");
}

void check_span_key() {
  const snapshot s = contended();
  const history h = contended_history(s, 600);
  render_options wired;
  wired.span_key = true;

  check(next_span(60000) == 300000 && next_span(300000) == 600000 && next_span(600000) == 60000, "t steps 1, 5 and 10 minutes, and wraps");
  check(next_span(12345) == 60000, "a span it does not know starts over");

  // Only when the caller says the key is wired, and only where it does something.
  check(!has(as_text(large(s, h, 100, 34)), "[t]"), "no dead key: without the option the footer has no [t]");
  check(has(as_text({large(s, h, 100, 34, wired).back()}), "[1] overall  [2] gpu  [3] cpu  [t] span 60s  [q] quit"), "the footer of section 6");
  for (const int span_s : {300, 600}) {
    render_options o = options_for(span_s);
    const std::string t = as_text(large(s, h, 131, 40, o));
    const std::string name = span_s == 300 ? "5m" : "10m";
    check(has(t, "[t] span " + name), "the footer says the span");
    check(has(t, "-" + name + " ─"), "the axis says the span");
    const std::string busy = span_s == 300 ? "busy avg 100% max 100%" : "busy avg 60% max 100%";
    check(has(t, busy + (span_s == 300 ? " · each cell 5.1 s" : " · each cell 10 s")), "past 60 s the axis says what a cell is");
    check(!has(t, "peak over mean") && !has(t, "collecting"), "and not the old captions");
    check(has(as_text(large(s, h, 100, 34, o)), busy + " ─") && !has(as_text(large(s, h, 100, 34, o)), "each cell"), "where both do not fit, the cell's length goes first");
  }
  check(!has(as_text(render(s, 131, 40, kNow, &h, view::overall, wired)), "[t]") && !span_key_acts(s, h, view::overall, 131, 40), "t is absent from panel 1, which has no graph");
  check(as_text(render(s, 131, 40, kNow, &h, view::overall, options_for(600))) == as_text(render(s, 131, 40, kNow, &h, view::overall)), "and panel 1 ignores the range");
  // Panel 2 and the small layout of panel 3 draw over the range too.
  const std::string plain = as_text(render(s, 131, 40, kNow, &h, view::gpu));
  check(has(plain, "-60s ") && !has(plain, "-5m ") && !has(plain, "[t]"), "panel 2 is at 1 minute by default, and offers no dead key");
  for (const int span_s : {300, 600}) {
    render_options o = options_for(span_s);
    o.span_key = true;
    const std::string name = span_s == 300 ? "5m" : "10m";
    const std::string g = as_text(render(s, 131, 40, kNow, &h, view::gpu, o)), c = as_text(render(s, 99, 40, kNow, &h, view::cpu, o));
    check(has(g, "-" + name + " ─") && !has(g, "-60s") && has(g, "[t] span " + name), "panel 2 draws over the range and says so");
    check(has(c, "-" + name + " ─") && !has(c, "-60s") && has(c, "[t] span " + name), "the small layout draws over the range and says so");
  }
  check(span_key_acts(s, h, view::gpu, 131, 40) && has(as_text(render(s, 131, 40, kNow, &h, view::gpu, wired)), "[t]"), "t acts on panel 2");
  check(has(as_text(render(s, 99, 40, kNow, &h, view::cpu, wired)), "[t]") && span_key_acts(s, h, view::cpu, 99, 40), "t acts in the small layout");
  check(has(as_text(render(s, 100, 26, kNow, &h, view::cpu, wired)), "[t]") && span_key_acts(s, h, view::cpu, 100, 26), "and below the height floor");
  check(span_key_acts(s, h, view::cpu, 100, 27) && has(as_text(render(s, 100, 27, kNow, &h, view::cpu, wired)), "[t]"), "at the floor it acts");
  check(span_key_acts(s, h, view::cpu, 100, 34), "and at the design's size");

  // A span shows the samples it covers: 10 minutes reach back past the 60 s the default shows.
  const std::vector<line> shortest = large(s, h, 100, 34), longest = large(s, h, 100, 34, options_for(600));
  check(cells_of(line_at(shortest, 0, "P0"))[5].fg == 179, "60 s of a long load are all hot");
  check(cells_of(line_at(longest, 0, "P0"))[5].fg == 236, "10 minutes reach back past its onset");
  // A burst: the cell is its mean, the label and one dot say how high.
  snapshot burst = contended();
  const history bh = build_history(burst, 600, [&](snapshot& t, int age, int seq) {
    quiet_moment(t, seq);
    if (age % 40 == 20) {
      t.cpu.busy_pct = 100;
      t.cpu.core_pct.assign(20, 100.0);
    }
  });
  const std::vector<line> bf = large(burst, bh, 100, 34, options_for(600));
  const std::string burst_top = columns(as_text({line_at(bf, 0, "100 ")}), 5, 43);
  check(count_braille(burst_top) == 1 && has(burst_top, "⠈"), "a one-second burst in a 14 s cell is its mean, and the latest one's cell carries the maximum's dot: " + burst_top);
  check(display_width(burst_top.substr(0, burst_top.find("⠈"))) == 41, "the dot is in the latest burst's cell, 20 s before now");
  check(has(as_text(bf), "busy avg 6% max 100%"), "the label says how high it went");
  const std::vector<painted> burst_row = cells_of(line_at(bf, 0, "P0"));
  check(std::none_of(burst_row.begin(), burst_row.begin() + 48, [](const painted& c) { return c.fg == 179; }), "and does not paint 20 seconds hot");

  // The mean in every plot and lane, and in each plot the maximum's dot: one bucket of 14 s, one sample high
  // among quiet ones.
  const auto spike = [&](snapshot& t, int age, int seq) {
    quiet_moment(t, seq);
    const bool high = age == 200;  // In the middle of a full bucket
    t.cpu.busy_pct = high ? 100 : 4;
    guest_figures g;
    g.seq = seq;
    g.busy = high ? 100 : 3;
    g.iowait = high ? 95 : 3;
    g.used_gib = high ? 30.0 : 3.0;
    t.wsl_json = guest_line(read_wsl(burst.wsl_json).core_pct, g);
    t.memory.available = t.memory.total / (high ? 20 : 2);
    t.cpu_derived.own_kernel_pct = high ? 95 : 3;
  };
  const history sh = build_history(burst, 600, spike);
  const std::vector<line> sf = large(burst, sh, 100, 40, options_for(600)), lean = large(burst, sh, 120, 30, options_for(600));
  const auto marked_once = [&](const char* start, int side) {
    const std::string top = columns(as_text({line_at(sf, side, start)}), side + 5, 43);
    return count_braille(top) == 1 && has(top, "⠈");
  };
  const auto any_hot = [](const line& l, int from, int cells_wide = 43) {
    const std::vector<painted> cells = cells_of(l);
    return std::any_of(cells.begin() + from, cells.begin() + from + cells_wide, [](const painted& c) { return c.fg == 179; });
  };
  check(marked_once("100 │", 0), "the Windows busy plot takes the mean, and one dot marks its maximum");
  check(marked_once("100 │", kRight(100)), "the WSL busy plot too");
  check(marked_once("RAM │", 0), "the Windows RAM plot too");
  check(marked_once("RAM │", kRight(100)), "the WSL RAM plot too");
  check(!any_hot(line_at(sf, 0, "kern"), 5), "the kern lane takes the mean");
  check(!any_hot(line_at(sf, kRight(100), "iowt"), kRight(100) + 5), "the iowait lane takes the mean");
  check(!any_hot(line_at(lean, 0, "RAM │"), 5, 53), "a RAM lane takes the mean");
  check(!any_hot(line_at(lean, kRight(120), "RAM │"), kRight(120) + 5, 53), "on both sides");
}

void check_large_captions() {
  snapshot s = contended();
  const history h = contended_history(s);
  std::vector<line> f = large(s, h);
  const line caption = line_at(f, 0, "threads");
  int swatch = 0;
  for (const span& p : caption) swatch += p.fg256 >= 0 ? display_width(p.text) : 0;
  const std::vector<painted> c = cells_of(caption);
  std::vector<int> ramp;
  for (const painted& p : c) {
    if (p.fg >= 0) ramp.push_back(p.fg);
  }
  check(swatch == 5 && ramp == std::vector<int>({236, 60, 67, 110, 179}), "five swatches, in ramp order");

  // Legend first, then the count. The name and "highest" stay.
  const line narrow = heat_caption("threads", "64 of 64 ≥90%", true, true, 48);
  check(as_text({narrow}) == "threads · 64 of 64 ≥90% · highest of each row\n", "highest leaves no room for the legend");
  check(as_text({heat_caption("vCPUs, not Windows cores", "64 of 64 ≥90%", true, false, 49)}) == "vCPUs, not Windows cores · highest of each row\n", "the count goes before the label");
  check(has(as_text({heat_caption("vCPUs, not Windows cores", "64 of 64 ≥90%", true, false, 62)}), "64 of 64"), "with room both stay");
  check(!has(as_text({heat_caption("threads", "20 of 20 ≥90%", false, true, 42)}), "idle"), "the legend needs its whole width");
  check(has(as_text({heat_caption("threads", "20 of 20 ≥90%", false, true, 43)}), "idle"), "and has it at 43");

  // Pressure: short, then n/a where the kernel has none.
  const auto fourth = [&](const snapshot& t, int w = 100) { return columns(as_text({large(t, h, w, 34)[5]}), kRight(w)); };
  const std::string psi = "\"pressure\":{\"cpu\":4,\"memory\":0,\"io\":0}";
  snapshot p = contended();
  p.wsl_json.replace(p.wsl_json.find(psi), psi.size(), "\"pressure\":{\"cpu\":100,\"memory\":100,\"io\":100}");
  check(has(fourth(p), "· psi 100/100/100") && !has(fourth(p), "pressure"), "a tight row has the short form: " + fourth(p));
  check(has(fourth(p, 131), "· pressure cpu 100 mem 100 io 100"), "a wide one the long form: " + fourth(p, 131));
  snapshot none = contended();
  none.wsl_json.replace(none.wsl_json.find(psi), psi.size(), "\"pressure\":{\"cpu\":null,\"memory\":null,\"io\":null}");
  check(has(fourth(none), "RAM 12.3/31.2 GiB · pressure n/a"), "no PSI at all is n/a");
  snapshot part = contended();
  part.wsl_json.replace(part.wsl_json.find(psi), psi.size(), "\"pressure\":{\"cpu\":4,\"memory\":null,\"io\":0}");
  check(has(fourth(part), "pressure cpu 4 mem n/a io 0"), "one missing figure is n/a");
  snapshot high = contended();
  high.wsl_json.replace(high.wsl_json.find(psi), psi.size(), "\"pressure\":{\"cpu\":20,\"memory\":0,\"io\":0}");
  bool watched = false;
  const std::vector<line> high_frame = large(high, h);
  for (const span& q : high_frame[5]) watched = watched || (has(q.text, "cpu 20") && q.color == tone::watch);
  check(watched, "high pressure is watch");

  // Missing guest figures: an older collector's line.
  snapshot old_line = contended();
  old_line.wsl_json = busy_json();
  old_line.lost = {lost_state::no_data, kNaN};
  const std::string t = as_text(large(old_line, h));
  check(has(t, "user n/a · sys n/a · iowait n/a") && has(t, "load n/a"), "a line without the new keys is n/a, not 0");
  check(has(t, "thr") && has(t, " n/a "), "and its processes have no thread count");

  snapshot no_mem = contended();
  no_mem.memory = {};
  const std::string nm = as_text(large(no_mem, h));
  check(has(nm, "RAM n/a") && !has(nm, "free"), "unread memory is n/a");
}

void check_large_sweep() {
  for (const char* name : {"contended", "idle", "stale", "noisy"}) {
    snapshot s;
    history h;
    cpu_fixture(name, 60, s, h);
    for (const int span_s : {60, 600}) {
      const render_options o = options_for(span_s);
      for (int w = 100; w <= 300; w += 7) {
        for (int rows = 17; rows <= 66; ++rows) {
          check(frame_fits(render(s, w, rows, kNow, &h, view::cpu, o), w, rows), std::string(name) + " " + std::to_string(w) + "x" + std::to_string(rows));
        }
      }
    }
  }
  // The labels at every width this layout has, however the rows fall.
  const snapshot labelled = contended();
  const history lh = contended_history(labelled);
  for (int w = 100; w <= 300; ++w) {
    for (const int rows : {27, 34, 66}) {
      const std::string at = " at " + std::to_string(w) + "x" + std::to_string(rows);
      const std::vector<line> f = large(labelled, lh, w, rows);
      const std::string t = as_text(f);
      for (const char* label : {"whole machine, includes the WSL VM", "guest view, inside the VM", "no usage measured", "WSL VM", "vCPUs, not Windows cores", "inferred", "two systems' own views"}) {
        check(has(t, label), std::string("labelled: ") + label + at);
      }
      check(has(t, " st dxg\n") && has(t, "  S holder"), "a holder is named as one" + at);
    }
  }
  for (const int threads : {4, 8, 14, 20, 21, 40, 64, 65}) {
    snapshot s = contended(threads);
    const history h = contended_history(s);
    snapshot hostile = s;
    hostile.cpu.user_pct = 1e308;
    hostile.cpu.busy_pct = kInf;
    hostile.cpu_derived = {kInf, -1e308, 1e300, -kInf};
    hostile.vm.vmmem_cores = 1e308;
    hostile.wsl_age_s = 1e308;
    hostile.lost = {lost_state::known, 1e300};
    for (const int w : {100, 101, 113, 131, 300}) {
      for (int rows = 17; rows <= 66; rows += 3) {
        check(frame_fits(large(s, h, w, rows), w, rows), "threads " + std::to_string(threads) + " at " + std::to_string(w) + "x" + std::to_string(rows));
        check(frame_fits(large(hostile, h, w, rows), w, rows), "hostile numbers, " + std::to_string(threads) + " threads at " + std::to_string(w) + "x" + std::to_string(rows));
      }
    }
  }
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
        key('T', L't'), key('T', 0x3145), key('T', 0x14, TRUE, LEFT_CTRL_PRESSED),  // t, t under Hangul input mode, Ctrl+T
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
    check(c.poll_key() == 't', "t, for the span");
    check(c.poll_key() == 't', "t under Hangul input mode");
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

// The graph rule on the large layout's time axis (docs/design/cpu-panel.md section 5): unwatched cells are
// whole buckets before the one watching began in, on both spans, for the busy plots and the RAM plots;
// the heat block and the lanes keep blank for no measurement.
void check_graph_rule_buckets() {
  const auto is_dot = [](const cell& c) { return c.text == "·" && c.color == tone::faint; };
  const auto lead = [&](const std::vector<cell>& cs) {  // Cells before the first drawn one
    size_t i = 0;
    while (i < cs.size() && (cs[i].text == " " || is_dot(cs[i]))) ++i;
    return static_cast<int>(i);
  };
  const auto alternate = [&](const std::vector<cell>& cs, int n) {
    bool ok = n <= static_cast<int>(cs.size());
    for (int i = 0; ok && i < n; ++i) ok = i % 2 == 0 ? is_dot(cs[static_cast<size_t>(i)]) : cs[static_cast<size_t>(i)].text == " " && cs[static_cast<size_t>(i)].color == tone::faint;
    return ok;
  };
  const auto faint_dots = [](const line& l) {
    int n = 0;
    for (const span& p : l) {
      if (p.color != tone::faint) continue;
      for (size_t i = 0; i < p.text.size();) {
        const size_t from = i;
        next_cp(p.text, i);
        n += p.text.compare(from, i - from, "·") == 0;
      }
    }
    return n;
  };

  // The axis itself: the bucket watching began in holds the first sample, so is watched; every bucket
  // before it is not. kNow is a whole second, so a 60 s span over 60 cells puts it on a bucket edge.
  history none, one, edge, unplaced_only;
  check(unwatched_cells(none, make_axis(none, kNow, 60000, 60)) == 60, "on the axis, no sample: every cell unwatched");
  snapshot bad = rule_sample(kNow, 0, 0);
  bad.unix_ms = 0;
  unplaced_only.push(bad);
  check(unwatched_cells(unplaced_only, make_axis(unplaced_only, kNow, 60000, 60)) == 0, "on the axis, unplaceable samples are a gap");
  one.push(rule_sample(kNow - 9000, 0, 0));
  edge.push(rule_sample(kNow - 9001, 0, 0));
  const time_axis a1 = make_axis(one, kNow, 60000, 60), a2 = make_axis(edge, kNow, 60000, 60);
  check(unwatched_cells(one, a1) == 50 && a1.cell.size() == 1 && a1.cell[0] == 50, "unwatched stops at the first sample's bucket");
  check(unwatched_cells(edge, a2) == 49 && a2.cell.size() == 1 && a2.cell[0] == 49, "a millisecond earlier is the bucket before");
  check(unwatched_cells(one, make_axis(one, kNow + 600000, 600000, 59)) == 0, "10 minutes later nothing is unwatched");

  // A fresh start on both spans, in the large layout: dots up to the first sample's cell, both busy plots
  // and both RAM plots alike, nothing above a plot's bottom row, nothing in the heat block or the lanes.
  history fresh;
  for (int age = 9; age >= 0; --age) fresh.push(rule_sample(kNow - age * 1000, 50, age));
  for (const int span_s : {60, 600}) {
    for (const auto& [w, h] : {std::pair{100, 34}, std::pair{131, 40}, std::pair{200, 60}}) {
      const std::string n = " " + std::to_string(span_s) + " s " + std::to_string(w) + "x" + std::to_string(h);
      const std::vector<line> f = render(rule_sample(kNow, 50, 99), w, h, kNow, &fresh, view::cpu, options_for(span_s));
      if (!is_large(f)) { check(false, "the large layout draws" + n); continue; }
      const std::vector<cell> win = bottom_cells(f, 0), wsl = bottom_cells(f, 1);
      const int cells = static_cast<int>(win.size()), first = lead(win);
      const time_axis axis = make_axis(fresh, kNow, span_s * 1000LL, cells);
      check(cells > 9 && wsl.size() == win.size() && first == unwatched_cells(fresh, axis) && first == axis.cell.front() && first < cells,
            "the unwatched part ends at the first sample's bucket" + n);
      check(alternate(win, first) && alternate(wsl, lead(wsl)) && lead(wsl) == first, "a faint · in every other cell, both columns alike" + n);
      int ram_row = -1, dots = 0, bottom_dots = 0;
      for (size_t i = 0; i < f.size(); ++i) {
        const std::string t = as_text({f[i]});
        if (t.rfind("RAM │", 0) == 0) ram_row = static_cast<int>(i);
        dots += faint_dots(f[i]);
        if (t.rfind("  0 │", 0) == 0) bottom_dots += faint_dots(f[i]);
      }
      check(ram_row > 0 && faint_dots(f[static_cast<size_t>(ram_row)]) == 0, "the RAM plot's top row has no dots" + n);
      if (ram_row > 0 && static_cast<size_t>(ram_row) + 1 < f.size()) {
        const std::vector<std::vector<cell>> ram = plots_in(f[static_cast<size_t>(ram_row) + 1]);
        check(ram.size() == 2 && lead(ram[0]) == first && alternate(ram[0], first) && alternate(ram[1], first), "the RAM plots' bottom row is dotted the same" + n);
        bottom_dots += faint_dots(f[static_cast<size_t>(ram_row) + 1]);
      }
      check(dots == bottom_dots && dots == 4 * ((first + 1) / 2), "dots only on the plots' bottom rows, not the heat block or the lanes" + n);
    }
  }

  // A measured 0 for the whole 10 minutes is ⣀ in every cell of both busy plots.
  history zero;
  for (int age = 600; age >= 0; --age) zero.push(rule_sample(kNow - age * 1000, 0, age));
  const std::vector<line> zf = render(rule_sample(kNow, 0, 999), 131, 40, kNow, &zero, view::cpu, options_for(600));
  for (int g = 0; g < 2; ++g) {
    const std::vector<cell> cs = bottom_cells(zf, g);
    bool line_of_zero = !cs.empty();
    for (const cell& c : cs) line_of_zero = line_of_zero && c.text == "⣀" && c.color == tone::bar;
    check(line_of_zero, "a measured 0 over 10 minutes is ⣀ in the bar tone, graph " + std::to_string(g));
  }

  // A stall of 120 s with the wall clock 100 s behind: a gap on the minute's axis, and on 10 minutes the
  // samples keep their place, with dots only before they began.
  history partial;
  for (int age = 9; age >= 0; --age) partial.push(rule_sample(kNow - 120000 - age * 1000, 50, age));
  snapshot late = rule_sample(kNow, 50, 10);
  late.unix_ms = kNow - 100000;
  late.interval_s = 120;
  partial.push(late);
  for (const int span_s : {60, 600}) {
    const std::vector<line> f = render(late, 131, 40, kNow - 100000, &partial, view::cpu, options_for(span_s));
    const std::vector<cell> cs = bottom_cells(f, 0);
    const time_axis axis = make_axis(partial, kNow - 100000, span_s * 1000LL, static_cast<int>(cs.size()));
    const int first = lead(cs);
    if (span_s == 60) {
      check(!cs.empty() && first == static_cast<int>(cs.size()) - 1 && std::count_if(cs.begin(), cs.end(), is_dot) == 0, "a stall on the minute's axis is a gap, not dotted");
    } else {
      check(!cs.empty() && first == unwatched_cells(partial, axis) && first == axis.cell.front() && alternate(cs, first), "on 10 minutes, dots end where the shifted samples begin");
    }
  }

  // A stall of all that is kept, on 10 minutes: a gap across the axis, no dots.
  history cleared;
  for (int age = 9; age >= 0; --age) cleared.push(rule_sample(kNow - 2000000 - age * 1000, 50, age));
  snapshot after = rule_sample(kNow, 50, 10);
  after.interval_s = static_cast<double>(history::kKeepMs) / 1000.0;
  cleared.push(after);
  const std::vector<cell> cc = bottom_cells(render(after, 131, 40, kNow, &cleared, view::cpu, options_for(600)), 0);
  check(cleared.samples().size() == 1 && !cc.empty() && std::count_if(cc.begin(), cc.end(), is_dot) == 0 && lead(cc) == static_cast<int>(cc.size()) - 1,
        "a stall of what is kept is a gap across 10 minutes, not dotted");

  // The wall clock a little short of interval_s is jitter and moves nothing; past kStepMs it stepped back.
  history jitter;
  jitter.push(rule_sample(kNow, 0, 0));
  snapshot j = rule_sample(kNow + 500, 0, 1);
  j.interval_s = 1.4;
  jitter.push(j);
  check(jitter.samples()[0].unix_ms == kNow && jitter.watched_from_ms() == kNow, "a wall clock 900 ms short of interval_s moves nothing");
  snapshot k = rule_sample(kNow + 1500, 0, 2);
  k.interval_s = 2.5;
  jitter.push(k);
  check(jitter.samples()[1].unix_ms == kNow + 1500 - 2500 && jitter.samples()[0].unix_ms == kNow - 1500 && jitter.watched_from_ms() == kNow - 1500,
        "1.5 s short of interval_s is a step back: the samples and when watching began move");
}

// design/panel-grammar.md part 5: mean cells, the maximum's dot, window-wide avg and max, scale by measure,
// over 100 drawn at the ceiling and printed as measured.
void check_graph_labels() {
  const snapshot base = busy();
  const auto util_history = [&](int seconds, const std::vector<std::pair<int, double>>& spikes) {
    history h;
    for (int age = seconds; age >= 0; --age) {
      double v = 0;
      for (const auto& [at, high] : spikes) v = age == at ? high : v;
      h.push(rule_sample(kNow - age * 1000, v, age));
    }
    return h;
  };
  const auto top_row_braille = [](const std::vector<std::string>& rows) { return count_braille(rows.front()); };

  // One 100 among 0s over 10 minutes: the cell is its mean, the label its maximum, and one dot says where.
  const history spike = util_history(600, {{300, 100}});
  plotted d = plot_series(spike, &history_sample::gpu_util, kNow, 78, 600000);
  check(d.stats.max == 100 && d.stats.max_count > 1 && d.cols[static_cast<size_t>(d.stats.max_at)] < 100 && d.cols[static_cast<size_t>(d.stats.max_at)] > 0, "10 minutes: the spike's column is a mean under 100");
  std::vector<std::string> rows = plot_rows(d.cols, 12, max_mark(d.stats), d.stats.max);
  check(top_row_braille(rows) == 1 && (has(rows.front(), "⠁") || has(rows.front(), "⠈")), "and that column alone has one dot at the top");
  int upper = 0;
  for (size_t y = 0; y + 2 < rows.size(); ++y) upper += count_braille(rows[y]);
  check(upper == 1, "no other cell has a dot above its mean");
  const std::vector<std::string> unmarked = plot_rows(d.cols, 12);
  check(top_row_braille(unmarked) == 0, "without the mark the spike would read as nearly idle");
  // At 1 minute every sample has its own column: the column is the spike, and no dot is added.
  const history recent = util_history(60, {{30, 100}});
  d = plot_series(recent, &history_sample::gpu_util, kNow, 78, 60000);
  check(d.stats.max == 100 && d.stats.max_count == 1 && max_mark(d.stats) == -1, "1 minute: the maximum's column holds one sample, so no dot");
  rows = plot_rows(d.cols, 12, max_mark(d.stats), d.stats.max);
  check(rows == plot_rows(d.cols, 12) && top_row_braille(rows) == 1, "and the column itself reaches the top");
  // Two cells holding the maximum: the later.
  const history twice = util_history(600, {{400, 100}, {100, 100}});
  d = plot_series(twice, &history_sample::gpu_util, kNow, 78, 600000);
  const plotted only_later = plot_series(util_history(600, {{100, 100}}), &history_sample::gpu_util, kNow, 78, 600000);
  check(d.stats.max_at == only_later.stats.max_at && d.stats.max_at > 39, "two cells at the maximum: the later one carries the dot");
  rows = plot_rows(d.cols, 12, max_mark(d.stats), d.stats.max);
  check(top_row_braille(rows) == 1, "and only one dot is drawn");
  // A cell whose mean already reaches the maximum's dot row gets none.
  check(plot_rows({50, 50}, 1, 0, 60) == plot_rows({50, 50}, 1), "a mean at the maximum's dot row: no dot");
  check(plot_rows({50, 50}, 1, 0, 100) != plot_rows({50, 50}, 1), "a maximum above it: a dot");
  check(plot_rows({kNaN, kNaN}, 1, 0, 100) == plot_rows({kNaN, kNaN}, 1), "a gap never takes a dot");
  check(plot_rows({100, 100}, 2, 1, 300) == plot_rows({100, 100}, 2), "a maximum over the ceiling is drawn at it, not past it");
  // Through panel 2, at 10 minutes: one dot on the device graph's top row, and the label.
  render_options ten = options_for(600);
  const std::vector<line> pf = render(rule_sample(kNow, 0, 999), 131, 40, kNow, &spike, view::gpu, ten);
  const std::string top = panel_cells(upper_graph_rows(as_text(pf)).substr(0, upper_graph_rows(as_text(pf)).find('\n') + 1), 0);
  check(count_braille(top) == 1, "panel 2 at 10 minutes: the maximum's dot on the device graph's top row: " + top);
  check(has(line_with(pf, "-10m"), "max 100%"), "and the label says 100");

  // avg and max, from the samples, by hand.
  history hand;
  for (const double v : {10.0, 20.0, 30.0, 40.0}) hand.push(rule_sample(kNow - static_cast<int64_t>(4 - v / 10) * 1000, v, static_cast<int>(v)));
  const window_stats st = series_stats(hand, &history_sample::gpu_util, kNow, 60000);
  check(st.avg == 25 && st.max == 40, "avg 25 and max 40 of 10, 20, 30, 40");
  check(has(line_with(render(rule_sample(kNow, 40, 99), 131, 40, kNow, &hand, view::gpu), "-60s"), "avg 25% max 40%"), "and the device graph says so");
  check(std::isnan(series_stats(hand, &history_sample::gpu_util, kNow + 61000, 60000).avg), "a window with no sample has no figures");
  history gaps;
  for (int age = 60; age >= 0; --age) gaps.push(sample_at(bare(), kNow - age * 1000, 0, 0, 0));
  const std::string blank = as_text(render(bare(), 131, 40, kNow, &gaps, view::gpu));
  check(!has(blank, "avg") && !has(blank, "max"), "no label where every sample in the range is a gap");
  check(has(as_text(render(bare(), 99, 40, kNow, &gaps, view::cpu)), "avg") && !has(panel_cells(graph_rows(as_text(render(bare(), 99, 40, kNow, &gaps, view::cpu))), 1), "⣀"), "the Windows graph is labelled while the WSL one, with no line, is blank");

  // Over 100: drawn at the ceiling, printed as measured in the watch tone, and the split's * with its caption.
  const history demo = demo_minute(base);
  const std::vector<line> df = render(base, 131, 40, kNow, &demo, view::gpu);
  bool watched = false, plain_max = true;
  for (const line& l : df) {
    for (const span& p : l) {
      if (p.text == "112%*") watched = p.color == tone::watch;
      if (has(p.text, "max 95%") || p.text == "95%") plain_max = plain_max && p.color != tone::watch;
    }
  }
  check(watched, "demo_minute: max 112%* in the watch tone");
  check(plain_max, "a maximum under the ceiling is not");
  check(has(as_text(df), "VM level, not exact shares*") && has(as_text(df), "avg 37% max 112%*"), "the caption carries the * beside the split's label");
  const std::string panel_1 = as_text(render(base, 131, 40, kNow, &demo, view::overall));
  check(!has(panel_1, "shares*"), "panel 1 has no graph, so no *");
  history under;
  for (int age = 60; age >= 0; --age) under.push(sample_at(base, kNow - age * 1000, 50, 100, 40));
  const std::string uf = as_text(render(base, 131, 40, kNow, &under, view::gpu));
  check(!has(uf, "*") && has(uf, "max 100%"), "every split value at or under 100: no * anywhere");
  history over_windows;
  for (int age = 60; age >= 0; --age) over_windows.push(sample_at(base, kNow - age * 1000, 50, 30, age == 10 ? 104 : 40));
  const std::string ow = as_text(render(base, 131, 40, kNow, &over_windows, view::gpu));
  check(has(ow, "max 104%*") && has(ow, "shares*"), "the Windows side over 100 carries it too");
  check(!has(as_text(render(base, 131, 40, kNow, &over_windows, view::gpu, options_for(60))), "max 30%*"), "a side under 100 has no * of its own");
  history hot_power;
  for (int age = 60; age >= 0; --age) {
    snapshot p = sample_at(base, kNow - age * 1000, 50, 30, 40);
    p.gpu_device.power_w = age == 5 ? 210 : 150;
    hot_power.push(p);
  }
  bool power_watched = false;
  for (const line& l : render(base, 131, 40, kNow, &hot_power, view::gpu)) {
    for (const span& p : l) power_watched = power_watched || (p.text == "210 W" && p.color == tone::watch);
  }
  check(power_watched, "power over its limit: the watts in the watch tone, and no *, which only the split explains");
  // The memory and power labels are in their own units, from the samples, not from the heights.
  double gib_sum = 0, gib_max = 0, w_max = 0;
  int n = 0;
  for (const history_sample& p : demo.samples()) {
    gib_sum += p.gpu_mem_gib;
    gib_max = std::max(gib_max, p.gpu_mem_gib);
    w_max = std::max(w_max, p.gpu_power_w);
    ++n;
  }
  check(has(as_text(df), fmt("avg %.1f max %.1f GiB", gib_sum / n, gib_max)) && has(as_text(df), fmt("max %.0f W", w_max)), "memory in GiB and power in W, worked out by hand");
  snapshot limitless = base;
  limitless.gpu_device.power_limit_w = 0;
  limitless.unix_ms = kNow;
  history nl;
  nl.push(limitless);
  check(std::isnan(nl.samples().back().gpu_power_w) && !has(line_with(render(limitless, 131, 40, kNow, &nl, view::gpu), " W ─"), "W"), "no limit, no height: no label either");
  snapshot huge = base;
  huge.gpu_split = {true, 1e300, -5, 1, 1};
  huge.unix_ms = kNow;
  history hh;
  hh.push(huge);
  const std::string ht = as_text(render(huge, 131, 50, kNow, &hh, view::gpu));
  check(has(ht, "max 1e+300%*") && has(ht, "avg -5% max -5%"), "huge and negative figures are printed as measured: " + line_with(render(huge, 131, 50, kNow, &hh, view::gpu), "-60s"));

  // Scale by measure: 1, 2 or 5 times a power of ten, never below the floor.
  const struct { double max, floor, top; } scales[] = {{13.25, 0.05, 20}, {0.01, 0.05, 0.05}, {1, 0.05, 1}, {1.0001, 0.05, 2}, {2.5, 0.05, 5}, {7, 0.05, 10},
                                                       {0, 0.05, 0.05}, {kNaN, 0.05, 0.05}, {100, 0.05, 100}, {0.051, 0.05, 0.1}, {-3, 0.05, 0.05}, {kInf, 0.05, 0.05}};
  for (const auto& sc : scales) check(scale_top(sc.max, sc.floor) == sc.top, fmt("scale %g for %g", sc.top, sc.max));
  check(plot_rows({10, 10}, 1, -1, 0, 20) == plot_rows({50, 50}, 1), "a scaled graph draws its value against its top");

  // No minimum and no old caption anywhere; every labelled axis keeps its range and now, at every width.
  snapshot c;
  history ch;
  cpu_fixture("contended", 600, c, ch);
  for (const int64_t span_ms : kSpansMs) {
    render_options o;
    o.span_ms = span_ms;
    const std::string range = "-" + span_text(span_ms) + " ";
    for (int w = 36; w <= 300; w += 7) {
      for (const view v : {view::gpu, view::cpu}) {
        for (const auto& [sn, hn] : {std::pair<const snapshot*, const history*>{&base, &demo}, {&c, &ch}}) {
          const std::vector<line> f = render(*sn, w, 40, kNow, hn, v, o);
          const std::string t = as_text(f);
          const std::string at = " " + std::to_string(w) + " " + range;
          check(!has(t, "min ") && !has(t, "peak over mean") && !has(t, "collecting:"), "no minimum and no old caption" + at);
          for (const line& l : f) {
            const std::string row_text = as_text({l});
            if (!has(row_text, range)) continue;
            size_t ranges = 0, nows = 0;
            for (size_t i = row_text.find(range); i != std::string::npos; i = row_text.find(range, i + 1)) ++ranges;
            for (size_t i = row_text.find(" now"); i != std::string::npos; i = row_text.find(" now", i + 1)) ++nows;
            check(ranges == nows, "every axis keeps its range and now" + at);
            const size_t avg = row_text.find("avg ");
            if (avg != std::string::npos) check(row_text.find(range) < avg && avg < row_text.find(" now"), "the label sits between them" + at);
          }
        }
      }
    }
  }
}

}  // namespace

// Frame bytes, and the time to build one from a snapshot: render() and vt_frame(), no terminal.
int measure_frames() {
  for (const char* name : {"contended", "idle", "stale", "noisy"}) {
    for (const int span_s : {60, 600}) {
      for (const auto& [w, rows] : {std::pair{131, 40}, std::pair{100, 34}}) {
        snapshot s;
        history h;
        const render_options o = options_for(span_s);
        cpu_fixture(name, span_s, s, h);
        size_t bytes = 0;
        constexpr int kFrames = 300;
        const auto from = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) bytes = vt_frame(render(s, w, rows, kNow, &h, view::cpu, o), rows).size();
        const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
        printf("%-10s %3d s %dx%d  %6zu bytes  %.3f ms a frame\n", name, span_s, w, rows, bytes, took.count() / kFrames);
      }
    }
  }
  for (const int span_s : {60, 600}) {  // Panel 2: five graphs, each with its label and the maximum's dot
    snapshot s;
    history h;
    cpu_fixture("noisy", span_s, s, h);
    const render_options o = options_for(span_s);
    size_t bytes = 0;
    constexpr int kFrames = 300;
    const auto from = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) bytes = vt_frame(render(s, 131, 40, kNow, &h, view::gpu, o), 40).size();
    const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
    printf("panel 2    %3d s 131x40  %6zu bytes  %.3f ms a frame\n", span_s, bytes, took.count() / kFrames);
  }
  for (const int threads : {40, 64}) {
    const snapshot s = noisy_now(threads);
    const history h = noisy_history(s);
    printf("noisy, %d threads, 131x40  %zu bytes\n", threads, vt_frame(render(s, 131, 40, kNow, &h, view::cpu), 40).size());
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "measure") == 0) return measure_frames();
  const bool quick = argc == 2 && strcmp(argv[1], "quick") == 0;
#ifdef _WIN32
  { console c; int w = 0, h = 0; check(!c.is_terminal() || c.size(w, h), "console opens"); }
#endif
#ifdef _WIN32
  if (argc == 3) {  // The panels on the real console, keys as the program has them
    snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    history demo = demo_minute(s);
    const bool fixture = cpu_fixture(argv[1], 600, s, demo);
    console c;
    view v = fixture ? view::cpu : view::overall;
    int w = 131, h = 40;
    render_options o = options_for(60);
    for (char k = 0; k != 'q' && !console::stop_requested(); k = c.poll_key()) {
      if (k == '1') v = view::overall;
      if (k == '2') v = view::gpu;
      if (k == '3') v = view::cpu;
      c.size(w, h);
      if (k == 't' && span_key_acts(s, demo, v, w, h)) o.span_ms = next_span(o.span_ms);
      c.draw(render(s, w, h, kNow, &demo, v, o));
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
  if (argc >= 6 && strcmp(argv[1], "vt") == 0) {  // render_check vt <fixture> W H FILE [span_s]: one frame's VT bytes
    const render_options o = options_for(argc >= 7 ? atoi(argv[6]) : 60);
    snapshot s;
    history h;
    if (!cpu_fixture(argv[2], static_cast<int>(o.span_ms / 1000), s, h)) return 2;
    const int rows = atoi(argv[4]);
    const std::string vt = vt_frame(render(s, atoi(argv[3]), rows, kNow, &h, view::cpu, o), rows);
    std::ofstream out(argv[5], std::ios::binary);
    out.write(vt.data(), static_cast<std::streamsize>(vt.size()));
    out.close();
    printf("%zu bytes\n", vt.size());
    return out ? 0 : 1;
  }
  if (argc >= 4) {  // render_check <contended|idle|stale|noisy> W H [span_s]: the large layout of panel 3
    const render_options o = options_for(argc >= 5 ? atoi(argv[4]) : 60);
    snapshot s;
    history h;
    if (cpu_fixture(argv[1], static_cast<int>(o.span_ms / 1000), s, h)) {
      const std::string t = as_text(render(s, atoi(argv[2]), atoi(argv[3]), kNow, &h, view::cpu, o));
      fwrite(t.data(), 1, t.size(), stdout);
      return 0;
    }
  }
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
  check_vt_colour();
  check_buckets();
  check_scale();
  check_graph_rule();
  check_panels();
  check_large_mockup();
  check_large_allocation();
  check_large_floors();
  check_large_layouts();
  check_heat_cells();
  check_lost_text();
  check_large_states();
  check_large_lists();
  check_no_sums();
  check_large_bytes();
  check_span_key();
  check_large_captions();
  check_graph_rule_buckets();
  check_graph_labels();
  if (!quick) {
    check_denied_note();
    check_holder_wording();
    check_sizes_with_history();
    check_large_sweep();
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
