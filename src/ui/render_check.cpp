// Checks the renderer on hand-built snapshots. render_check <busy|bare> W H prints a frame as text,
// the one overall screen with no history; add overall, gpu, cpu or a panel's number 1-8 for that panel over
// a made-up minute. render_check <busy|bare|contended|idle|stale|noisy> live draws the panels on the
// console with the keys of keys.h: 1-8, ←→, ↑↓, tab, esc, q.
// render_check <busy|bare> draws the plain screen on the console until Ctrl+C.
// render_check <contended|idle|stale|noisy> W H [span_s] prints the large layout of panel 3.
// render_check vt <fixture> W H FILE [span_s] writes one frame's VT bytes to FILE, to render as an image.
// render_check measure prints frame bytes and the time to build a frame.
// render_check quick runs everything but the size sweeps, about a second, for mutating the renderer.
// Build: scripts/build.sh src/ui/render_check.cpp   (it includes the sources it tests)
#include "ui/render.cpp"
#include "ui/history.h"
#include "ui/keys.h"
#include "core/wsl_view.cpp"
#include "analysis/process_totals.h"
#ifdef _WIN32
#include "ui/console.cpp"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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
         "\",\"rss\":" + std::to_string((27 - i) * 90000000ll) + ",\"cores\":" + std::to_string((26 - i) * 0.04) + ",\"rated\":true,\"on_gpu\":false},";
  }
  p += R"({"pid":4121,"name":"python3","state":"R","rss":6442450944,"cores":3.21,"rated":true,"on_gpu":true},)"
       R"({"pid":4388,"name":"한글추론기","state":"S","rss":2147483648,"cores":0.1,"rated":true,"on_gpu":true},)"
       R"({"pid":4400,"name":"\u001b]0;pwned\u0007\u001b[31mevil","state":"S","rss":1000,"cores":0.0,"rated":true,"on_gpu":false})";
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
    p.pid = pid; p.name = name; p.cores = cores; p.working_set = ws; p.cores_known = true;
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
  s.gpu_engines_known = true;
  s.gpu_device = {true, "NVIDIA GeForce RTX 4070", true, true, true, true, true, 91, 187, 200, 71, 2730, 9 * kG, 12 * kG};
  s.gpu_memory = {{11736, 4509715660, 100000, 4509715660}, {9002, 1503238553, 200000, 1503238553}};
  s.gpu_split = {true, 52, 36, 4509715660, 1503238553};
  s.wsl_json = busy_json();
  s.wsl_age_s = 0.17;
  s.wsl_clock_known = false;
  s.process_totals = process_totals_of(s);  // As main.cpp fills it, from the rows
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
  p.pid = 812; p.name = "explorer.exe"; p.cores = 0.06; p.working_set = 180000000; p.cores_known = true;
  s.processes.push_back(p);
  s.disks = {{"0 C:", 0, 4096, 1}};
  s.nets = {{"이더넷", 0, 0, 1000000000, true}};
  s.process_totals = process_totals_of(s);
  return s;
}

// The panel and range a caller of the view_state overload would ask for.
view_state state_for(int panel, int span_s = 60) {
  view_state v;
  v.panel = panel;
  v.range = span_s >= 600 ? 2 : span_s >= 300 ? 1 : 0;
  return v;
}

bool has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }
int count_of(const std::string& text, const std::string& what) {
  int n = 0;
  for (size_t i = text.find(what); i != std::string::npos; i = text.find(what, i + what.size())) ++n;
  return n;
}
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

  // rated and start, additive keys of schema 1 (PROTOCOL.md, processes): a row without them, from an older
  // collector, is not rated and has no identity beyond its pid.
  v = read_wsl(R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"rated":true,"start":98765432},{"pid":2,"rated":false,"start":null},{"pid":3,"rated":null},{"pid":4}]})");
  check(v.ok && v.processes.size() == 4, "reads rated and start");
  check(v.processes[0].rated && v.processes[0].start == 98765432u, "rated true, and start's value");
  check(!v.processes[1].rated && !v.processes[1].start, "rated false, and a null start is no identity");
  check(!v.processes[2].rated, "a null rated is not rated");
  check(!v.processes[3].rated && !v.processes[3].start, "absent, neither: an older collector's row is not rated");
  check(read_wsl(busy_json()).processes[0].rated && !read_wsl(busy_json()).processes[0].start, "the busy line, rated with no start, reads no identity beyond pid");
  // start is an identity: out of range, fractional or null leaves it empty and the line still reads; a string refuses it.
  for (const char* empty : {"-5", "1e300", "1.5", "18446744073709551616", "null"}) {
    const wsl_view e = read_wsl(std::string(R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"start":)") + empty + "}]}");
    check(e.ok && e.processes.size() == 1 && !e.processes[0].start, std::string("start ") + empty + " is no identity, and the line is read");
  }
  for (const auto& [text, value] : {std::pair<const char*, uint64_t>{"0", 0}, {"616497", 616497}, {"9223372036854775808", 9223372036854775808ull}}) {
    const wsl_view e = read_wsl(std::string(R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"start":)") + text + "}]}");
    check(e.ok && e.processes.size() == 1 && e.processes[0].start == value, std::string("start ") + text + " is kept");
  }
  for (const char* bad : {R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"rated":1}]})", R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"rated":"true"}]})",
                          R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"rated":tru}]})", R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"start":"5"}]})",
                          R"({"schema":1,"busy_pct":1,"memory":{},"processes":[{"pid":1,"start":true}]})"}) {
    check(!read_wsl(bad).ok, std::string("a rated that is not a bool, or a start that is not a number, refuses the line: ") + bad);
  }

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
  check(known_columns(util_columns(h)) == 0, "60 s ago is outside: a sample stands for the second before it");
  h = history();
  h.push(sample_at(base, kNow - 59900, 40, 1, 1));
  c = util_columns(h);
  check(!std::isnan(c[0]) && known_columns(c) == 1, "just inside is the left edge");
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
  const std::string t = as_text(render(stale, 99, 40, kNow, &h, state_for(3)));
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
  check(count_braille(graph_rows(as_text(render(busy(), 131, 40, kNow - 7200000, &h, state_for(2))))) > 0, "drawing behind the newest sample still draws");

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
  const std::string cells = graph_rows(as_text(render(unreadable, 99, 30, kNow, &h, state_for(3))));
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
  const std::vector<double> ten = plot_columns(h, &history_sample::gpu_util, kNow, kDots, 600000);
  check(std::isnan(ten[kDots / 3 - 6]) && !std::isnan(ten[kDots / 3 + 6]), "and 400 s of 600 fill two thirds of it");
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
  check(one.wsl_busy == 11.0f, "the guest's busy");

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
  check(h.samples().back().windows_threads == 0 && std::isnan(h.samples().back().windows_core[0]), "a failed read keeps no 0");

  // A repeated line carries for one sample in these fields, and is still a gap in the old one.
  h = history();
  h.push(at(base, kNow - 3000, 11));
  h.push(at(base, kNow - 2000, 11));
  check(h.samples().back().wsl_busy == 11.0f && h.samples().back().wsl_core[6] == 97.0f, "one repeat carries the last value");
  check(std::isnan(h.samples().back().wsl_cpu), "and is still a gap for the small layout and panel 2");
  h.push(at(base, kNow - 1000, 11));
  check(std::isnan(h.samples().back().wsl_busy) && std::isnan(h.samples().back().wsl_core[6]) && h.samples().back().wsl_threads == 0, "two repeats in a row are a gap");
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
  check(make_axis(h, kNow, 600000, 43).bucket_ms == 13954, "the span is an argument");
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

  // Cells narrower than a second: what is between two close samples takes the later one, which stands for it.
  h = bucket_history({{1000, 70}, {0, 30}});
  a = make_axis(h, kNow, 60000, 240);
  check(a.bucket_ms == 250, "4 cells a second");
  std::vector<double> cells = bucket_cells(a, windows_busy);
  check(cells[239] == 30 && cells[238] == 30 && cells[236] == 30 && cells[235] == 70 && std::isnan(cells[234]), "a wide terminal does not stripe, and the cells between take the later sample");
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
  check(known_columns(bucket_cells(make_axis(h, kNow, 600000, 30), windows_busy)) == 1, "a gap inside a 20 s cell is not visible");

  // The left edge: the sample before it joins, and nothing is drawn from it; the first sample inside covers it.
  h = bucket_history({{60500, 70}, {59500, 20}});
  cells = bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy);
  check(cells[0] == 20 && cells[1] == 20 && known_columns(cells) == 2, "the left edge takes the first sample inside, joined to the one before");
  h = bucket_history({{61500, 10}, {60500, 70}, {59500, 20}});
  check(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)[0] == 20, "joined through the newest sample before it");
  h = bucket_history({{61500, 10}, {59500, 20}});
  check(std::isnan(bucket_cells(make_axis(h, kNow, 60000, 240), windows_busy)[0]), "a sample before it too far back to join leaves the edge blank");
  h = bucket_history({{60000, 99}, {1000, 30}});
  const time_axis edge_axis = make_axis(h, kNow, 60000, 60);
  check(bucket_stats(edge_axis, windows_busy).max == 30 && known_columns(bucket_cells(edge_axis, windows_busy)) == 1, "a sample at exactly -60 s is outside the large layout's window too");
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
  const std::string t = as_text(render(s, 131, 50, kNow, &h, state_for(2)));
  check(has(t, "112%  WSL VM"), "the printed number stays as measured");

  s = busy();
  s.gpu_split = {true, 1e300, -5, 1, 1};
  s.unix_ms = kNow;
  history huge;
  huge.push(s);
  check(plot_columns(huge, &history_sample::vm_pct, kNow, kDots)[kDots - 1] == 100 && plot_columns(huge, &history_sample::windows_pct, kNow, kDots)[kDots - 1] == 0, "out of range clamps both ways");
  check(has(as_text(render(s, 131, 50, kNow, &huge, state_for(2))), "1e+300%"), "a huge number is printed, short enough to fit");

  for (const view_state& v : {state_for(2), state_for(3)}) {
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
  const struct { view_state v; int graphs; const char* name; } panels[] = {{state_for(2), 5, " gpu"}, {state_for(3), 2, " cpu"}};

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
  const std::string bare_gpu = graph_rows(as_text(render(bare(), 131, 40, kNow, &watched_bare, state_for(2))));
  check(!has(bare_gpu, "·") && count_braille(bare_gpu) == 0, "watched with nothing measured is blank, not dotted");
  const std::string bare_wsl = panel_cells(graph_rows(as_text(render(bare(), 131, 40, kNow, &watched_bare, state_for(3)))), 1);
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
  check(dots_in(bottom_cells(render(stall, 131, 40, kNow - 7200000, &stepped, state_for(2)), 0)) == 0, "after a long stall no dots");
  // A stall of all that is kept clears the samples, and every span before it is watched with nothing measured.
  snapshot clear = rule_sample(kNow - 9000000, 0, 13);
  clear.interval_s = static_cast<double>(history::kKeepMs) / 1000.0;
  stepped.push(clear);
  check(stepped.samples().size() == 1 && stepped.watched_from_ms() <= kNow - 9000000 - history::kKeepMs && unwatched_cells(stepped, kNow - 9000000, kCells) == 0,
        "a stall that clears the history is a gap, not unwatched");
  check(dots_in(bottom_cells(render(clear, 131, 40, kNow - 9000000, &stepped, state_for(2)), 0)) == 0, "after a stall of what is kept no dots");
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
  const struct { view_state v; int whole, partial; const char* name; } pairs[] = {{state_for(2), 1, 0, " device"}, {state_for(3), 0, 1, " WSL CPU"}};
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
    check(as_text(render(s, 131, 50, kNow, nullptr, state_for(2))) == plain && as_text(render(s, 131, 50, kNow, nullptr, state_for(3))) == plain, "no history, no panels");
    check(!has(plain, "q quit") && !has(plain, "1 overview"), "no history, no footer");
    // The overall panel is the screen without a history, one row shorter, then the footer.
    const auto with = render(s, 131, 50, kNow, &demo, state_for(1));
    const auto without = render(s, 131, 49, kNow);
    check(with.size() == without.size() + 1 && as_text({with.begin(), with.end() - 1}) == as_text(without), "the overall panel is the screen without a history");
    check(count_braille(graph_rows(as_text(with))) == 0 && !has(as_text(with), "-60s"), "the overall panel has no graphs");
  }

  const auto footer_of = [&](const view_state& v, int w = 131, int h = 50) {
    const auto f = render(full, w, h, kNow, &demo, v);
    return f.back();
  };
  const struct { view_state v; const char* name; } keys[] = {{state_for(1), "1 overview"}, {state_for(2), "2 gpu"}, {state_for(3), "3 cpu"}};
  for (const auto& k : keys) {
    const line f = footer_of(k.v);
    const std::string text = as_text({f});
    check(has(text, "1 overview  2 gpu  3 cpu  4 mem") && has(text, "q quit"), "the footer names the panels and q");
    for (const auto& o : keys) {
      bool on = false;
      for (const span& p : f) on = on || (has(p.text, o.name) && p.bold && p.color == tone::title);
      check(on == (o.v.panel == k.v.panel), std::string("only the active panel is highlighted: ") + o.name);
    }
    check(render(full, 131, 50, kNow, &demo, k.v).size() <= 50, "the footer is counted in height");
    check(has(as_text({render(full, 131, 1, kNow, &demo, k.v).back()}), k.name), "one row is the footer");
  }

  // GPU panel labels, at a width where each title is on one line, then wrapped.
  const std::string g = as_text(render(full, 200, 60, kNow, &demo, state_for(2)));
  for (const char* label : {"device · NVML", "WSL VM · engine counters, VM level, not exact shares", "Windows · engine counters", "GPU memory · NVML, used of total", "power · NVML, of limit", "/dev/dxg holders, no usage measured"}) {
    check(has(g, label), std::string("labelled: ") + label);
  }
  for (const int w : {40, 80, 112, 131}) {
    const std::string t = as_text(render(full, w, 66, kNow, &demo, state_for(2)));
    const std::string n = " at " + std::to_string(w);
    check(has(t, "device · NVML") && has(t, "engine counters,") && has(t, "VM level, not exact shares") && has(t, "Windows · engine counters"), "wrapped titles stay whole" + n);
    check(has(t, "dxg_denied 36") && has(t, "no usage measured"), "holders carry no usage" + n);
    check(!has(t, "88%"), "52 + 36 is never shown" + n);
    check(has(t, "unexplained") && !has(t, "different measures") && !has(t, "expected to match"), "device and engine: only that the difference is unexplained" + n);
  }
  check(has(g, "9.0/12.0 GiB") && has(g, "187/200 W") && !has(g, "75%") && !has(g, "94%"), "bytes and watts stay in their units");

  // The GPU lists: Windows by GPU%, holders apart, no Linux figure.
  const auto frame = render(full, 131, 50, kNow, &demo, state_for(2));
  bool seen_right = false;
  size_t vmwp = 0, game = 0, n = 0;
  for (const line& l : frame) {
    const std::string t = as_text({l});
    ++n;
    if (has(t, "vmwp.exe") && !vmwp) vmwp = n;  // The list's row, not the history's title under it
    if (has(t, "Game.Shipping") && !game) game = n;
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
  const std::string rs = as_text(render(resort, 131, 50, kNow, &demo, state_for(2)));
  check(rs.find("Game.Shipping") < rs.find("vmwp.exe"), "the GPU list follows GPU%, not cores");
  check(!has(as_text(render(full, 131, 50, kNow, &demo, state_for(2))), "svchost.exe"), "no process without GPU use in that list");
  const std::string stacked = as_text(render(full, 60, 66, kNow, &demo, state_for(2)));
  check(stacked.rfind("vmwp.exe", stacked.find("Windows · vmwp.exe")) < stacked.find("한글추론기", stacked.find("PROCESSES")), "stacked GPU lists do not interleave");

  // The CPU panel's small layout, which is every window under 100 columns.
  for (const int w : {80, 90, 99}) {
    const std::string t = as_text(render(full, w, 60, kNow, &demo, state_for(3)));
    const std::string where = " at " + std::to_string(w);
    check(has(t, "whole machine, includes the WSL VM") && has(t, "guest view, inside the VM"), "CPU graphs name their source" + where);
    check(has(t, "hottest core 3 at 96%") && has(t, "hottest core 6 at 97%"), "both sets of core rows" + where);
    check(!has(t, "62%") && !has(t, "61.6"), "Windows and WSL busy are not added" + where);
    check(has(t, "top ") && has(t, "ollama") && has(t, "svchost.exe"), "both process lists" + where);
  }
  // The title rows themselves: the WINDOWS and WSL rule rows hold the same words.
  for (const int w : {131, 200}) {  // 26 rows are under the large layout's floor, at any width
    const auto f = render(full, w, 26, kNow, &demo, state_for(3));
    const std::string title = line_with(f, "%  Windows CPU");
    check(has(title, "Windows CPU · whole machine, includes the WSL VM"), "the Windows graph title names its source");
    check(has(title, "WSL CPU · guest view, inside the VM"), "the WSL graph title names its source");
    check(has(as_text(f), "two systems' own views of overlapping work, never added"), "the CPU rule says the views overlap");
  }
  const auto wrapped = render(full, 80, 60, kNow, &demo, state_for(3));
  const std::string second = line_with(wrapped, "%  Windows CPU", 1);
  check(has(second, "includes the WSL VM") && has(second, "guest view, inside the VM"), "wrapped titles keep their source");
  // The small layout stacks the two lists, Windows first, never interleaved (rule 3).
  const std::string cpu = as_text(render(full, 131, 26, kNow, &demo, state_for(3)));
  check(has(cpu, "System") && has(cpu, "ollama") && cpu.rfind("System") < cpu.find("WSL  by cores") && cpu.find("WSL  by cores") < cpu.find("ollama"), "CPU panel: the stacked lists do not interleave");

  // Missing is n/a, in every panel, and history does not fill in for it.
  const std::string bg = as_text(render(bare(), 131, 50, kNow, &demo_bare, state_for(2)));
  for (const char* v : {"n/a  device · NVML", "n/a  WSL VM ·", "n/a  Windows · engine counters", "n/a  GPU memory", "n/a  power"}) check(has(bg, v), std::string("missing: ") + v);
  check(count_braille(graph_rows(bg)) == 0, "no measurement, a blank graph");
  check(has(bg, "device  unavailable") && has(bg, "unavailable: no GPU engine data") && has(bg, "unavailable: no WSL data"), "bare GPU panel says what is missing");
  check(!has(bg.substr(0, bg.find("PROCESSES")), " 0%"), "the GPU panel shows no zero");

  const std::string bc = as_text(render(bare(), 99, 50, kNow, &demo_bare, state_for(3)));
  check(has(bc, "n/a  WSL CPU") && has(bc, "WSL data unavailable"), "no WSL line is n/a");
  check(!has(bc, "  0%  WSL"), "no WSL line is not 0");
  snapshot no_cpu = bare();
  no_cpu.cpu = {};
  check(has(as_text(render(no_cpu, 99, 50, kNow, &demo_bare, state_for(3))), "n/a  Windows CPU"), "a failed CPU read is n/a");
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
  const std::string pt = as_text(render(part, 131, 50, kNow, &demo, state_for(2)));
  for (const char* v : {"n/a  device · NVML", "n/a  WSL VM", "n/a  Windows ·", "n/a  GPU memory", "n/a  power"}) check(has(pt, v), std::string("has_* false: ") + v);
  part = busy();
  part.gpu_device.power_limit_w = 0;
  part.unix_ms = kNow;
  history nl;
  nl.push(part);
  check(has(as_text(render(part, 131, 50, kNow, &nl, state_for(2))), "187 W") && std::isnan(nl.samples().back().gpu_power_pct), "no limit: watts printed, no height invented");

  // Blank with little history.
  const std::string eg = as_text(render(full, 131, 50, kNow, &empty, state_for(2)));
  check(count_braille(graph_rows(eg)) == 0 && has(eg, "-60s") && has(eg, "91%"), "an empty history is blank graphs and the live number");
  check(has(graph_rows(eg), "· · ·"), "an empty history is not watched yet: dotted, not a gap");

  // Short windows: graphs shrink, then lower blocks go, the footer stays.
  const std::string tall = as_text(render(full, 131, 50, kNow, &demo, state_for(2)));
  check(has(tall, "device  util") && has(tall, "PROCESSES") && !has(tall, "rows hidden"), "a tall window shows everything");
  const std::string mid = as_text(render(full, 131, 12, kNow, &demo, state_for(2)));
  check(has(mid, "100│") && has(mid, "some rows hidden") && has(mid, "q quit"), "a short window says it dropped something");
  panel one{{"title"}, "1%", tone::text, &history_sample::gpu_util};
  const std::vector<panel> pair = {one, one};
  std::vector<grid_row> fitted;
  add_panels(fitted, pair, 131);
  const int whole = rows_height(fitted);
  check(!fit_rows(fitted, whole) && fitted[0].plot, "what fits is not hidden");
  check(fit_rows(fitted, whole - 1) && !fitted[0].plot, "a plot that does not fit is reported");
  // The rule row is the first to go; it counts as hidden too.
  const auto no_rule = render(full, 80, 23, kNow, &demo, state_for(2));
  check(!has(as_text(no_rule), "GPU ─") && has(as_text(no_rule), "vmwp.exe") && marks_hidden(no_rule), "a dropped GPU rule is marked");
  // Too narrow for the note: a marker the right edge cannot cut.
  for (const int w : {1, 10, 36, 40, 48, 59}) {
    for (const view_state& v : {state_for(2), state_for(3)}) {
      const auto f = render(full, w, 8, kNow, &demo, v);
      check(as_text({f.back()}).rfind("…", 0) == 0, "hidden is marked at width " + std::to_string(w));
    }
  }
  check(has(as_text({render(full, 131, 8, kNow, &demo, state_for(2)).back()}), "8 vm cost   … some rows hidden"), "a footer with the panels' names holds the note");
  check(!marks_hidden(render(full, 40, 66, kNow, &demo, state_for(3))), "nothing hidden, no marker");

  // /dev/dxg holders: a count, and the warning whenever the block is drawn.
  const snapshot many = with_holders(full, 22);
  const auto crowded = render(many, 131, 50, kNow, &demo, state_for(2));
  check(has(as_text(crowded), "of 22") && has(as_text(crowded), "dxg_denied 36") && marks_hidden(crowded), "a cut holder list counts itself");
  check(has(as_text(render(full, 131, 50, kNow, &demo, state_for(2))), "top 2 of 2"), "a whole holder list counts too");
  for (const int w : {30, 40, 50, 63, 64, 80, 100, 131, 200}) {
    for (int rows = 1; rows <= 66; ++rows) {
      const auto f = render(many, w, rows, kNow, &demo, state_for(2));
      const std::string text = as_text(f);
      const std::string at = " at " + std::to_string(w) + "x" + std::to_string(rows);
      if (!has(text, "no usage measured")) continue;
      check(has(text, "dxg_denied 36"), "the warning survives" + at);
      check(has(text, "of 22"), "the count survives" + at);
      check(marks_hidden(f), "the cut is marked" + at);
      check(!has(text, "top 0 of 22"), "a drawn list has a name in it" + at);
    }
  }
  check(!has(as_text(render(no_wsl(), 131, 50, kNow, &demo, state_for(2))), "dxg_denied"), "no WSL line, no warning row");
  bool no_room_cut = false;
  const wsl_view many_view = read_wsl(many.wsl_json);
  check(has(as_text(dxg_holder_list(many_view, 60, 0, no_room_cut)), "dxg_denied 36") && no_room_cut, "the warning needs no name row");

  // An unreadable line that names dxg_denied is no WSL data: it reserves no warning row.
  snapshot unread = busy();
  unread.wsl_json = R"({"schema":2,"busy_pct":1,"memory":{},"dxg_denied":36})";
  for (const int w : {60, 131}) {
    for (int rows = 1; rows <= 66; ++rows) {
      const bool drawn = has(as_text(render(unread, w, rows, kNow, &demo, state_for(2))), "PROCESSES");
      check(drawn == has(as_text(render(no_wsl(), w, rows, kNow, &demo, state_for(2))), "PROCESSES"), "no warning row without WSL data, " + std::to_string(w) + "x" + std::to_string(rows));
    }
  }

  // More GPU users than rows: a cut Windows list is marked, and stacked lists split the rows.
  const auto shown_of = [](const std::string& t, const char* tail) {  // The Windows list's own `N of M` line
    const size_t end = t.find(tail);
    return end == std::string::npos ? -1 : atoi(t.c_str() + t.rfind('\n', end) + 1);
  };
  snapshot users = with_holders(full, 22);
  users.gpu_engines.clear();
  for (uint32_t i = 0; i < 15; ++i) users.gpu_engines.push_back({10000 + i * 4, "3D", 50.0 - i});
  snapshot few_holders = users;
  few_holders.wsl_json = full.wsl_json;
  const auto cut_users = render(few_holders, 131, 50, kNow, &demo, state_for(2));
  // 15 using an engine and the two holding local memory, which the list now holds too (Q4).
  check(shown_of(as_text(cut_users), " of 17 ") > 0 && shown_of(as_text(cut_users), " of 17 ") < 17 && has(as_text(cut_users), "top 2 of 2"), "a cut Windows list says N of 17 under it");
  const auto top_of = [](const std::string& t, const char* tail) {
    const size_t end = t.find(tail);
    return end == std::string::npos ? -1 : atoi(t.c_str() + t.rfind("top ", end) + 4);
  };
  int odd = 0;
  for (int rows = 1; rows <= 66; ++rows) {
    const std::string t = as_text(render(users, 60, rows, kNow, &demo, state_for(2)));
    const int windows = shown_of(t, " of 17\n"), holders = top_of(t, " of 22");
    if (windows < 0 || holders < 0) continue;
    check(windows - holders == 0 || windows - holders == 1, "stacked lists split the rows evenly at 60x" + std::to_string(rows) + ": " + std::to_string(windows) + "," + std::to_string(holders));
    odd += windows - holders;
  }
  check(odd > 0, "an odd row goes to the first list");

  // A process at zero is no GPU use.
  snapshot zero = busy();
  zero.gpu_engines.push_back({4, "3D", 0});
  const auto zf = render(zero, 131, 50, kNow, &demo, state_for(2));
  check(has(as_text(zf), "\n2 of 2 ") && !has(as_text(zf), "System"), "an engine at 0% is not a GPU user; pid 5120, in no process row or slot, is not listed");

  // Without a limit the watts stand alone and the title says why the graph is blank.
  snapshot limitless = busy();
  limitless.gpu_device.power_limit_w = 0;
  check(has(as_text(render(limitless, 200, 50, kNow, &empty, state_for(2))), "no limit reported, not graphed"), "no limit is named in the title");
  const std::string low = as_text(render(full, 131, 6, kNow, &demo, state_for(2)));
  check(has(low, "91%  device · NVML") && has(low, "52%  WSL VM") && has(low, "q quit") && !has(low, "100│"), "a very short window keeps the values and labels");
  const std::string narrow_low = as_text(render(full, 50, 12, kNow, &demo, state_for(2)));
  check(has(narrow_low, "100│") && has(narrow_low, "36%  Windows ·") && marks_hidden(render(full, 50, 12, kNow, &demo, state_for(2))), "a stack that cannot fit keeps every value it can");
  const std::string cpu_low = as_text(render(full, 80, 24, kNow, &demo, state_for(3)));
  check(has(cpu_low, "svchost.exe") || has(cpu_low, "vmmemWSL"), "at 80x24 the CPU panel's Windows list has rows");
  const std::string gpu_low = as_text(render(full, 80, 24, kNow, &demo, state_for(2)));
  check(has(gpu_low, "vmwp.exe") && has(gpu_low, "python3") && has(gpu_low, "dxg_denied"), "at 80x24 the GPU panel's lists have names");

  // Names, as a Windows GPU user and as a /dev/dxg holder.
  snapshot evil = busy();
  evil.gpu_engines.push_back({9003, "3D", 60});
  const std::string pids = "\"dxg_pids\":[4121,4388";
  evil.wsl_json.replace(evil.wsl_json.find(pids), pids.size(), pids + ",4400");
  for (const view_state& v : {state_for(1), state_for(2), state_for(3)}) {
    check(vt_clean(vt_frame(render(evil, 131, 50, kNow, &demo, v), 50)), "names stay safe in every panel");
    check(vt_clean(vt_frame(render(evil, 60, 66, kNow, &demo, v), 66)), "names stay safe when stacked");
  }
  check(has(as_text(render(evil, 131, 50, kNow, &demo, state_for(2))), "bad.exe"), "the hostile name is on the panel");
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
      const std::string gpu = row_of(as_text(render(s, w, 66, kNow, &demo, state_for(2))), "dxg_denied");
      const std::string overall = row_of(as_text(render(s, w, 66, kNow, &demo, state_for(1))), "dxg_denied");
      check(ends(gpu, static_cast<int>(full.size()) <= list ? full : brief), "the warning is whole in the GPU panel" + at);
      check(ends(overall, static_cast<int>(full.size()) <= w - 8 ? full : brief), "the warning is whole in the overall panel" + at);
    }

    // The clear wording where it has room, on the frames people read.
    for (const int w : {80, 100, 131, 200}) {
      const std::string at = " at " + std::to_string(w) + "x24, " + head;
      check(ends(row_of(as_text(render(s, w, 24, kNow, &demo, state_for(2))), "dxg_denied"), full), "the clear wording fits the GPU panel" + at);
      check(ends(row_of(as_text(render(s, w, 24, kNow, &demo, state_for(1))), "dxg_denied"), full), "the clear wording fits the overall panel" + at);
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
        for (const view_state& v : {state_for(1), state_for(2), state_for(3)}) frames += as_text(render(*s, w, rows, kNow, &demo, v));
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
      for (const view_state& v : {state_for(1), state_for(3)}) {
        const std::string t = as_text(render(*s, w, 66, kNow, &demo, v)), at = " at " + std::to_string(w) + " view " + std::to_string(v.panel);
        const std::string head = after_bar(row_of(t, " of 64,"));
        if (head.empty() || has(t, "vCPUs, not Windows cores")) continue;  // No room for the list; the large layout has its own check
        check(head.size() >= 18 && head.compare(head.size() - 18, 18, " no usage measured") == 0, "the WSL list says no usage is measured" + at);
        const std::string below = t.substr(t.find(" of 64,"));
        const std::string cols = after_bar(row_of(below, "stat rss GiB"));
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
  check(has(head_at(100), "WSL Ubuntu-24.04  top 3 of 64?, /dev/dxg holders first, then cores, no usage measured\n"), "the longest wording where it fits");
  check(has(head_at(64), "WSL Ubuntu-24.04  top 3 of 64?, holders first, no usage measured\n"), "the 131-column list keeps the order");
  check(has(head_at(49), "WSL Ubuntu…  top 3 of 64?, dxg: no usage measured\n"), "the distro gives way to the label");
}

void check_sweep(const snapshot& s, const history& h, const char* name, int step = 3) {
  for (const view_state& v : {state_for(1), state_for(2), state_for(3)}) {
    for (const int w : sizes(300, step)) {
      for (const int rows : sizes(66, step)) {
        const auto frame = render(s, w, rows, kNow, &h, v);
        bool fits = static_cast<int>(frame.size()) <= rows && !frame.empty();
        for (const line& l : frame) {
          int cols = 0;
          for (const span& p : l) cols += display_width(p.text);
          fits = fits && cols <= w;
        }
        check(fits, std::string(name) + " view " + std::to_string(v.panel) + " " + std::to_string(w) + "x" + std::to_string(rows));
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
    R"({"pid":4121,"name":"cpu_burn","state":"R","rss":107374182,"cores":19.72,"threads":21,"rated":true,"on_gpu":false},)"
    R"({"pid":4388,"name":"python3","state":"S","rss":2147483648,"cores":0.1,"threads":12,"rated":true,"on_gpu":true},)"
    R"({"pid":2000,"name":"node","state":"S","rss":429496729,"cores":0.04,"threads":11,"rated":true,"on_gpu":false},)"
    R"({"pid":2001,"name":"sshd","state":"S","rss":10000000,"cores":0.03,"threads":1,"rated":true,"on_gpu":false},)"
    R"({"pid":2002,"name":"systemd","state":"S","rss":10000000,"cores":0.02,"threads":1,"rated":true,"on_gpu":false},)"
    R"({"pid":2003,"name":"dbus-daemon","state":"S","rss":10000000,"cores":0.01,"threads":1,"rated":true,"on_gpu":false})";

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
    p.pid = pid; p.name = name; p.cores = cores; p.threads = thr; p.working_set = static_cast<uint64_t>(ws * static_cast<double>(kG)); p.cores_known = true;
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
  s.process_totals = process_totals_of(s);
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
  // The burners are quiet too, so a process's history steps where the busy plots do (design part 3, Mockups).
  for (process_row& p : s.processes) {
    if (p.name == "cpu_burn.exe") p.cores = 0;
    if (p.pid == s.vm.vmmem_pid) p.cores = s.vm.vmmem_cores;
  }
  s.process_totals = process_totals_of(s);
  std::string quiet = kCpuProcesses;
  const std::string busy_burner = "\"cores\":19.72";
  if (const size_t at = quiet.find(busy_burner); at != std::string::npos) quiet.replace(at, busy_burner.size(), "\"cores\":0");
  s.wsl_json = guest_line(std::vector<double>(vcpus, 2.0), g, quiet);
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


// ---- The large layout of panel 3, against docs/design/cpu-panel.md -----------------------------------

std::vector<line> large(const snapshot& s, const history& h, int w = 100, int rows = 34, const view_state& o = state_for(3)) {
  return render(s, w, rows, kNow, &h, o);
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
  row_is(4, "VM ran 5.76 of 20 threads · ≈41 ready, 13 VM     │ WSL short ≈14 of 20 vCPUs, inferred · load? 19.8");
  check(columns(as_text({f[5]}), 0, 5) == "100 │" && columns(as_text({f[5]}), 51, 5) == "100 │", "the busy plots start where the 3 heads end");
  check(columns(as_text({f[7]}), 0, 5) == "  0 │" && columns(as_text({f[7]}), 51, 5) == "  0 │", "busy is 3 rows at 34");
  check(!has(text, "RAM"), "RAM has left panel 3 for panel 4");
  check(columns(as_text({f[8]}), 0, 5) == "kern│" && columns(as_text({f[9]}), 0, 5) == "VM  │", "the Windows lanes");
  check(columns(as_text({f[8]}), 51, 5) == "iowt│" && columns(as_text({f[9]}), 51, 5) == "lost≈", "the WSL lanes, and lost's gutter is ≈");
  check(columns(as_text({f[10]}), 0, 24) == "threads · 20 of 20 ≥90% " && has(as_text({f[10]}), "idle █████ ≥90%"), "the caption's count and its five swatches");
  check(columns(as_text({f[10]}), 51) == "vCPUs, not Windows cores · 20 of 20 ≥90%", "the block says a vCPU is not a core");
  int n = 11;
  for (const char* left : {"P0", "P1", "P2", "P3", "P4", "P5", "E0", "E2", "E4", "E6"}) {
    check(columns(as_text({f[static_cast<size_t>(n)]}), 0, 2) == left, std::string("row ") + left);
    ++n;
  }
  for (int r = 0; r < 10; ++r) check(columns(as_text({f[static_cast<size_t>(11 + r)]}), 51, 5) == pad("v" + std::to_string(2 * r), 4) + "│", "row v" + std::to_string(2 * r));
  check(columns(as_text({f[21]}), 5) == "-60s ───── busy avg 61% max 100% ────── now │      -60s ───── busy avg 61% max 100% ────── now", "the axis under both blocks, with each busy plot's average and maximum");
  // design/panel-grammar.md part 3, the 100 by 34 mockup, with experiment 06's labels (part 1's table).
  row_is(22, "Windows  by cores                                │ WSL  by cores · dxg: no usage measured");
  row_is(23, "   cores     pid name          thr WS GiB        │    cores     pid name  thr stat rss GiB st dxg");
  row_is(24, "▶  13.25   31000 cpu_burn.exe   21    0.1        │    19.72    4121 cpu_…  21          0.1  R");
  row_is(25, "    5.76    7120 vmmemWSL       22   10.1 WSL VM │     0.10    4388 pyth…  12          2.0  S holder");
  row_is(26, "    0.42       4 System        310    0.0        │     0.04    2000 node   11          0.4  S");
  row_is(27, "    0.31   11736 vmwp.exe       40    0.1 WSL VM │     0.03    2001 sshd    1          0.0  S");
  row_is(28, "    0.22    9001 카카오톡.exe   60    0.4        │     0.02    2002 syst…   1          0.0  S");
  row_is(29, "5 of 46                                          │ 5 of 6 · guest count? 64");
  row_is(30, "Windows · cpu_burn.exe 31000 · cores                   all Windows processes   scale 50   last 1 min");
  check(!has(as_text({f[31], f[32]}), "│") && count_braille(as_text({f[32]})) > 0 && count_braille(columns(as_text({f[32]}), 0, 1)) == 1,
        "the history is 2 rows, full width from column 0, no gutter");
  row_is(33, "1 2 3 cpu 4 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit");
  // The focused list is lit, the other dim; ▶ and the selection background only on the focused list's row.
  check(f[22][0].color == tone::title && f[22][0].bold, "the focused list's heading is lit");
  bool wsl_dim = false, marked_bg = true, other_bg = false;
  for (const span& p : f[22]) wsl_dim = wsl_dim || (p.text == "WSL" && p.color == tone::dim);
  for (const span& p : f[24]) {
    if (has(p.text, "│")) break;
    if (p.text.find_first_not_of(' ') != std::string::npos) marked_bg = marked_bg && p.bg256 == kSelectBg;  // Not the padding to the divider
  }
  for (size_t i = 25; i <= 28; ++i) {
    for (const span& p : f[i]) other_bg = other_bg || p.bg256 == kSelectBg;
  }
  check(wsl_dim, "the other heading is dim");
  check(marked_bg && !other_bg && count_of(text, "▶") == 1, "one ▶, on the selection background, in the focused list");
  check(has(vt_frame(f, 34), "48;2;44;62;76"), "machtop's selection background reaches the terminal");

  // Both plots are as wide as the smaller column, whichever side is wider, and share the axis.
  for (const int w : {100, 101, 131, 132, 200}) {
    const auto g = large(s, h, w, 34);
    const int cells = (w - 3) / 2 - 5, right = kRight(w);
    check(static_cast<int>(cells_of(g[11]).size()) == right + 5 + cells, "the plots use the smaller width at " + std::to_string(w));
    check(columns(as_text({g[11]}), cells + 5, 3) == " │ ", "the divider sits where the left column ends at " + std::to_string(w));
  }

  // One onset: quiet to the left, amber to the right, on both sides and in the same cell of each.
  const auto left = cells_of(f[11]), right = cells_of(f[11]);
  check(left[5].fg == 236 && left[5].bg == 236 && left[47].fg == 179 && left[47].bg == 179, "the heat shows the onset");
  int onset_left = -1, onset_right = -1;
  for (int c = 0; c < 43; ++c) {
    if (onset_left < 0 && left[static_cast<size_t>(5 + c)].fg == 179) onset_left = c;
    if (onset_right < 0 && right[static_cast<size_t>(56 + c)].fg == 179) onset_right = c;
  }
  check(onset_left > 10 && onset_left == onset_right, "one onset lines up across the two blocks");
  check(left[5].glyph == "▀", "a heat cell is a half block");
  check(cells_of(f[8])[47].fg == 60 || cells_of(f[8])[47].fg == 67, "the kernel lane is a ramp colour");
  check(cells_of(f[9])[47].glyph == "█", "a lane is one full cell");
  check(cells_of(f[9])[56 + 42].fg == 179 && cells_of(f[9])[56 + 42].glyph == "█", "the lost lane is amber from 1 vCPU");
  check(columns(as_text({f[9]}), 56, 1) == " ", "a gated lost is a gap in its lane, not a cell");
  check(cells_of(f[8])[56 + 42].fg == 236, "the iowait lane is the iowait, 0 under a full guest");
}

void check_large_allocation() {
  // design/panel-grammar.md part 3, "Rows": the history goes before a list falls under 3 rows; then lists to 6,
  // the history to 3 for its legend (design/panel-grammar-slice2.md Q6), the heat block, the busy plots to 12, the
  // history to 6, and the lists take the rest.
  struct want { int height, heat, busy, lists, history; };
  for (const want& e : std::initializer_list<want>{{28, 0, 0, 0, 0},  // Below the floor
                                                   {29, 10, 3, 3, 0}, {30, 10, 3, 4, 0}, {31, 10, 3, 5, 0}, {32, 10, 3, 3, 2},
                                                   {34, 10, 3, 5, 2}, {35, 10, 3, 6, 2}, {36, 10, 3, 6, 3}, {40, 10, 7, 6, 3},
                                                   {44, 10, 11, 6, 3}, {45, 10, 12, 6, 3}, {48, 10, 12, 6, 6}, {49, 10, 12, 7, 6}, {66, 10, 12, 24, 6}}) {
    cpu_rows p;
    const bool fits = plan_cpu_rows(10, e.height, p);
    const std::string at = " at " + std::to_string(e.height);
    if (e.heat == 0) { check(!fits, "below the floor" + at); continue; }
    check(fits && p.heat == e.heat && p.busy == e.busy && p.lists == e.lists && p.history == e.history, "the allocation" + at);
  }
  // More threads than rows: the lists get their 6 before the heat block takes more than 10.
  cpu_rows p;
  check(plan_cpu_rows(20, 29, p) && p.heat == 10 && p.history == 0, "20 natural rows have the same floor");
  check(plan_cpu_rows(20, 34, p) && p.heat == 10 && p.lists == 5, "and the same 34: 5 list rows");
  check(plan_cpu_rows(20, 40, p) && p.heat == 14 && p.busy == 3 && p.lists == 6 && p.history == 3, "the heat block takes the next rows, after the history's third");
  check(plan_cpu_rows(20, 46, p) && p.heat == 20 && p.busy == 3, "up to 20 rows");
  check(plan_cpu_rows(32, 66, p) && p.heat == 20 && p.busy == 12 && p.history == 6, "never more");
  check(plan_cpu_rows(2, 21, p) && p.heat == 2 && !plan_cpu_rows(2, 20, p), "4 threads have a floor of 21");
  check(plan_cpu_rows(0, 19, p) && p.heat == 0, "no rows to draw leaves the fixed rows");

  const snapshot s = contended();
  const history h = contended_history(s);
  for (const int rows : {29, 30, 31, 32, 34, 40, 43, 44, 50, 66}) {
    const auto f = large(s, h, 120, rows);
    const std::string at = " at 120x" + std::to_string(rows);
    check(static_cast<int>(f.size()) == rows && frame_fits(f, 120, rows), "the frame fills its rows" + at);
    check(has(as_text({f.back()}), "q quit"), "the footer is the last row" + at);
    check(is_large(f), "this layout" + at);
    check(marks_hidden(f) == (rows < 32), "the footer says when the history went" + at);
    check(has(as_text(f), "kern│") && has(as_text(f), "lost≈"), "the lanes are always there" + at);
    check(!has(as_text(f), "RAM"), "nothing on panel 3 says RAM" + at);
  }
  const auto at40 = large(s, h, 131, 40);
  check(row_at(at40, 0, "kern") - row_at(at40, 0, "100 │") == 7, "at 40 the busy graph is 7 rows, the history 3");
  check(has(as_text(large(s, h, 100, 34)), "5 of 46") && has(as_text(large(s, h, 131, 40)), "6 of 46"), "5 list rows at 34, 6 at 40");
}

void check_large_floors() {
  struct size_case { int threads, rows; bool highest; };
  // Numbered rows, two threads each, so the natural count is half the threads, rounded up.
  for (const auto& c : std::initializer_list<size_case>{{4, 2, false}, {8, 4, false}, {14, 7, false}, {20, 10, false}, {21, 10, true}, {40, 10, true}, {64, 10, true}}) {
    snapshot s = contended(c.threads);
    s.cpu_layout = {};
    const history h = contended_history(s);
    const int floor = 19 + c.rows;  // Without the history: 13 fixed, 3 busy, 3 list rows
    const std::string at = " at " + std::to_string(c.threads) + " threads";
    const auto f = large(s, h, 100, floor);
    check(is_large(f), "the floor is 19 plus heat rows" + at);
    check(!is_large(large(s, h, 100, floor - 1)), "one row less is the small layout" + at);
    check(!is_large(large(s, h, 99, 66)), "99 columns are the small layout" + at);
    check(has(as_text({large(s, h, 100, floor - 1).back()}), "←→ 1 min  q quit"), "the small layout has its footer, and ←→ acts there too" + at);
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
  const auto f2 = large(small_room, h2, 100, 29);
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
  const std::vector<line> bf = render(back, 100, 34, kNow - 3600000, &stepped, state_for(3));
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

  // Lanes: the kernel lane leaves the VM out.
  snapshot lanes = contended();
  lanes.cpu_derived.own_kernel_pct = 2;
  lanes.cpu_derived.vm_cpu_pct = 70;
  lanes.memory.available = lanes.memory.total / 4;
  history lh;
  lh.push(lanes);
  const std::vector<line> lf = large(lanes, lh, 100, 34);
  check(cells_of(line_at(lf, 0, "kern"))[47].fg == 236, "the kern lane is kernel less the VM");
  check(cells_of(line_at(lf, 0, "VM  "))[47].fg == 110, "the VM lane is the VM");

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
  check(third(s, h) == "WSL short ≈14 of 20 vCPUs, inferred · load? 19.8", "the contended head");

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
  check(third(s, h) == "WSL short <1 vCPU, inferred · load? 19.8", "under 1 vCPU it prints <1");
  check(!watch_on(s), "and is not watch");

  s = contended();
  s.lost = {lost_state::collecting, kNaN};
  check(third(s, h) == "load? 19.8 · lost CPU collecting", "until 5 samples it says so");
  s.lost = {lost_state::guest_under_99, kNaN};
  check(third(s, h) == "load? 19.8 · lost CPU n/a under 99% busy", "under 99% guest busy, n/a and why");
  s.lost = {lost_state::windows_under_95, kNaN};
  check(third(s, h) == "load? 19.8 · lost CPU n/a, Windows under 95%", "under 95% Windows busy, n/a and why");
  s.lost = {lost_state::no_data, kNaN};
  check(third(s, h) == "load? 19.8 · lost CPU n/a", "with a stale line or a reset, n/a");
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
  check(!has(as_text(large(s, h)), "load? 123") && has(as_text(large(s, h)), "≈128 of 128 vCPUs, inferred"), "the head drops load, never the label");
  check(has(as_text(large(s, h, 131, 34)), "load? 123456.7"), "a wider head keeps it");
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
  check(!has(as_text(large(base, full, 100, 34, state_for(3, 300))), "collecting"), "a longer span counts nothing either");

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
  check(has(t, "collector restarted 2 times"), "the head says the collector keeps restarting");
  const std::string wide_gone = as_text(large(gone, before_gone, 131, 40));
  check(has(as_text(large(gone, before_gone, 200, 40)), "WSL unavailable: the collector sent nothing · collector restarted 2 times"), "with room, why as well");
  check(has(wide_gone, "collector restarted 2 times"), "and at 131");
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
  check(has(as_text({large(steal, full, 131, 34)[3]}), "· steal? 0.3%"), "steal is shown above 0");
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
  f = render(one_fail, 100, 34, kNow + 3000, &failing, state_for(3));
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
    p.pid = pid; p.name = "p" + std::to_string(pid); p.cores = cores; p.threads = 3; p.cores_known = true;
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
  check(!has(t, "Idle") && has(t, "6 of 6 ") && has(t, "Windows  by cores"), "the idle process is not listed");
  check(cores_key(0.149) == 1 && cores_key(0.151) == 2 && cores_key(kNaN) == -1 && cores_key(kInf) == -1 && cores_key(1e300) == 10000000000, "the sort key is 0.1 cores");

  snapshot w = contended();
  w.wsl_json = guest_line(std::vector<double>(20, 100.0), {},
                          R"({"pid":30,"name":"zeta","state":"R","rss":1,"cores":0.24,"threads":2,"rated":true,"on_gpu":false},)"
                          R"({"pid":20,"name":"beta","state":"D","rss":1,"cores":0.16,"threads":null,"rated":true,"on_gpu":true},)"
                          R"({"pid":10,"name":"한글추론기","state":"S","rss":1,"cores":0.01,"rated":true,"on_gpu":false},)"
                          R"({"pid":40,"name":"]0;pwned[31mevil","state":"S","rss":1,"cores":3,"threads":1,"rated":true,"on_gpu":false})");
  const std::vector<line> frame = large(w, h, 131, 40);  // Room for the guest's names
  const std::string wt = as_text(frame);
  check(wt.find("pwned") < wt.find("beta") && wt.find("beta") < wt.find("zeta"), "the guest sorts the same way: rounded cores, then PID, and a holder has no place of its own");
  check(has(line_with(frame, " beta "), " n/a "), "a null thread count is n/a");
  check(has(wt, " holder") && !has(wt, "on GPU"), "a holder is a holder");
  check(vt_clean(vt_frame(frame, 40)), "hostile names stay safe");
  bool sides = false;
  for (const line& l : frame) {
    const std::string row_text = as_text({l});
    if (has(row_text, "svchost.exe")) { sides = true; check(columns(row_text, 0, 64).find("svchost") != std::string::npos, "Windows names stay left"); }
    if (has(row_text, "한글추론기")) check(columns(row_text, kRight(131)).find("한글추론기") != std::string::npos, "Linux names stay right");
  }
  check(wt.find("Windows  by cores") < wt.find("WSL  by cores") && wt.find("WSL  by cores") != std::string::npos, "two lists");
  check(sides || has(wt, "p100") || has(wt, "cpu_burn.exe"), "the Windows list was drawn");

  // The guest's count is under its list; the head keeps its label, and `guest's count` gives way first.
  snapshot big = contended();
  big.wsl_json.replace(big.wsl_json.find("\"process_count\":64"), 18, "\"process_count\":1280");
  check(has(as_text(large(big, h)), "WSL  by cores · dxg: no usage measured") && has(as_text(large(big, h)), "of 6 · guest count? 1280"), "the WSL head keeps no usage measured, and the count is under the list");
  check(has(as_text(large(big, h, 131, 34)), "WSL  by cores, guest's count · dxg: no usage measured"), "with room it says whose count");
  check(!has(as_text(large(big, h, 131, 40)), "top 6"), "no heading says top");

  snapshot empty = contended();
  empty.processes.clear();
  const std::vector<line> sparse = large(empty, h, 100, 66);
  check(sparse.size() == 66 && has(as_text({sparse.back()}), "q quit") && has(as_text({sparse[60]}), "│ ") == false, "a short list leaves the footer on the last row");
  check(has(as_text(large(empty, h)), "Windows  ") && has(as_text(large(empty, h)), "unavailable"), "an empty list is unavailable");
  check(frame_fits(large(empty, h), 100, 34), "and fits");

  // More rows than processes: the footer stays on the last row.
  const auto tall = large(contended(), h, 100, 66);
  check(tall.size() == 66 && has(as_text({tall.back()}), "q quit"), "short lists leave the footer in place");
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
    // As whole numbers: a Windows-only figure such as the whole of all Windows processes may hold the digits.
    std::vector<std::string> numbers;
    std::string token;
    for (const char c : t + " ") {
      if ((c >= '0' && c <= '9') || c == '.') { token += c; continue; }
      if (!token.empty()) numbers.push_back(token);
      token.clear();
    }
    for (const char* sum : {"78", "61", "17.0", "71.2", "9.60"}) check(std::find(numbers.begin(), numbers.end(), sum) == numbers.end(), std::string("no sum of the two sides: ") + sum);
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

  // ← and → step the range, clamped, and the frame follows it: apply_key, then render, as main.cpp does.
  view_state v = state_for(3);
  std::string seen;
  for (const key_event k : {key_event{key::left}, key_event{key::left}, key_event{key::left}, key_event{key::right}}) {
    apply_key(v, k, s);
    seen += as_text({large(s, h, 131, 40, v).back()});
  }
  check(has(seen, "←→ 5 min") && has(seen, "←→ 10 min") && v.range == 1, "← steps 1, 5, 10 minutes and stops; → comes back, and the footer says each");
  check(as_text({large(s, h, 100, 34).back()}) == "1 2 3 cpu 4 5 6 7 8" + std::string(44, ' ') + "↑↓ select  tab list  ←→ 1 min  q quit\n",
        "panel 3's footer at 100 by 34, as the design draws it");
  for (const int span_s : {300, 600}) {
    const view_state o = state_for(3, span_s);
    const std::string t = as_text(large(s, h, 131, 40, o));
    const std::string name = span_s == 300 ? "5m" : "10m";
    check(has(t, span_s == 300 ? "←→ 5 min" : "←→ 10 min"), "the footer says the range");
    check(has(t, "-" + name + " ─"), "the axis says the range");
    const std::string busy = span_s == 300 ? "busy avg 100% max 100%" : "busy avg 60% max 100%";
    check(has(t, busy + (span_s == 300 ? " · each cell 5.1 s" : " · each cell 10 s")), "past 60 s the axis says what a cell is");
    check(!has(t, "peak over mean") && !has(t, "collecting"), "and not the old captions");
    check(has(as_text(large(s, h, 100, 34, o)), busy + " ─") && !has(as_text(large(s, h, 100, 34, o)), "each cell"), "where both do not fit, the cell's length goes first");
  }
  view_state one = state_for(1);
  apply_key(one, key_event{key::left}, s);
  check(one.range == 0 && !has(as_text(render(s, 131, 40, kNow, &h, one)), "←→"), "← is absent from panel 1, which has no graph, and does nothing there");
  check(as_text(render(s, 131, 40, kNow, &h, state_for(1, 600))) == as_text(render(s, 131, 40, kNow, &h, state_for(1))), "and panel 1 ignores the range");
  // Panel 2 and the small layout of panel 3 draw over the range too.
  const std::string plain = as_text(render(s, 131, 40, kNow, &h, state_for(2)));
  check(has(plain, "-60s ") && !has(plain, "-5m ") && has(plain, "←→ 1 min"), "panel 2 is at 1 minute by default, and offers ←→");
  for (const int span_s : {300, 600}) {
    const std::string name = span_s == 300 ? "5m" : "10m", minutes = span_s == 300 ? "←→ 5 min" : "←→ 10 min";
    const std::string g = as_text(render(s, 131, 40, kNow, &h, state_for(2, span_s))), c = as_text(render(s, 99, 40, kNow, &h, state_for(3, span_s)));
    check(has(g, "-" + name + " ─") && !has(g, "-60s") && has(g, minutes), "panel 2 draws over the range and says so");
    check(has(c, "-" + name + " ─") && !has(c, "-60s") && has(c, minutes), "the small layout draws over the range and says so");
  }
  for (const auto& [w, rows] : {std::pair{99, 40}, std::pair{100, 26}, std::pair{100, 27}, std::pair{100, 34}}) {
    view_state x = state_for(3);
    apply_key(x, key_event{key::left}, s);
    check(x.range == 1 && has(as_text({render(s, w, rows, kNow, &h, x).back()}), "←→ 5 min"), "← acts on panel 3 in either layout, at " + std::to_string(w) + "x" + std::to_string(rows));
  }

  // A span shows the samples it covers: 10 minutes reach back past the 60 s the default shows.
  const std::vector<line> shortest = large(s, h, 100, 34), longest = large(s, h, 100, 34, state_for(3, 600));
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
  const std::vector<line> bf = large(burst, bh, 100, 34, state_for(3, 600));
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
  const std::vector<line> sf = large(burst, sh, 100, 40, state_for(3, 600)), lean = large(burst, sh, 120, 30, state_for(3, 600));
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

  check(!any_hot(line_at(sf, 0, "kern"), 5), "the kern lane takes the mean");
  check(!any_hot(line_at(sf, kRight(100), "iowt"), kRight(100) + 5), "the iowait lane takes the mean");

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

  // Memory and its pressure are panel 4's: panel 3 shows neither, whatever the guest's line says.
  const std::string psi = "\"pressure\":{\"cpu\":4,\"memory\":0,\"io\":0}";
  snapshot p = contended();
  p.wsl_json.replace(p.wsl_json.find(psi), psi.size(), "\"pressure\":{\"cpu\":100,\"memory\":100,\"io\":100}");
  check(!has(as_text(large(p, h, 131, 40)), "pressure") && !has(as_text(large(p, h, 131, 40)), "psi"), "no pressure on panel 3");

  // Missing guest figures: an older collector's line.
  snapshot old_line = contended();
  old_line.wsl_json = busy_json();
  old_line.lost = {lost_state::no_data, kNaN};
  const std::string t = as_text(large(old_line, h));
  check(has(t, "user n/a · sys n/a · iowait n/a") && has(t, "load? n/a"), "a line without the new keys is n/a, not 0");
  check(has(t, "thr") && has(t, " n/a "), "and its processes have no thread count");

  snapshot no_mem = contended();
  no_mem.memory = {};
  const std::string nm = as_text(large(no_mem, h));
  check(!has(nm, "RAM") && !has(nm, "GiB  free") && frame_fits(large(no_mem, h), 100, 34), "unread memory changes nothing on panel 3");
}

void check_large_sweep() {
  for (const char* name : {"contended", "idle", "stale", "noisy"}) {
    snapshot s;
    history h;
    cpu_fixture(name, 60, s, h);
    for (const int span_s : {60, 600}) {
      const view_state o = state_for(3, span_s);
      for (int w = 100; w <= 300; w += 7) {
        for (int rows = 17; rows <= 66; ++rows) {
          check(frame_fits(render(s, w, rows, kNow, &h, o), w, rows), std::string(name) + " " + std::to_string(w) + "x" + std::to_string(rows));
        }
      }
    }
  }
  // The labels at every width this layout has, however the rows fall.
  const snapshot labelled = contended();
  const history lh = contended_history(labelled);
  for (int w = 100; w <= 300; ++w) {
    for (const int rows : {29, 34, 66}) {  // 29: the floor, without the history
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
    check(c.poll().k == key::none, "no console, no key");
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
    check(c.poll().k == key::none, "no key waiting, no wait");
    const auto record = [](WORD vk, WCHAR ch, BOOL down = TRUE, DWORD state = 0) {
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
        other, record('Q', L'q'), record('Q', L'q', FALSE),
        record(VK_LEFT, 0), record(0, 0xD55C), record('2', L'2'), record('2', L'2', FALSE),
        record('Q', 0x3142),                                       // q under Hangul input mode
        record('T', L't'), record('T', 0x3145), record('T', 0x14, TRUE, LEFT_CTRL_PRESSED),  // t, t under Hangul input mode, Ctrl+T: nothing now
        record(VK_DELETE, 0x7F), record(VK_PACKET, 0xD83D), record(VK_PACKET, 0xDE00),  // Delete, a surrogate pair
        record(0, L'q'), record('X', L'x'), record('Q', 0x11, TRUE, LEFT_CTRL_PRESSED),  // A character alone, another key, Ctrl+Q
        record(VK_NUMPAD3, L'3'), record('1', L'!', TRUE, SHIFT_PRESSED), record('Q', L'Q', TRUE, SHIFT_PRESSED),
        record(VK_PROCESSKEY, 0x3134), record(VK_TAB, L'\t', TRUE, SHIFT_PRESSED), record(VK_ESCAPE, 0x1B), record('Q', L'q', TRUE, LEFT_ALT_PRESSED)};
    const DWORD total = static_cast<DWORD>(std::size(events));
    DWORD put = 0;
    WriteConsoleInputW(in, events, total, &put);
    check(put == total, "events written");
    const auto next_is = [&](wwt::key k, int n = 0, bool alt = false) {
      const key_event e = c.poll();
      return e.k == k && e.panel == n && e.esc_first == alt;
    };
    check(next_is(key::quit), "a key comes back");
    check(next_is(key::left), "releases and focus are passed over; an arrow is a key now");
    check(next_is(key::panel, 2), "a lone non-ASCII character with no key is passed over");
    check(next_is(key::quit), "q under Hangul input mode");
    check(next_is(key::panel, 3), "t, Ctrl+T, Delete, a surrogate pair, a bare character, another key and Ctrl+Q are passed over; the numeric keypad counts");
    check(next_is(key::panel, 1), "Shift on a digit, for the layouts that need it");
    check(next_is(key::quit), "Shift on q");
    check(next_is(key::sort), "VK_PROCESSKEY with ㄴ is s");
    check(next_is(key::esc), "Shift+Tab is passed over; esc comes back");
    check(next_is(key::quit, 0, true), "Alt+Q is esc, then q, in one record");
    check(c.poll().k == key::none, "then nothing");
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
    check(c.poll().k == key::none && !c.is_terminal(), "a failed CONIN$ open gives no key");
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
      const std::vector<line> f = render(rule_sample(kNow, 50, 99), w, h, kNow, &fresh, state_for(3, span_s));
      if (!is_large(f)) { check(false, "the large layout draws" + n); continue; }
      const std::vector<cell> win = bottom_cells(f, 0), wsl = bottom_cells(f, 1);
      const int cells = static_cast<int>(win.size()), first = lead(win);
      const time_axis axis = make_axis(fresh, kNow, span_s * 1000LL, cells);
      check(cells > 9 && wsl.size() == win.size() && first == unwatched_cells(fresh, axis) && first == axis.cell.front() && first < cells,
            "the unwatched part ends at the first sample's bucket" + n);
      check(alternate(win, first) && alternate(wsl, lead(wsl)) && lead(wsl) == first, "a faint · in every other cell, both columns alike" + n);
      int dots = 0, bottom_dots = 0, busy_dots = 0;
      const size_t history_bottom = f.size() - 2;  // The history has no gutter: its bottom row is the one above the footer
      for (size_t i = 0; i < f.size(); ++i) {
        const std::string t = as_text({f[i]});
        dots += faint_dots(f[i]);
        if (t.rfind("  0 │", 0) == 0) {
          bottom_dots += faint_dots(f[i]);
          if (busy_dots == 0) busy_dots = faint_dots(f[i]);  // The busy plots' row comes first
        } else if (i == history_bottom) {
          bottom_dots += faint_dots(f[i]);
        }
      }
      check(dots == bottom_dots && busy_dots == 2 * ((first + 1) / 2), "dots only on the plots' bottom rows, not the heat block or the lanes" + n);
    }
  }

  // A measured 0 for the whole 10 minutes is ⣀ in every cell of both busy plots.
  history zero;
  for (int age = 600; age >= 0; --age) zero.push(rule_sample(kNow - age * 1000, 0, age));
  const std::vector<line> zf = render(rule_sample(kNow, 0, 999), 131, 40, kNow, &zero, state_for(3, 600));
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
    const std::vector<line> f = render(late, 131, 40, kNow - 100000, &partial, state_for(3, span_s));
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
  const std::vector<cell> cc = bottom_cells(render(after, 131, 40, kNow, &cleared, state_for(3, 600)), 0);
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

// design/panel-grammar.md part 2: view_state, apply_key, the footer and the panels not built.
void check_view_keys() {
  const snapshot s = busy();
  const history demo = demo_minute(s);
  const auto press = [&](view_state& v, std::initializer_list<key_event> keys) {
    bool going = true;
    for (const key_event& k : keys) going = apply_key(v, k, s) && going;
    return going;
  };
  const key_event down{key::down}, up{key::up}, tab{key::tab}, esc{key::esc}, left{key::left}, right{key::right};
  const auto digit = [](int n) { return key_event{key::panel, static_cast<uint8_t>(n)}; };

  view_state v;
  check(v.panel == 1 && v.range == 0 && v.panels[3].focus == 0 && !v.panels[3].rows[0].set, "a fresh view: panel 1, 1 minute, Windows focused, first rows");
  check(press(v, {digit(3), tab, tab}) && v.panel == 3 && v.panels[3].focus == 0, "3 tab tab returns focus to Windows");
  v = {};
  press(v, {digit(3), tab, esc, digit(3)});
  check(v.panels[3].focus == 1, "3 tab esc 3 keeps WSL focused");
  v = {};
  press(v, {digit(3), down, down, down});
  const row_key third = v.panels[3].rows[0];
  const std::vector<const process_row*> order = windows_cpu_order(s);
  check(third.set && third.pid == order[3]->pid && third.name == order[3]->name, "↓↓↓ moves to the fourth row of the Windows list as drawn: pid " + std::to_string(third.pid));
  press(v, {esc, digit(3)});
  check(v.panels[3].rows[0].pid == third.pid && v.panels[3].rows[0].name == third.name, "↓↓↓ esc 3 keeps that row");
  press(v, {tab, down});
  check(v.panels[3].rows[1].set && v.panels[3].rows[1].pid == wsl_cpu_order(read_wsl(s.wsl_json))[1]->pid && v.panels[3].rows[0].pid == third.pid, "each list keeps its own selection");
  v = {};
  press(v, {digit(3), up});
  check(v.panels[3].rows[0].pid == order[0]->pid, "↑ on the first row stays");
  for (size_t i = 0; i < order.size() + 5; ++i) press(v, {down});
  check(v.panels[3].rows[0].pid == order.back()->pid, "↓ stops at the last row, no wrap");
  press(v, {down});
  check(v.panels[3].rows[0].pid == order.back()->pid, "and stays there");
  // A row that leaves the list: the selection reads as the first row, and the key is kept until a key moves it.
  snapshot gone = s;
  const uint32_t leaving = order[5]->pid;
  v = {};
  press(v, {digit(3), down, down, down, down, down});
  check(v.panels[3].rows[0].pid == leaving, "the sixth row is selected");
  gone.processes.erase(std::find_if(gone.processes.begin(), gone.processes.end(), [&](const process_row& p) { return p.pid == leaving; }));
  check(apply_key(v, key_event{key::sort}, gone) && v.panels[3].rows[0].pid == leaving, "a key that moves nothing keeps the key of a row that left");
  apply_key(v, down, gone);
  check(v.panels[3].rows[0].pid == windows_cpu_order(gone)[1]->pid, "from a row that left, ↓ moves on from the first row");
  // A reused pid under another name is another row.
  snapshot renamed = s;
  for (process_row& p : renamed.processes) {
    if (p.pid == third.pid) p.name = "other.exe";
  }
  v = {};
  press(v, {digit(3), down, down, down});
  apply_key(v, down, renamed);
  check(v.panels[3].rows[0].pid == windows_cpu_order(renamed)[1]->pid, "a pid under another name is not the row selected");

  // The range: one for the screen, clamped, only where there is a graph to change.
  v = {};
  press(v, {digit(2), left, left, left});
  check(v.range == 2, "←←← stops at 10 minutes");
  press(v, {right, right, right});
  check(v.range == 0, "→→→ stops at 1 minute");
  press(v, {left, digit(3)});
  check(v.range == 1, "the range is the screen's, kept across panels");
  press(v, {digit(1), left, left});
  check(v.range == 1, "← on panel 1 leaves the range");
  for (int p = 5; p <= 8; ++p) {
    press(v, {digit(p), left, right, right});
    check(v.range == 1 && v.panel == p, "←→ on a panel not built leaves the range: " + std::to_string(p));
  }
  press(v, {esc});
  check(v.panel == 1 && v.range == 1, "esc goes to panel 1 and keeps the range");

  // tab: only where two lists can be selected.
  v = {};
  press(v, {tab});
  check(v.panels[1].focus == 0, "tab on panel 1 changes nothing");
  press(v, {digit(2), tab});
  check(v.panels[2].focus == 0, "tab on panel 2 changes nothing");
  for (const snapshot& one_side : {no_wsl(), [] { snapshot t = busy(); t.wsl_json = "{\"schema\":2}"; return t; }()}) {
    view_state w;
    apply_key(w, digit(3), one_side);
    apply_key(w, tab, one_side);
    check(w.panels[3].focus == 0, "tab on panel 3 with WSL unavailable changes nothing");
  }
  snapshot windows_unread = s;
  windows_unread.processes.clear();
  v = {};
  apply_key(v, digit(3), windows_unread);
  apply_key(v, tab, windows_unread);
  check(v.panels[3].focus == 1, "with the Windows list empty, tab moves focus to the list that has rows");
  apply_key(v, tab, windows_unread);
  check(v.panels[3].focus == 1, "and keeps it there");
  // Focus never sticks on a list with nothing to select: WSL gone, unreadable or stale, from focus on it.
  for (const snapshot& gone_wsl : {no_wsl(), [] { snapshot t = busy(); t.wsl_json = "{\"schema\":2}"; return t; }(), [] { snapshot t = busy(); t.wsl_age_s = 3600; return t; }()}) {
    view_state w;
    press(w, {digit(3), tab});
    check(w.panels[3].focus == 1, "WSL focused while it has rows");
    apply_key(w, tab, gone_wsl);
    check(w.panels[3].focus == 0, "tab from a WSL list that is gone, unreadable or stale moves focus to Windows");
    w = {};
    press(w, {digit(3), tab});
    apply_key(w, down, gone_wsl);
    check(w.panels[3].focus == 0 && w.panels[3].rows[0].pid == windows_cpu_order(gone_wsl)[1]->pid, "↓ there does too, and moves in Windows");
    w = {};
    press(w, {digit(3), tab});
    apply_key(w, up, gone_wsl);
    check(w.panels[3].focus == 0 && w.panels[3].rows[0].pid == windows_cpu_order(gone_wsl)[0]->pid, "and ↑");
  }
  view_state stale_view;
  apply_key(stale_view, digit(3), stale_now());
  apply_key(stale_view, tab, stale_now());
  check(stale_view.panels[3].focus == 0, "tab on panel 3 with a stale WSL line changes nothing (stale_now)");

  // Panels, quitting, and Alt.
  v = {};
  for (int p = 1; p <= 8; ++p) {
    apply_key(v, digit(p), s);
    check(v.panel == p, "digit " + std::to_string(p) + " opens its panel");
  }
  check(!apply_key(v, key_event{key::quit}, s), "q quits");
  check(apply_key(v, key_event{key::sort}, s) && apply_key(v, key_event{}, s) && v.panel == 8, "s and nothing change nothing");
  v = {};
  apply_key(v, digit(2), s);
  apply_key(v, key_for('3', U'3', kLeftAlt), s);
  check(v.panel == 3, "Alt+3 is esc, then 3");
  v = {};
  press(v, {digit(3), tab});
  apply_key(v, key_for(vk::escape, 0x1B, kLeftAlt), s);
  check(v.panel == 1 && v.panels[3].focus == 1, "Alt+esc is esc twice, and focus stays");
  check(!apply_key(v, key_for('Q', U'q', kLeftAlt), s), "Alt+Q quits");
  view_state odd;
  odd.panel = 42;
  odd.range = -3;
  check(apply_key(odd, right, s) && odd.panel >= 1 && odd.panel <= 8 && odd.range == 0, "a view out of range is brought back into it");

  // The footer: never wider than the screen, the current panel named and lit, built dim, not built faint,
  // only the keys that act there, no [t] and no s.
  for (int p = 1; p <= 8; ++p) {
    for (int w = 1; w <= 300; ++w) {
      const std::vector<line> f = render(s, w, 40, kNow, &demo, state_for(p));
      const line& foot = f.back();
      const std::string t = as_text({foot});
      const std::string at = " panel " + std::to_string(p) + " at " + std::to_string(w);
      check(width_of(foot) <= w && static_cast<int>(f.size()) <= 40, "the footer fits" + at);
      const std::string name = fmt("%d %s", p, kPanelLabel[p]);
      if (w >= display_width(name) + 2) check(has(t, name), "the current panel is named" + at);
      check(!has(t, "[t]") && !has(t, " s ") && !has(t, "sort"), "no t and no s" + at);
      // Panel 3 offers ↑↓ and tab with its lists; the keys go together, so they need 19 + 3 + 37 columns there.
      // 19 for the panels' numbers, 3, 37 for the keys; panel 4 stacked under 80 columns leaves rows out at 40
      // high, and the footer's leading … takes 2 more.
      // Panel 2 offers ↑↓ but not tab: its one list (design/panel-grammar-slice2.md section 5), 19 + 3 + 27 columns.
      const bool lists = p == 3 || p == 4;
      const int keys_from = p == 4 ? 61 : lists ? 59 : p == 2 ? 49 : 38;
      check(has(t, "↑↓") == ((lists || p == 2) && w >= keys_from) && has(t, "tab list") == (lists && w >= keys_from), "↑↓ on panels 2, 3 and 4, which draw their lists, tab only on 3 and 4" + at);
      check(has(t, "←→") == (panel_has_range(p) && w >= keys_from), "←→ only on panels 2, 3 and 4, where it fits beside the panels' numbers" + at);
      for (const span& q : foot) {
        if (q.text.empty() || q.text[0] < '1' || q.text[0] > '8' || (q.text.size() > 1 && q.text[1] != ' ')) continue;
        const int n = q.text[0] - '0';
        const tone want = n == p ? tone::title : panel_built(n) ? tone::dim : tone::faint;
        check(q.color == want && q.bold == (n == p), "panel " + std::to_string(n) + "'s tone" + at);
      }
    }
  }
  const auto foot_text = [&](int p, int w, int range = 0) {
    view_state x = state_for(p);
    x.range = range;
    return as_text({render(s, w, 40, kNow, &demo, x).back()});
  };
  check(foot_text(1, 131) == "1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost" + std::string(59, ' ') + "q quit\n", "panel 1's footer at 131, as the design draws it");
  check(foot_text(3, 80) == "1 2 3 cpu 4 5 6 7 8" + std::string(24, ' ') + "↑↓ select  tab list  ←→ 1 min  q quit\n", "panel 3's footer at 80: the other panels are numbers");
  check(has(foot_text(2, 131, 1), "←→ 5 min  q quit") && has(foot_text(3, 131, 2), "←→ 10 min  q quit"), "the footer says the range");
  check(foot_text(3, 6) == "3 cpu\n", "too narrow for the numbers: the current panel alone");
  // Hidden rows are still said: after the panels, or by a leading … where that does not fit.
  check(has(as_text({render(s, 131, 12, kNow, &demo, state_for(2)).back()}), "8 vm cost   … some rows hidden"), "a short window says it dropped something");
  for (const int w : {1, 10, 36, 60, 80}) {
    check(as_text({render(s, w, 8, kNow, &demo, state_for(2)).back()}).rfind("…", 0) == 0, "hidden is marked at width " + std::to_string(w));
  }
  check(!marks_hidden(render(s, 131, 50, kNow, &demo, state_for(2))), "nothing hidden, no marker");

  // The panels not built: their question, where their figures are meanwhile, the footer on the bottom row.
  const struct { int p; const char* title; const char* question; const char* where; } pages[] = {
      {5, "DISK", "Which disk is busy, and is it the WSL disk?", "on panel 1"},
      {6, "NETWORK", "Which interface is busy?", "on panel 1"}, {7, "PROCESSES", "Every process, every measure.", "on panels 1 and 3"},
      {8, "VM COST", "What has each side cost the other, and when?", "on panel 2 and the VM's CPU on panel 3"}};
  for (const auto& pg : pages) {
    const std::vector<line> f = render(s, 100, 34, kNow, &demo, state_for(pg.p));
    const std::string t = as_text(f);
    const std::string n = std::string(" ") + pg.title;
    check(f.size() == 34 && line_with(f, pg.title).rfind(pg.title, 0) == 0 && has(line_with(f, pg.title), "not built yet"), "a page not built says so in its rule" + n);
    const std::string wide = as_text(render(s, 300, 34, kNow, &demo, state_for(pg.p)));  // One line: no phrase is broken
    check(has(wide, pg.question) && has(wide, pg.where) && has(wide, "until this panel is built"), "and asks its question, and says where its figures are" + n);
    check(count_braille(t) == 0 && !has(t, "0%"), "and draws nothing it does not have" + n);
    check(has(as_text({f.back()}), "q quit"), "the footer is the bottom row" + n);
    for (int w = 1; w <= 300; w += 3) {
      for (const int h : {1, 2, 5, 24, 34}) check(frame_fits(render(s, w, h, kNow, &demo, state_for(pg.p)), w, h), n + " fits " + std::to_string(w) + "x" + std::to_string(h));
    }
  }
  check(has(as_text(render(s, 100, 34, kNow, &demo, state_for(5))), "DISK ───────────────────────────────────────────────────────────────────────────────── not built yet"), "the DISK rule, as the design draws it");

  // Without a history every panel is the one plain screen.
  for (const snapshot& t : {busy(), bare()}) {
    for (int p = 1; p <= 8; ++p) check(as_text(render(t, 131, 50, kNow, nullptr, state_for(p))) == as_text(render(t, 131, 50, kNow)), "--text is unchanged: no history, no panel, no footer");
  }
  check(vt_clean(vt_frame(render(s, 131, 40, kNow, &demo, state_for(7)), 40)), "a page not built is only our own sequences");
}

// The audit of commits 1 to 5: ←→ only where a graph is drawn, the caption's * only with its label, and n/a
// for a process's first reading.
void check_audit_fixes() {
  const snapshot s = busy();
  const history demo = demo_minute(s);
  for (int p = 2; p <= 3; ++p) {
    for (int w = 1; w <= 200; w += 3) {
      for (int rows = 1; rows <= 50; rows += 1) {
        const std::vector<line> f = render(s, w, rows, kNow, &demo, state_for(p));
        const std::string body = as_text({f.begin(), f.end() - 1}), foot = as_text({f.back()});
        const bool graph = has(body, "-60s") || has(body, "100│") || has(body, "100 │");
        const std::string at = " panel " + std::to_string(p) + " at " + std::to_string(w) + "x" + std::to_string(rows);
        if (has(foot, "←→")) check(graph, "←→ is offered only where a graph is drawn" + at);
        if (!graph) check(!has(foot, "←→"), "no graph, no ←→" + at);
        if (p == 2) check(has(body, "shares*") == has(body, "112%*"), "the caption's * comes and goes with its label" + at);
        if (p == 2 && has(body, "max 112%")) check(has(body, "max 112%*") == has(body, "shares*"), "a label whose caption cannot carry * prints the number without it" + at);
      }
    }
  }
  // The window's edge in the labels: a sample at exactly -60 s is outside, one just inside is in.
  history edge;
  edge.push(rule_sample(kNow - 60000, 99, 0));
  edge.push(rule_sample(kNow - 1000, 10, 1));
  edge.push(rule_sample(kNow, 20, 2));
  window_stats es = series_stats(edge, &history_sample::gpu_util, kNow, 60000);
  check(es.max == 20 && es.avg == 15, "series_stats leaves out the sample at exactly -span");
  check(plot_series(edge, &history_sample::gpu_util, kNow, 240, 60000).stats.max == 20, "and plot_series agrees");
  es = series_stats(edge, &history_sample::gpu_util, kNow - 1, 60000);
  check(es.max == 99, "a millisecond earlier it is inside");
  view_state short_view = state_for(2);
  apply_key(short_view, key_event{key::left}, s);
  check(short_view.range == 1, "the range still changes on a panel too short to draw it");
  check(has(as_text(render(s, 131, 40, kNow, &demo, state_for(2))), "shares*"), "with room, the caption has its *");

  // n/a where a process has no earlier reading, on panels 1 and 3, both sides.
  snapshot first = busy();
  for (process_row& p : first.processes) {
    if (p.name == "System") p.cores_known = false;
  }
  const std::string four = "\"pid\":4121,\"name\":\"python3\",\"state\":\"R\",\"rss\":6442450944,\"cores\":3.21,\"rated\":true";
  first.wsl_json.replace(first.wsl_json.find(four), four.size(), "\"pid\":4121,\"name\":\"python3\",\"state\":\"R\",\"rss\":6442450944,\"cores\":0,\"rated\":false");
  const std::string one = as_text(render(first, 200, 66, kNow)), three = as_text(render(first, 200, 66, kNow, &demo, state_for(3)));
  check(has(one, "   n/a       4 System") && has(one, "   n/a    4121 python3"), "panel 1: a first reading is n/a on both sides, not 0.00");
  check(has(three, "   n/a       4 System") || has(three, "System"), "panel 3 shows System");
  check(line_with(render(first, 200, 66, kNow, &demo, state_for(3)), "4 System").find("0.42") == std::string::npos, "panel 3: a first reading's cores are not printed");
  check(!has(line_with(render(first, 200, 66, kNow), " 4121 python3"), "0.00"), "nor the guest's");
  check(cores_text(true, 0) == "0.00" && cores_text(false, 3) == "n/a" && cores_text(true, kNaN) == "n/a", "a measured 0 is 0.00; unmeasured or missing is n/a");
}

// design/panel-grammar.md part 3, "The per-process ring".
void check_process_ring() {
  const auto win = [](uint32_t pid, const char* name, double cores, uint64_t start = 7, bool known = true) {
    process_row p;
    p.pid = pid; p.name = name; p.cores = cores; p.working_set = 1ull << 30; p.create_time = start; p.cores_known = known;
    return p;
  };
  const auto at = [](int64_t t, std::vector<process_row> rows) {
    snapshot s = bare();
    s.unix_ms = t;
    s.processes = std::move(rows);
    return s;
  };
  const auto value = [](const history& h, const proc_id& id, size_t i) {
    const int slot = h.slot_of(id);
    return slot < 0 ? kNaN : static_cast<double>(h.cores_at(slot, h.position(h.samples()[i])));
  };
  const proc_id a{0, 100, 7, "a.exe"};

  history h;
  h.push(at(kNow, {win(0, "Idle", 11), win(100, "a.exe", 2, 7, false)}));
  check(h.slot_of(proc_id{0, 0, 7, "Idle"}) < 0, "pid 0 gets no slot");
  check(h.slot_of(a) >= 0 && std::isnan(value(h, a, 0)), "a first reading is a gap, not 0");
  check(h.memory_at(h.slot_of(a), h.position(h.samples()[0])) == 1.0f, "its memory is kept: working set, GiB");
  h.push(at(kNow + 1000, {win(100, "a.exe", 2)}));
  check(value(h, a, 1) == 2.0, "the next reading is the value");
  h.push(at(kNow + 2000, {}));
  check(std::isnan(value(h, a, 2)), "a sample the process missed is a gap");
  h.push(at(kNow + 3000, {win(100, "a.exe", 3, 8)}));
  check(std::isnan(value(h, proc_id{0, 100, 8, "a.exe"}, 2)) && value(h, proc_id{0, 100, 8, "a.exe"}, 3) == 3.0 && value(h, a, 1) == 2.0,
        "a reused pid with the same name and another start is another slot, and the old one keeps its own");
  h.push(at(kNow + 4000, {win(100, "b.exe", 4, 8)}));
  check(h.slot_of(proc_id{0, 100, 8, "a.exe"}) < 0 && value(h, proc_id{0, 100, 8, "b.exe"}, 4) == 4.0, "the same pid and start under another name is another process");

  // Every push writes every slot: after the ring wraps, a slot that missed a sample reads NaN, not what was there.
  history wrap;
  for (int i = 0; i < 601; ++i) wrap.push(at(kNow + i * 1000, {win(100, "a.exe", 5), win(200, "b.exe", 1)}));
  for (int i = 601; i < 700; ++i) wrap.push(at(kNow + i * 1000, {win(100, "a.exe", 5)}));
  const proc_id b{0, 200, 7, "b.exe"};
  bool gaps = true;
  for (size_t i = wrap.samples().size() - 99; i < wrap.samples().size(); ++i) gaps = gaps && std::isnan(value(wrap, b, i));
  check(gaps && value(wrap, b, 0) == 1.0, "a slot that missed samples reads NaN there after the ring wraps");
  // Gone for all that is kept: let go.
  for (int i = 700; i < 1302; ++i) wrap.push(at(kNow + i * 1000, {win(100, "a.exe", 5)}));
  check(wrap.slot_of(b) < 0 && wrap.slot_of(a) >= 0, "a process gone for kKeepMs frees its slot");

  // A backward clock step: the values stay in their samples.
  history step;
  for (int i = 0; i < 5; ++i) step.push(at(kNow + i * 1000, {win(100, "a.exe", 1 + i)}));
  step.push(at(kNow - 3600000, {win(100, "a.exe", 9)}));
  bool kept = step.samples().size() == 6;
  for (size_t i = 0; i < 5; ++i) kept = kept && value(step, a, i) == 1.0 + static_cast<double>(i);
  check(kept && value(step, a, 5) == 9.0, "a backward clock step keeps a slot's values in their samples");
  // A stall of all that is kept: the samples go, and with them every value a slot held.
  snapshot stall = at(kNow + 5000000, {win(100, "a.exe", 7)});
  stall.interval_s = 600;
  step.push(stall);
  check(step.samples().size() == 1 && value(step, a, 0) == 7.0, "a stall of kKeepMs clears the slots' past with the samples");

  // The cap: 1024 slots, or twice the rows of the newest sample. The slot least recently seen is reused, a
  // selected one never, and nothing is refused.
  history cap;
  std::vector<process_row> many;
  for (uint32_t i = 1; i <= 1024; ++i) many.push_back(win(i, "p", 1));
  cap.push(at(kNow, many));
  check(cap.slots_allocated() == 1024 && cap.slots_used() == 1024, "1024 processes, 1024 slots");
  const proc_id first{0, 1, 7, "p"}, second{0, 2, 7, "p"};
  std::vector<process_row> later(many.begin() + 2, many.begin() + 12);  // pids 3 to 12 stay; 1 and 2 have not been seen since
  cap.push(at(kNow + 1000, later), {first});
  later.push_back(win(5000, "new", 1));
  cap.push(at(kNow + 2000, later), {first});
  check(cap.slots_allocated() == 1024 && cap.slot_of(proc_id{0, 5000, 7, "new"}) >= 0, "at the cap a new process takes a slot");
  check(cap.slot_of(first) >= 0 && cap.slot_of(second) < 0, "the least recently seen is reused, the selected one never");
  std::vector<process_row> crowd;
  for (uint32_t i = 1; i <= 1500; ++i) crowd.push_back(win(10000 + i, "q", 1));
  cap.push(at(kNow + 3000, crowd), {first});
  bool all = true;
  for (uint32_t i = 1; i <= 1500; ++i) all = all && cap.slot_of(proc_id{0, 10000 + i, 7, "q"}) >= 0;
  check(all && cap.slot_of(first) >= 0, "more processes than the cap: none is refused, and the selected one stays");
  check(cap.slots_allocated() <= 3000, "the cap is twice the newest sample's rows");

  // The guest: rated decides a first reading; a repeated line carries once, a second repeat is a gap; stale is a gap.
  snapshot g = contended();
  g.wsl_json = guest_line(std::vector<double>(20, 50.0), {}, R"({"pid":7,"name":"burn","state":"R","rss":1073741824,"cores":3,"rated":false,"start":11,"on_gpu":false})");
  history gh;
  g.unix_ms = kNow;
  gh.push(g);
  const proc_id burn{1, 7, 11, "burn"};
  check(gh.slot_of(burn) >= 0 && std::isnan(value(gh, burn, 0)), "rated false is a gap");
  guest_figures fig;
  fig.seq = 1;
  g.wsl_json = guest_line(std::vector<double>(20, 50.0), fig, R"({"pid":7,"name":"burn","state":"R","rss":1073741824,"cores":3,"rated":true,"start":11,"on_gpu":false})");
  g.unix_ms = kNow + 1000;
  gh.push(g);
  check(value(gh, burn, 1) == 3.0 && gh.memory_at(gh.slot_of(burn), gh.position(gh.samples()[1])) == 1.0f, "a rated guest row is its cores, and its stat rss");
  g.unix_ms = kNow + 2000;
  gh.push(g);
  check(value(gh, burn, 2) == 3.0, "one repeat of the line carries the value");
  g.unix_ms = kNow + 3000;
  gh.push(g);
  check(std::isnan(value(gh, burn, 3)), "a second repeat is a gap");
  fig.seq = 2;
  g.wsl_json = guest_line(std::vector<double>(20, 50.0), fig, R"({"pid":7,"name":"burn","state":"R","rss":1073741824,"cores":3,"rated":true,"start":11,"on_gpu":false})");
  g.wsl_age_s = 5;
  g.unix_ms = kNow + 4000;
  gh.push(g);
  check(std::isnan(value(gh, burn, 4)), "a stale line is a gap");
  check(gh.slot_of(proc_id{1, 7, 12, "burn"}) < 0, "the guest's start is part of who it is");
}

// design/panel-grammar.md part 3: selection, scrolling, the one history.
void check_selection() {
  const snapshot s = contended();
  const history h = contended_history(s);
  const key_event down{key::down}, up{key::up}, tab{key::tab};
  const auto rows_of = [](const std::vector<line>& f, const char* end) {  // The Windows rows drawn, by pid
    std::vector<std::string> out;
    bool in = false;
    for (const line& l : f) {
      const std::string t = columns(as_text({l}), 0, 48);
      if (has(t, "   cores ")) { in = true; continue; }
      if (in && has(t, end)) break;
      if (in) out.push_back(t);
    }
    return out;
  };
  const auto nth = [](const std::vector<std::string>& rows, size_t i) { return i < rows.size() ? rows[i] : std::string(); };
  view_state v = state_for(3);
  std::vector<line> f = large(s, h, 100, 34, v);
  check(count_of(as_text(f), "▶") == 1 && has(line_with(f, "▶"), "cpu_burn.exe"), "the first row is selected before any key");
  check(has(as_text(f), "Windows · cpu_burn.exe 31000 · cores"), "and the history is its own, titled with its list and pid");
  // 46 rows in 5: machtop's scroll.
  for (int i = 0; i < 4; ++i) apply_key(v, down, s);
  f = large(s, h, 100, 34, v);
  std::vector<std::string> shown = rows_of(f, " of 46");
  check(shown.size() == 5 && has(nth(shown, 0), "31000") && has(nth(shown, 4), "▶"), "↓ 4 times: row 1 still drawn first, the selection on row 5");
  apply_key(v, down, s);
  f = large(s, h, 100, 34, v);
  shown = rows_of(f, " of 46");
  check(shown.size() == 5 && !has(as_text(f), "31000 cpu_burn.exe   ") && has(nth(shown, 4), "▶") && has(as_text(f), "5 of 46"), "the fifth draws rows 2 to 6, the selection last: " + nth(shown, 0));
  for (int i = 0; i < 60; ++i) apply_key(v, down, s);
  f = large(s, h, 100, 34, v);
  shown = rows_of(f, " of 46");
  const std::string last_pid = std::to_string(windows_cpu_order(s).back()->pid);
  check(has(nth(shown, 4), "▶") && has(nth(shown, 4), last_pid) && has(as_text(f), "5 of 46"), "↓ at 46 stays on the last row");
  for (int i = 0; i < 41; ++i) {
    apply_key(v, up, s);
    shown = rows_of(large(s, h, 100, 34, v), " of 46");
    if (i < 40) check(has(nth(shown, 4), "▶"), "↑ from 46 keeps the selection on the bottom row until row 5");
  }
  shown = rows_of(large(s, h, 100, 34, v), " of 46");
  check(has(nth(shown, 0), "31000") && has(nth(shown, 4), "▶"), "row 5 again: the list is drawn from row 1");
  check(!has(as_text(large(s, h, 100, 34, v)), "top 5") && !has(as_text(large(s, h, 100, 34, v)), "top 6"), "no heading says top");
  check(has(as_text(large(s, h, 100, 34, v)), "6 · guest count? 64"), "the guest's own count");

  // Tab: the WSL list is focused, its heading lit, its row marked, the history its own, as the guest counted.
  v = state_for(3);
  apply_key(v, tab, s);
  f = large(s, h, 131, 40, v);
  check(count_of(as_text(f), "▶") == 1 && line_with(f, "▶").find("▶") > line_with(f, "▶").find("│"), "exactly one ▶, in the focused list");
  check(has(as_text(f), "WSL · cpu_burn 4121 · cores, guest's count"), "a WSL history says it is the guest's count");
  check(has(line_with(f, "WSL · cpu_burn"), "scale 20"), "scale 20 for the burner: 19.72 cores");
  // An idle process: the floor of 0.05 cores.
  for (int i = 0; i < 5; ++i) apply_key(v, down, s);
  f = large(s, h, 131, 40, v);
  check(has(line_with(f, "WSL · dbus-daemon"), "scale 0.05") && has(line_with(f, "WSL · dbus-daemon"), "max 0.01"), "scale 0.05 for an idle process: " + line_with(f, "WSL · "));
  // A row that leaves falls to the first row, and comes back when it returns.
  snapshot gone = s;
  gone.wsl_json = guest_line(std::vector<double>(20, 100.0), {}, R"({"pid":4121,"name":"cpu_burn","state":"R","rss":1,"cores":19.72,"threads":21,"rated":true,"on_gpu":false})");
  f = large(gone, h, 131, 40, v);
  check(has(line_with(f, "▶"), "4121 cpu_burn"), "a selected row that left: the first row is shown selected");
  f = large(s, h, 131, 40, v);
  check(has(line_with(f, "▶"), "dbus-daemon"), "and when it returns it is selected again");

  // A process with no slot draws a gap, never 0; the history follows focus back.
  snapshot fresh = s;
  process_row newcomer;
  newcomer.pid = 99999; newcomer.name = "fresh.exe"; newcomer.cores = 50; newcomer.cores_known = true;
  fresh.processes.push_back(newcomer);
  view_state nv = state_for(3);
  f = large(fresh, h, 131, 40, nv);
  check(has(as_text(f), "Windows · fresh.exe 99999 · cores") && !has(line_with(f, "Windows · fresh.exe"), "avg"), "a process with no history yet: no label");
  const int hist_row = row_at(f, 0, "Windows · fresh.exe");
  check(hist_row > 0 && count_braille(as_text({f[static_cast<size_t>(hist_row) + 1], f[static_cast<size_t>(hist_row) + 2]})) == 0, "and no cell, not a 0");

  // Hostile names in the title stay safe.
  snapshot evil = s;
  evil.processes.push_back(newcomer);
  evil.processes.back().name = "\x1b]0;pwned\x07\x1b[2Jevil.exe";
  evil.processes.back().cores = 90;
  check(vt_clean(vt_frame(large(evil, h, 131, 40, state_for(3)), 40)) && has(as_text(large(evil, h, 131, 40, state_for(3))), "Windows · ?]0;pwned"), "a hostile name in the history's title is made safe");

  // The four fixtures at both sizes: one history, labelled with its list.
  for (const char* name : {"contended", "idle", "stale", "noisy"}) {
    snapshot fs;
    history fh;
    cpu_fixture(name, 60, fs, fh);
    for (const auto& [w, rows] : {std::pair{100, 34}, std::pair{131, 40}}) {
      const std::vector<line> ff = large(fs, fh, w, rows);
      const std::string t = as_text(ff);
      const std::string n = std::string(" ") + name + " " + std::to_string(w) + "x" + std::to_string(rows);
      check(count_of(t, "Windows · ") == 1 && count_of(t, "· cores") == 1, "one history" + n);
      check(!has(t, "RAM"), "nothing on panel 3 says RAM" + n);
      const int marks = count_of(t, "?");
      const int expected = count_of(t, "load?") + count_of(t, "guest count?") + count_of(t, "steal?") + count_of(t, "? not yet checked");
      check(marks == expected && (marks == 0 || has(t, "? not yet checked")), "panel 3's only ? are load, the guest's count and steal, and the rule says what it means" + n);
      check(has(t, "stat rss GiB") && !has(t, "RSS GiB"), "the guest's resident size is named for what it reads" + n);
      check(vt_frame(ff, rows).size() < 48 * 1024, "under the frame budget" + n);
    }
  }
  // The selected rows are what push keeps.
  view_state sel = state_for(3);
  apply_key(sel, down, s);
  const std::vector<proc_id> keep = selected_processes(s, sel);
  const std::vector<const process_row*> cpu_order = windows_cpu_order(s);
  check(keep.size() == 1 && keep[0].side == 0 && keep[0].pid == cpu_order[1]->pid && keep[0].start == cpu_order[1]->create_time, "selected_processes names the row moved to, with its start");
  apply_key(sel, tab, s);
  apply_key(sel, down, s);
  check(selected_processes(s, sel).size() == 2 && selected_processes(s, sel)[1].side == 1, "and the guest's once it is moved in");
  check(selected_processes(s, state_for(3)).empty(), "a list never moved in names nothing: its first row is in the sample");
}

// design/panel-grammar.md part 1, after experiment 06: what keeps its ? and what does not.
void check_unchecked_marks() {
  const std::string one = as_text(render(busy(), 200, 66, kNow));
  check(has(one, "cache? 12.0 GiB") && has(one, "swap? 0.0 GiB"), "Windows cache and guest swap keep their ?");
  check(has(one, "? not yet checked") && count_of(one, "? not yet checked") == 1, "panel 1 says once what ? means");
  check(has(one, "available 39.0 GiB") && !has(one, "available?") && !has(one, "commit?") && !has(one, "WS GiB?"), "cleared by experiment 06: available, commit, working set");
  check(has(one, "stat rss GiB") && !has(one, "RSS GiB?") && !has(one, " RSS GiB"), "guest RSS is relabelled stat rss, with no ?");
  check(!has(one, "cores?"), "guest cores are cleared");
  snapshot nothing = bare();
  nothing.memory = {};
  check(!has(as_text(render(nothing, 200, 66, kNow)), "?"), "no unchecked figure shown, no ?");
  const history demo = demo_minute(busy());
  check(!has(as_text(render(busy(), 131, 40, kNow, &demo, state_for(2))), "?"), "panel 2 has none");
}

// design/panel-grammar.md part 4, panel 4 Memory, with part 1's experiment-06 marks and machtop's zoom.
void check_memory_panel() {
  const snapshot s = busy();
  const history demo = demo_minute(s);
  const auto mem = [&](int w, int rows, const view_state& v = state_for(4), const snapshot* t = nullptr, const history* h = nullptr) {
    return render(t ? *t : s, w, rows, kNow, h ? h : &demo, v);
  };
  std::vector<line> f = mem(100, 34);
  std::string text = as_text(f);
  check(f.size() == 34 && frame_fits(f, 100, 34), "100 by 34 fits");
  check(!has(text, "not built yet") && has(as_text({f.back()}), "4 mem"), "key 4 is the memory panel now");
  check(has(text, "whole machine, includes the WSL VM") && has(text, "guest view, inside the VM"), "each side says what it is");
  check(has(text, "? not yet checked") && has(text, "cache? 12.0") && has(text, "swap? 0.0 GiB"), "cache and swap keep their ?, and the rule says what it means");
  check(count_of(text, "?") == 5 && !has(text, "used?") && !has(text, "commit?") && !has(text, "WS GiB?"), "and nothing else has one: ? on cache twice, swap, the guest's count, and the rule");
  check(has(text, "WINDOWS  used 24.7 of 63.7 GiB · cache? 12.0") && has(text, "WSL  used 7.3 of 31.2 GiB · cache 7.0"), "the heads, one format for both");
  check(has(text, "commit 31.0 of 80.0 GiB") && has(text, "PSI mem 0.00% avg10"), "commit on Windows, PSI mem in the guest");
  // The VM on its own row, full width, with nothing of the guest's on it.
  const std::string vm = line_with(f, "vmmemWSL working set");
  check(vm.rfind("VM  vmmemWSL working set 10.1 GiB · Windows' view of the VM, included in WINDOWS", 0) == 0 && !has(vm, "│"), "the VM's row, on its own");
  // The two sides stay apart: every guest figure right of the divider, every Windows one left.
  for (const line& l : f) {
    const std::string t = as_text({l});
    const size_t bar = t.find(" │ ");
    if (bar == std::string::npos) continue;
    for (const char* guest : {"7.3 of 31.2", "swap?", "PSI mem", "stat rss", "python3", "ollama"}) {
      if (has(t, guest)) check(t.find(guest) > bar, std::string("guest figures right of the divider: ") + guest);
    }
    for (const char* win : {"24.7 of 63.7", "commit", "WS GiB", "svchost", "vmmemWSL"}) {
      if (has(t, win)) check(t.find(win) < bar, std::string("Windows figures left of it: ") + win);
    }
  }
  // Lists: by memory, then pid; pid 0 left out; the VM's two processes named.
  const std::vector<const process_row*> by_ws = windows_mem_order(s);
  bool sorted = true;
  for (size_t i = 1; i < by_ws.size(); ++i) sorted = sorted && (by_ws[i - 1]->working_set > by_ws[i]->working_set || (by_ws[i - 1]->working_set == by_ws[i]->working_set && by_ws[i - 1]->pid < by_ws[i]->pid));
  check(sorted && by_ws.front()->pid == 7120 && std::none_of(by_ws.begin(), by_ws.end(), [](const process_row* p) { return p->pid == 0; }), "Windows by working set then pid, no pid 0");
  check(has(line_with(f, "7120 vmmemWSL"), "WSL VM") && has(line_with(f, "11736 vmwp.exe"), "WSL VM"), "vmmemWSL and vmwp.exe are the VM");
  check(has(line_with(f, "▶"), "7120 vmmemWSL") && has(text, "Windows · vmmemWSL 7120 · working set"), "the panel opens on the VM's history");
  check(line_with(f, "WSL  by stat rss").find("WSL") != std::string::npos && has(text, "stat rss GiB") && line_with(f, "4121 python3").find("6.00") != std::string::npos, "the guest by stat rss");
  // After 16 presses of ↓ at 100 by 34: rows 7 to 17, the 17th last and marked, 11 of 46, its own history.
  view_state v = state_for(4);
  for (int i = 0; i < 16; ++i) apply_key(v, key_event{key::down}, s);
  f = mem(100, 34, v);
  text = as_text(f);
  const std::vector<const process_row*> order = windows_mem_order(s);
  check(has(line_with(f, "▶"), std::to_string(order[16]->pid)) && order[16]->pid == 10048, "the 17th row is selected: pid 10048");
  check(has(text, "11 of 46") && has(line_with(f, "11 of 46", -1), "▶"), "drawn from row 7 with the 17th last, 11 of 46 under it");
  check(has(line_with(f, "Windows   "), std::to_string(order[6]->pid)) || has(text, std::to_string(order[6]->pid) + " "), "row 7 is the first drawn");
  check(has(text, "Windows · svchost.exe 10048 · working set") && has(line_with(f, "10048 · working set"), "GiB") &&
            has(line_with(f, "10048 · working set", 1), "all other Windows processes' working sets"),
        "the history names pid 10048, on the scale of all working sets, in GiB, the legend naming the grey");
  // tab: the guest's list, its history.
  apply_key(v, key_event{key::tab}, s);
  text = as_text(mem(100, 34, v));
  check(has(text, "WSL · python3 4121 · stat rss"), "tab: the guest's history, named stat rss");
  // Zoom, machtop's rule: the spread or the floor, a tenth under the minimum, a fifth over the maximum.
  window_stats st;
  st.min = 24.75;
  st.max = 24.75;
  zoom z = zoom_of(st, kMachineFloorGiB);
  check(std::fabs(z.bottom - (24.75 - 0.00625)) < 1e-9 && std::fabs(z.top - z.bottom - 0.075) < 1e-9, "a flat series spans the floor, 64 MiB, a tenth of it under");
  st.min = 10;
  st.max = 20;
  z = zoom_of(st, kMachineFloorGiB);
  check(z.bottom == 9 && z.top == 21, "a wide series: 10 to 20 is drawn 9 to 21");
  st.min = 0.0001;
  st.max = 0.002;
  z = zoom_of(st, kProcessFloorGiB);
  check(z.bottom == 0 && std::fabs(z.top - 1.2 * kProcessFloorGiB) < 1e-12, "never below 0, and a process's floor is 4 MiB");
  // At least 4 units of the last digit across the range: GiB to 3 decimals, else MiB.
  zoom_unit u = unit_of({24.74, 24.82});
  check(u.decimals == 2 && std::string(u.name) == " GiB", "0.08 GiB across: 2 decimals of GiB");
  u = unit_of({10.149, 10.1537});
  check(u.decimals == 3 && std::string(u.name) == " GiB", "4.8 MiB across: 3 decimals of GiB");
  u = unit_of({10.151, 10.153});
  check(u.decimals == 1 && std::string(u.name) == " MiB", "2 MiB across: MiB with 1 decimal, not 10.2–10.2 GiB");
  u = unit_of({0.02, 0.03});
  check(u.decimals == 0 && std::string(u.name) == " MiB", "under 1 GiB: MiB, 10 MiB across needs no decimal");
  // A flat series draws one line, not blank, and the label prints its range.
  const std::string plots = as_text(mem(131, 40));
  check(has(plots, "used avg 24.7 max 24.7 · range") && has(plots, "cache avg 7.0 max 7.0 · range"), "each plot's title has avg, max and range");
  int flat = 0;
  for (const line& l : mem(131, 40)) {
    const std::string t = as_text({l});
    if (t.rfind("    │⣀", 0) == 0) ++flat;
  }
  check(flat == 2, "a flat series draws one line, on the bottom row, in both of Windows' plots");
  const std::vector<line> pf = mem(131, 40);
  check(!has(as_text({pf.begin(), pf.end() - 1}), "min "), "no minimum is printed");
  // WSL stopped: the right side says why, and nothing there to select.
  const snapshot gone = no_wsl();
  view_state g = state_for(4);
  apply_key(g, key_event{key::tab}, gone);
  const std::vector<line> gf = mem(131, 40, g, &gone);
  check(has(as_text(gf), "WSL data unavailable") && !has(as_text({gf.back()}), "tab list") && has(as_text({gf.back()}), "↑↓ select") && g.panels[4].focus == 0, "WSL stopped: it says why, and tab is not offered");
  // Every size, three fixtures.
  snapshot hostile = busy();
  hostile.memory.available = hostile.memory.total * 3;
  hostile.processes.push_back(hostile.processes[2]);
  hostile.processes.back().name = "\x1b]0;x\x07\x1b[2J";
  hostile.processes.back().working_set = 50ull * kG;
  for (const snapshot* t : std::initializer_list<const snapshot*>{&s, &hostile}) {
    for (int w = 1; w <= 300; w += 3) {
      for (int rows = 1; rows <= 66; rows += 5) check(frame_fits(mem(w, rows, state_for(4), t), w, rows), "panel 4 fits " + std::to_string(w) + "x" + std::to_string(rows));
    }
  }
  const history bare_h = demo_minute(bare());
  const snapshot b = bare();
  for (int w = 1; w <= 300; w += 3) {
    for (int rows = 1; rows <= 66; rows += 5) check(frame_fits(mem(w, rows, state_for(4), &b, &bare_h), w, rows), "bare panel 4 fits " + std::to_string(w) + "x" + std::to_string(rows));
  }
  check(vt_clean(vt_frame(mem(131, 40, state_for(4), &hostile), 40)), "a hostile name on panel 4 stays safe");
  check(has(as_text(mem(131, 40, state_for(4), &b, &bare_h)), "WINDOWS  used 5.0 of 16.0 GiB") && has(as_text(mem(131, 40, state_for(4), &b, &bare_h)), "vmmemWSL not found"), "bare: Windows alone, and the VM said missing");
  snapshot no_psi = busy();
  no_psi.wsl_json.replace(no_psi.wsl_json.find("\"memory\":0.0"), 12, "\"memory\":null");
  check(has(as_text(mem(131, 40, state_for(4), &no_psi)), "PSI mem n/a"), "no PSI is n/a, not 0");
  // Rows: the history 8 and the lists 15 at 40, 6 and 11 at 34.
  memory_rows r = plan_memory_rows(40);
  check(r.history == 8 && r.lists == 15 && r.plots, "at 40: 8 history rows, 15 list rows");
  r = plan_memory_rows(34);
  check(r.history == 6 && r.lists == 11, "at 34: 6 and 11");
  r = plan_memory_rows(22);
  check(r.history == 2 && r.lists == 3, "short: the history keeps 2 rows while the lists have 3");
  r = plan_memory_rows(20);
  check(r.history == 0 && r.lists == 4, "shorter: the history goes before a list falls under 3");
  r = plan_memory_rows(18);
  check(!r.plots && r.lists == 8, "shorter still: then the plots");
  for (const auto& [w, rows] : {std::pair{131, 40}, std::pair{100, 34}}) check(vt_frame(mem(w, rows), rows).size() < 48 * 1024, "panel 4 under the frame budget");
}

// The final audit of slice 1: the auditor's cases (scratchpad ring/t.cpp and g.cpp), kept.
void check_final_audit() {
  constexpr int64_t T0 = 1793000000000LL;
  const auto win = [](uint32_t pid, const std::string& name, uint64_t start, double cores = 1) {
    process_row p;
    p.pid = pid; p.name = name; p.create_time = start; p.cores = cores; p.cores_known = true; p.working_set = 1 << 20;
    return p;
  };
  const auto at = [](int64_t t, std::vector<process_row> r) {
    snapshot s;
    s.unix_ms = t;
    s.interval_s = 1;
    s.processes = std::move(r);
    s.memory.total = 1ull << 34;
    s.memory.available = 1ull << 33;
    return s;
  };
  std::vector<process_row> base;
  for (uint32_t i = 1; i <= 390; ++i) base.push_back(win(i, "p", i));

  // A: a selected process leaves for 30 s while 50 new ones arrive each second, past the cap: its slot stays,
  // through the key remembered, with or without the start it was seen with.
  for (const uint64_t key_start : {uint64_t{42}, uint64_t{0}}) {
    history h;
    view_state v = state_for(3);
    v.panels[3].rows[0] = {true, 5000, "x.exe", key_start};
    int64_t t = T0;
    for (int i = 0; i < 5; ++i) {
      std::vector<process_row> r = base;
      r.push_back(win(5000, "x.exe", 42, 9));
      const snapshot s = at(t, r);
      h.push(s, selected_processes(s, v));
      t += 1000;
    }
    uint32_t next = 100000;
    for (int i = 0; i < 30; ++i) {
      std::vector<process_row> r = base;
      for (int k = 0; k < 50; ++k, ++next) r.push_back(win(next, "cc", next));
      const snapshot s = at(t, r);
      h.push(s, selected_processes(s, v));
      t += 1000;
    }
    const int slot = h.slot_of(proc_id{0, 5000, 42, "x.exe"});
    check(slot >= 0 && h.slots_allocated() <= 1024, "a selected process away while the ring churns keeps its slot, key start " + std::to_string(key_start));
    check(slot >= 0 && h.cores_at(slot, h.position(h.samples()[0])) == 9.0f, "and its history");
  }
  // B: at the cap, a new process never takes the slot of one in the same sample.
  {
    history h;
    std::vector<process_row> prev;
    for (uint32_t i = 1; i <= 1024; ++i) prev.push_back(win(i, "p", i, 2));
    h.push(at(T0, prev));
    h.push(at(T0 + 1000, prev));
    std::vector<process_row> now;
    for (uint32_t i = 0; i < 256; ++i) now.push_back(win(9000 + i, "n", i));
    for (uint32_t i = 1; i <= 256; ++i) now.push_back(win(i, "p", i, 2));
    h.push(at(T0 + 2000, now));
    int lost = 0;
    for (uint32_t i = 1; i <= 256; ++i) {
      const int slot = h.slot_of(proc_id{0, i, i, "p"});
      lost += slot < 0 || std::isnan(h.cores_at(slot, h.position(h.samples()[1]))) ? 1 : 0;
    }
    check(lost == 0 && h.slots_allocated() == 1024, "a process in this sample keeps its earlier samples: " + std::to_string(lost) + " of 256 lost");
  }
  // C: a backward clock step at the cap: when a process was last seen moves with the samples, so slots are
  // let go and reused as before, and do not grow.
  {
    history h;
    int64_t t = T0;
    uint32_t next = 100000;
    for (int i = 0; i < 700; ++i, t += 1000) {
      std::vector<process_row> r = base;
      for (int k = 0; k < 5; ++k, ++next) r.push_back(win(next, "cc", next));
      h.push(at(t, r));
    }
    t -= 3600000;
    for (int i = 0; i < 600; ++i, t += 1000) {
      std::vector<process_row> r = base;
      for (int k = 0; k < 5; ++k, ++next) r.push_back(win(next, "cc", next));
      h.push(at(t, r));
    }
    check(h.slots_allocated() == 1024 && h.slots_used() <= 1024, "a backward step does not grow the slots: " + std::to_string(h.slots_allocated()));
  }
  // g.cpp: a flat vmmemWSL of 10.15 GiB on panel 4 sits where its range says, and an idle process's cores
  // history on panel 3 prints its 0.05 scale whole.
  {
    history h;
    snapshot s;
    for (int i = 0; i < 60; ++i) {
      s = snapshot{};
      s.unix_ms = T0 + i * 1000;
      s.interval_s = 1;
      s.cpu.core_pct.assign(20, 5);
      s.cpu.busy_pct = 5;
      s.memory.total = 1ull << 36;
      s.memory.available = 1ull << 35;
      s.processes.push_back(win(10, "idle.exe", 5, 0.01));
      s.processes.back().working_set = 10898438144ull;
      h.push(s);
    }
    const std::vector<line> f = render(s, 131, 40, T0 + 59500, &h, state_for(4));
    // Since slice 2 step C the history draws from 0 to its scale, printed in the heading (Q3), not zoomed.
    const std::string title = line_with(f, "Windows · idle.exe 10 · working set");
    const size_t sc = title.find("scale ");
    const double top = sc == std::string::npos ? kNaN : atof(title.c_str() + sc + 6);
    const double scale = has(title, " MiB") ? 1024 : 1;
    const double v = 10898438144.0 / kG * scale;
    const int first = row_at(f, 0, "Windows · idle.exe");
    const int rows = plan_memory_rows(40).history;
    int drawn = -1;
    for (int i = 0; i < rows && drawn < 0; ++i) {
      if (count_braille(as_text({f[static_cast<size_t>(first + 1 + i)]})) > 0) drawn = i;
    }
    const int dots = std::max(1, static_cast<int>(std::lround(v / top * 4 * rows)));
    const int expected = rows - 1 - (dots - 1) / 4;
    check(std::isfinite(top) && top > 0 && rows >= 2 && drawn == expected && !has(title, "range"),
          "the history's top sits where its scale says: row " + std::to_string(drawn) + ", the scale puts it at " + std::to_string(expected) + ": " + title);
    check(v <= top && has(title, "scale 20 GiB"), "and the value is under the printed scale, 20 GiB");
    check(rows >= 2 && count_braille(as_text({f[static_cast<size_t>(first + rows)]})) > 0, "and the bottom row is drawn: from 0");
    const std::vector<line> c = render(s, 131, 40, T0 + 59500, &h, state_for(3));
    const int hist = row_at(c, 0, "Windows · idle.exe");
    check(hist > 0 && !has(as_text({c[static_cast<size_t>(hist) + 1]}), "│") && has(line_with(c, "Windows · idle.exe"), "scale 0.05   last 1 min"), "the 0.05 cores scale is printed whole in the heading");
  }
}

// design/panel-grammar-slice2.md section 1: a row is its key's by pid and name and, where both starts are
// known, by start too.
void check_selection_identity() {
  constexpr uint64_t kA = 133000000000000000ull, kB = kA + 777;
  snapshot s = busy();
  for (process_row& p : s.processes) p.create_time = p.pid == 10000 ? kA : kA + 1000 + p.pid;
  const history h = demo_minute(s);
  const std::vector<const process_row*> order = windows_cpu_order(s);
  view_state v = state_for(3);
  for (size_t i = 0; i < order.size() && !(v.panels[3].rows[0].set && v.panels[3].rows[0].pid == 10000); ++i) apply_key(v, key_event{key::down}, s);
  const row_key k = v.panels[3].rows[0];
  check(k.set && k.pid == 10000 && k.name == "svchost.exe" && k.start == kA, "after ↓ the key holds the row's start");
  const std::vector<proc_id> keep = selected_processes(s, v);
  check(keep.size() == 1 && keep[0].pid == 10000 && keep[0].start == kA, "selected_processes passes the start on");
  check(has(line_with(large(s, h, 131, 40, v), "▶"), "10000") && has(as_text(large(s, h, 131, 40, v)), "Windows · svchost.exe 10000 · cores"), "svchost.exe 10000 is selected and its history shown");

  // The same pid and name, another create time: another process, so the selection falls to the first row.
  snapshot reused = s;
  for (process_row& p : reused.processes) {
    if (p.pid == 10000) p.create_time = kB;
  }
  std::vector<line> f = large(reused, h, 131, 40, v);
  check(has(line_with(f, "▶"), " " + std::to_string(order[0]->pid) + " ") && !has(line_with(f, "▶"), "10000"), "a reused pid under the same name: the selection falls to the first row");
  check(!has(as_text(f), "svchost.exe 10000 ·"), "and the history title does not name 10000");
  // The same start: the selection stays.
  f = large(s, h, 131, 40, v);
  check(has(line_with(f, "▶"), "10000"), "the same pid, name and start: the selection stays");
  // ↓ from a reused pid moves on from the first row, as from a row that left.
  view_state moved = v;
  apply_key(moved, key_event{key::down}, reused);
  check(moved.panels[3].rows[0].pid == windows_cpu_order(reused)[1]->pid, "↓ from a reused pid moves on from the first row");

  // A start of 0 on either side: pid and name decide.
  const row_key known{true, 10000, "svchost.exe", kA}, unknown{true, 10000, "svchost.exe", 0};
  const std::vector<row_key> rows_known = {{true, 4, "System", 9}, known}, rows_unknown = {{true, 4, "System", 9}, unknown};
  check(selected_row(rows_unknown, known) == 1, "a row with no start matches a key with one by pid and name");
  check(selected_row(rows_known, unknown) == 1, "a key with no start matches a row with one by pid and name");
  check(selected_row(rows_known, row_key{true, 10000, "svchost.exe", kB}) == 0, "two known starts that differ do not match");
  check(selected_row(rows_known, row_key{true, 10000, "other.exe", kA}) == 0, "the name still decides beside the start");
  snapshot zero = s;
  for (process_row& p : zero.processes) {
    if (p.pid == 10000) p.create_time = 0;
  }
  check(has(line_with(large(zero, h, 131, 40, v), "▶"), "10000"), "a Windows row with create time 0: pid and name keep the selection");
  // The guest: busy_json's rows carry no start, so the key holds 0; a later line with a start keeps it.
  view_state g = state_for(3);
  apply_key(g, key_event{key::tab}, s);
  apply_key(g, key_event{key::down}, s);
  const row_key gk = g.panels[3].rows[1];
  check(gk.set && gk.start == 0, "a guest row with no start: the key's start is 0");
  snapshot started = s;
  const std::string at = "\"pid\":" + std::to_string(gk.pid) + ",";
  check(started.wsl_json.find(at) != std::string::npos, "the guest's selected row is in the line");
  started.wsl_json.replace(started.wsl_json.find(at), at.size(), at + "\"start\":4242,");
  f = large(started, h, 131, 40, g);
  check(has(line_with(f, "▶"), " " + std::to_string(gk.pid) + " "), "a guest row that gains a start keeps the selection made without one");
  apply_key(g, key_event{key::up}, started);
  apply_key(g, key_event{key::down}, started);
  check(g.panels[3].rows[1].pid == gk.pid && g.panels[3].rows[1].start == 4242, "moved to again, the key holds the guest's start");
  snapshot restarted = started;
  restarted.wsl_json.replace(restarted.wsl_json.find("\"start\":4242"), 13, "\"start\":4243");
  f = large(restarted, h, 131, 40, g);
  check(!has(line_with(f, "▶"), " " + std::to_string(gk.pid) + " "), "a guest pid reused under the same name, another start: the selection falls");
}

// design/panel-grammar-slice2.md section 4: two time columns to a braille cell, on an axis that halves the
// cell axis exactly, every cell's bucket even.
void check_two_columns() {
  const auto windows_busy = [](const history_sample& p) { return p.windows_cpu; };
  const auto glyph = [](const std::vector<line>& block, int cell) {
    const std::string t = as_text({block.back()});
    std::string out = columns(t, kCpuGutter + cell, 1);
    size_t i = 0;
    return out.empty() ? char32_t{0} : next_cp(out, i);
  };
  // At 10 minutes on 43 cells: two samples one column apart in one cell, different values, a cell whose halves differ.
  const time_axis cells43 = make_axis(history(), kNow, 600000, 43), cols43 = make_axis(history(), kNow, 600000, 43, 2);
  check(cells43.bucket_ms == 13954 && cols43.bucket_ms == 6977 && cols43.cells == 86 && cols43.last == 2 * cells43.last + 1, "10 minutes on 43 cells: 13954 ms a cell, 6977 a column, the right edge shared");
  const int64_t cell_start = cells43.last * cells43.bucket_ms;
  history two;
  two.push(rule_sample(cell_start + 100, 20, 0));
  two.push(rule_sample(cell_start + 6977 + 100, 80, 1));
  const time_axis a = make_axis(two, cell_start + 6977 + 100, 600000, 43, 2);
  const std::vector<double> cols = bucket_cells(a, windows_busy);
  check(cols.size() == 86 && cols[84] == 20 && cols[85] == 80, "each sample in its own column of the cell");
  const char32_t both = glyph(plot_block(cols, 1, "", "", 0, bucket_stats(a, windows_busy)), 42);
  check(both == 0x2800 + (0x40u | 0x80u | 0x20u | 0x10u), "and the braille cell's two columns differ: one dot left, three right");
  // The right edge's second half not begun: the cell is its first half, not a gap.
  history left_only;
  left_only.push(rule_sample(cell_start + 100, 30, 0));
  const std::vector<double> edge = bucket_cells(make_axis(left_only, cell_start + 100, 600000, 43, 2), windows_busy);
  check(edge[84] == 30 && edge[85] == 30, "a newest cell whose second half has not begun draws its first half in both");
  history right_only;
  right_only.push(rule_sample(cell_start + 100, 30, 0));
  right_only.push(rule_sample(cell_start + 7000, 70, 1));
  const std::vector<double> later = bucket_cells(make_axis(right_only, cell_start + 7000, 600000, 43, 2), windows_busy);
  check(later[84] == 30 && later[85] == 70, "a second half that has begun keeps its own value");

  // The large layout: every busy-plot column lies inside the heat cell under it, at 100 and 131 columns, 1, 5 and
  // 10 minutes, every bucket even, column 2c + 1 never across two cells.
  const snapshot cs = contended();
  const history ch = contended_history(cs);
  for (const int w : {100, 131}) {
    const int cells = (w - kListGap) / 2 - kCpuGutter;
    for (const int64_t span : {60000LL, 300000LL, 600000LL}) {
      const std::string at = " at " + std::to_string(w) + ", " + std::to_string(span / 60000) + " min";
      const int64_t end = axis_end(ch, kNow);
      const time_axis heat = make_axis(ch, end, span, cells), busy_axis = make_axis(ch, end, span, cells, 2);
      bool inside = heat.bucket_ms % 2 == 0 && busy_axis.bucket_ms * 2 == heat.bucket_ms && busy_axis.last == 2 * heat.last + 1 && busy_axis.cells == 2 * heat.cells &&
                    heat.samples == busy_axis.samples;
      for (size_t i = 0; inside && i < heat.samples.size(); ++i) inside = busy_axis.cell[i] < 0 ? heat.cell[i] < 0 : busy_axis.cell[i] / 2 == heat.cell[i];
      for (int c = 0; inside && c < heat.cells; ++c) {
        const int64_t from = (heat.last - heat.cells + 1 + c) * heat.bucket_ms;
        const int64_t col = busy_axis.last - busy_axis.cells + 1 + 2 * c;
        inside = col * busy_axis.bucket_ms == from && (col + 2) * busy_axis.bucket_ms == from + heat.bucket_ms;
      }
      check(inside, "every busy-plot cell covers the heat cell's milliseconds" + at);
      if (w == 131 && span == 60000) check(heat.bucket_ms == 1018, "at 131, 1 minute over 59 cells: 1017 ms rounds to an even 1018" + at);
      if (w == 131 && span == 300000) check(heat.bucket_ms == 5086, "at 131, 5 minutes over 59 cells: 5085 ms rounds to an even 5086" + at);
      check(frame_fits(large(cs, ch, w, 40, state_for(3, static_cast<int>(span / 1000))), w, 40), "the large layout fits" + at);
    }
  }

  // The max dot lands in the column of the latest maximum, left or right.
  for (const int side : {0, 1}) {
    history m;
    for (int k = 0; k < 14; ++k) {
      const int64_t t = cell_start + 200 + k * 1000;  // 7 in the left column, 7 in the right
      const bool spike = (side == 0 && k == 3) || (side == 1 && k == 10);
      m.push(rule_sample(t, spike ? 90 : 10, k));
    }
    const time_axis ma = make_axis(m, cell_start + 200 + 13 * 1000, 600000, 43, 2);
    const window_stats st = bucket_stats(ma, windows_busy);
    const char32_t g = glyph(plot_block(bucket_cells(ma, windows_busy), 1, "", "", 0, st), 42);
    const unsigned bits = static_cast<unsigned>(g - 0x2800);
    check(st.max_at == 84 + side && st.max_count > 1, std::string("the maximum's column is the ") + (side ? "right" : "left"));
    check(g >= 0x2800 && (bits & (side ? kDotRight[3] : kDotLeft[3])) && !(bits & (side ? kDotLeft[3] : kDotRight[3])), std::string("and its dot is drawn there, ") + (side ? "right" : "left"));
  }

  // · only in a cell both of whose columns are unwatched.
  const std::vector<double> blank(20, kNaN);
  const auto dotted = [&](int unwatched_columns) {
    const std::string t = as_text({plot_block(blank, 1, "", "", unwatched_columns, window_stats{}).back()});
    return columns(t, kCpuGutter, 10);
  };
  check(dotted(5) == "·         " && dotted(4) == dotted(5), "5 unwatched columns: 2 cells, the third half-watched has no dot");
  check(dotted(6) == "· ·       ", "6 columns are 3 cells, and · goes in every other one");
}

// design/panel-grammar-slice2.md section 6: a history has no gutter and no time axis; avg, max, scale and
// `last N min` are in its heading, and memory's history draws from 0.
void check_history_heading() {
  const snapshot s = busy();
  const history demo = demo_minute(s);
  const auto rtrim = [](std::string t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    return t;
  };
  const auto ends = [](const std::string& t, const std::string& tail) { return t.size() >= tail.size() && t.compare(t.size() - tail.size(), tail.size(), tail) == 0; };
  for (const int panel : {3, 4}) {
    for (const int range : {0, 1, 2}) {
      const std::string last = fmt("last %d min", static_cast<int>(kSpansMs[range] / 60000));
      view_state v = state_for(panel);
      v.range = range;
      for (int w = 1; w <= 300; ++w) {
        const std::vector<line> f = render(s, w, 40, kNow, &demo, v);
        const int at = row_at(f, 0, "Windows · ");
        const std::string n = " panel " + std::to_string(panel) + ", " + last + ", at " + std::to_string(w);
        if (w >= (panel == 4 ? kMemoryWide : 60)) check(at > 0, "the history is drawn" + n);
        if (at < 0) continue;
        const std::string t = rtrim(as_text({f[static_cast<size_t>(at)]}));
        bool cut = false;
        for (size_t k = 1; k < last.size(); ++k) cut = cut || ends(t, " " + last.substr(0, k));
        check(!cut && (ends(t, last) || !has(t, "last")), "the heading is never cut inside `" + last + "`" + n);
        if (w >= 70) check(ends(t, last), "the heading ends with `" + last + "`" + n);
        // The plot below: no gutter, no axis row.
        for (size_t i = static_cast<size_t>(at) + 1; i + 1 < f.size(); ++i) {
          const std::string r = as_text({f[i]});
          check(!has(r, "│") && !has(r, "-60s") && !has(r, "-5m") && !has(r, "-10m") && !has(r, " now"), "no gutter and no axis row under the history" + n);
        }
      }
    }
  }
  // ← and → change the heading's range.
  view_state v = state_for(3);
  for (const auto& [key, want] : {std::pair{key::left, "last 5 min"}, std::pair{key::left, "last 10 min"}, std::pair{key::right, "last 5 min"}}) {
    apply_key(v, key_event{key}, s);
    check(ends(rtrim(line_with(render(s, 131, 40, kNow, &demo, v), "Windows · ")), want), std::string("after ←/→ the heading reads ") + want);
  }
  // The figures go from the left, the range stays.
  {
    row t;
    t.add("Windows · x 1 · cores");
    history_heading(t, from_left({{"avg 1.00"}, {"max 2.00"}, {"scale 2"}}), 60000, 21 + 3 + display_width("scale 2   last 1 min"));
    const std::string text = rtrim(as_text({t}));
    check(ends(text, "scale 2   last 1 min") && !has(text, "avg") && !has(text, "max"), "short of room, avg and max go before scale and the range");
    row u;
    u.add("Windows · x 1 · cores");
    history_heading(u, from_left({{"avg 1.00"}, {"max 2.00"}, {"scale 2"}}), 60000, 21 + 2 + display_width("last 1 min"));
    check(rtrim(as_text({u})) == "Windows · x 1 · cores", "with no room for the range, the heading is the left side alone, not cut");
  }
  // Panel 4's history, one series: from 0 to a scale, never a range; its used and cache plots keep theirs.
  const std::vector<line> m = render(s, 131, 40, kNow, &demo, state_for(4));
  const std::string mt = line_with(m, "Windows · vmmemWSL 7120 · working set");
  check(has(mt, "scale ") && has(mt, " GiB   last 1 min") && !has(mt, "range"), "panel 4's history prints scale, not range: " + mt);
  const int mh = row_at(m, 0, "Windows · vmmemWSL");
  check(mh > 0 && count_braille(as_text({m[static_cast<size_t>(mh + plan_memory_rows(40).history)]})) > 0, "and its bottom row is drawn where a sample exists");
  check(has(as_text(m), "used avg 24.7 max 24.7 · range"), "panel 4's used and cache plots still zoom and print range");
  // A process at a few MiB: MiB, and never under 4 MiB.
  check(memory_scale_of(0.001).top * 1024 >= 4 && std::string(memory_scale_of(0.001).unit) == " MiB", "a process's memory scale is at least 4 MiB, in MiB");
  check(std::string(memory_scale_of(10.15).unit) == " GiB" && memory_scale_of(10.15).top == 20, "10.15 GiB is drawn to 20 GiB");
  check(std::string(memory_scale_of(900.0 / 1024).unit) == " MiB" && memory_scale_of(900.0 / 1024).top * 1024 == 1000, "900 MiB is drawn to 1000 MiB");
  // Panel 2's device graphs and panel 3's busy plots keep their gutter and axis.
  const std::string p2 = as_text(render(s, 131, 40, kNow, &demo, state_for(2)));
  check(has(p2, "100│") && has(p2, "  0│") && has(p2, "-60s") && has(p2, " now"), "panel 2's device graphs keep the gutter and the axis");
  const std::string p3 = as_text(render(s, 131, 40, kNow, &demo, state_for(3)));
  check(has(p3, "100 │") && has(p3, "  0 │") && has(p3, "-60s") && has(p3, " now"), "panel 3's busy plots keep the gutter and the axis");
}

// Each column of a row: its glyph and tone.
std::vector<std::pair<std::string, tone>> toned(const line& l) {
  std::vector<std::pair<std::string, tone>> out;
  for (const span& p : l) {
    for (size_t i = 0; i < p.text.size();) {
      std::string g;
      put_cp(g, next_cp(p.text, i));
      out.push_back({g, p.color});
    }
  }
  return out;
}

bool is_braille(const std::string& g) {
  size_t i = 0;
  const char32_t c = g.empty() ? 0 : next_cp(g, i);
  return c > 0x2800 && c <= 0x28FF;
}

// A Windows process history over `n` samples, the selected row's cores and the whole's given per second.
struct pow_fixture {
  snapshot s;
  history h;
};

pow_fixture part_of_whole(int n, const std::function<void(snapshot&, int)>& at) {
  pow_fixture f;
  for (int i = n - 1; i >= 0; --i) {
    snapshot s = contended();
    s.unix_ms = kNow - i * 1000;
    at(s, i);
    f.h.push(s);
    f.s = s;
  }
  return f;
}

// design/panel-grammar-slice2.md section 3: part of the whole on the Windows process histories of panels 3 and 4.
void check_part_of_whole() {
  // push copies the whole, NaN when it is not known.
  {
    history h;
    snapshot s = contended();
    h.push(s);
    check(std::fabs(h.samples().back().windows_cores_all - static_cast<float>(s.process_totals.cores)) < 1e-4 &&
              std::fabs(h.samples().back().windows_ws_all - static_cast<float>(gib(s.process_totals.working_set))) < 1e-4,
          "push copies process_totals: cores, and working sets in GiB");
    s.unix_ms += 1000;
    s.process_totals = {};
    h.push(s);
    check(std::isnan(h.samples().back().windows_cores_all) && std::isnan(h.samples().back().windows_ws_all), "a whole not known is NaN, never 0");
  }

  // contended at 131 by 40: two layers, every lit cell bar or track, no track at or below the part's top.
  const snapshot cs = contended();
  const history ch = contended_history(cs);
  std::vector<line> f = large(cs, ch, 131, 40);
  const int at = row_at(f, 0, "Windows · cpu_burn.exe");
  const std::string head = as_text({f[static_cast<size_t>(at)]});
  check(at > 0 && has(head, "all Windows processes avg") && has(head, "now ") && has(head, "% of all") && has(head, "scale 50   last 1 min"), "the heading names the whole, now and the whole's scale: " + head);
  check(has(as_text({f[static_cast<size_t>(at) + 1]}), "cpu_burn.exe") && has(as_text({f[static_cast<size_t>(at) + 1]}), "all other Windows processes"), "the legend names both layers");
  bool swatches = false;
  for (const span& p : f[static_cast<size_t>(at) + 1]) swatches = swatches || (p.text == "█" && p.color == tone::track);
  check(swatches && f[static_cast<size_t>(at) + 1][0].text == "█" && f[static_cast<size_t>(at) + 1][0].color == tone::bar, "its swatches in bar and track");
  const int plot_rows_n = 2;
  std::vector<std::vector<std::pair<std::string, tone>>> grid;
  for (int y = 0; y < plot_rows_n; ++y) grid.push_back(toned(f[static_cast<size_t>(at + 2 + y)]));
  bool lit_ok = true, order_ok = true, any_track = false, any_bar = false;
  for (size_t c = 0; c < grid[0].size(); ++c) {
    bool part_seen = false;
    for (int y = 0; y < plot_rows_n; ++y) {
      const auto& [g, t] = grid[static_cast<size_t>(y)][c];
      if (!is_braille(g)) continue;
      lit_ok = lit_ok && (t == tone::bar || t == tone::track);
      any_track = any_track || t == tone::track;
      any_bar = any_bar || t == tone::bar;
      if (t == tone::bar) part_seen = true;
      else if (part_seen) order_ok = false;  // A track cell under a part cell
    }
  }
  check(lit_ok && any_track && any_bar, "every lit cell of the history is bar or track, and both are drawn");
  check(order_ok, "no track cell lies at or below the part's top in its column");

  // The part is held at or under the whole: a whole that drops under the part's figure draws the whole's height.
  {
    pow_fixture p = part_of_whole(40, [](snapshot& s, int i) {
      for (process_row& r : s.processes) {
        if (r.name == "cpu_burn.exe") r.cores = i < 20 ? 10 : 1;
      }
      s.process_totals = process_totals_of(s);
      if (i < 20) s.process_totals.cores = 5;  // The part read 10 against a whole of 5: the whole is the most it draws
      else s.process_totals.cores = 20;
    });
    const std::vector<line> hist = process_history(p.h, nullptr, 0, 0, 0, 60000);  // nothing selected: a title and blank rows
    check(hist.size() == 1, "no process: the title alone");
    const proc_id id{0, 31000, 0, "cpu_burn.exe"};
    const std::vector<line> g = process_history(p.h, &id, 80, 4, kNow, 60000);  // heading, legend, 3 plot rows
    const auto top_row = toned(g[2]), mid_row = toned(g[3]);
    // Scale 20, 3 rows of 4 dots: the whole at 5 is 3 dots, the bottom row; the part held at 5 too, never 10 (6 dots).
    const size_t last = top_row.size() - 1;
    check(!is_braille(top_row[last].first) && !is_braille(mid_row[last].first), "the part is drawn no higher than the whole: nothing above its row");
    check(has(as_text({g[0]}), "scale 20"), "the scale is the whole's maximum rounded up");
  }

  // One Windows process: one series, no legend, no now, and the plot has the legend's row.
  {
    snapshot one = contended();
    one.processes.resize(2);  // Idle and cpu_burn.exe
    one.process_totals = process_totals_of(one);
    history h;
    for (int i = 30; i >= 0; --i) {
      snapshot t = one;
      t.unix_ms = kNow - i * 1000;
      h.push(t);
    }
    const proc_id id{0, 31000, 0, "cpu_burn.exe"};
    const std::vector<line> g = process_history(h, &id, 120, 4, kNow, 60000);
    const std::string t = as_text(g);
    check(g.size() == 5 && !has(t, "all other") && !has(t, "now ") && !has(t, "all Windows processes") && count_braille(as_text({g[4]})) > 0 && has(t, "avg 13.25"),
          "one Windows process: one series, its plot 4 rows, no legend, no share");
  }
  // A part equal to the whole for the first half and below it after: part of the whole.
  {
    pow_fixture p = part_of_whole(40, [](snapshot& s, int i) {
      if (i >= 20) s.processes.resize(2);
      s.process_totals = process_totals_of(s);
    });
    const proc_id id{0, 31000, 0, "cpu_burn.exe"};
    check(has(as_text(process_history(p.h, &id, 160, 4, kNow, 60000)), "all other Windows processes"), "a second row appearing inside the window makes two layers");
  }
  // WSL, panels 3 and 4: one series always.
  for (const int panel : {3, 4}) {
    view_state v = state_for(panel);
    apply_key(v, key_event{key::tab}, cs);
    const std::string t = as_text(render(cs, 160, 40, kNow, &ch, v));
    const std::string w = line_with(render(cs, 160, 40, kNow, &ch, v), "WSL · ");
    check(!w.empty() && !has(t, "all other") && !has(w, "now ") && !has(t, "all WSL processes") && (panel == 4 || has(w, "guest's count")), "the guest's history is one series on panel " + std::to_string(panel) + ": " + w);
  }
  // Panel 4: from 0, Σ working sets, never `used` or `range` in the history.
  {
    const snapshot b = busy();
    const history demo = demo_minute(b);
    const std::vector<line> m = render(b, 200, 40, kNow, &demo, state_for(4));
    const std::string mh = line_with(m, "Windows · vmmemWSL 7120 · working set");
    check(has(mh, "Σ working sets, all Windows processes avg") && !has(mh, "used") && !has(mh, "range"), "panel 4's heading names its whole for its sum: " + mh);
    check(has(line_with(m, "Windows · vmmemWSL 7120 · working set", 1), "all other Windows processes' working sets"), "and its legend");
    const int r = row_at(m, 0, "Windows · vmmemWSL");
    check(r > 0 && count_braille(as_text({m[static_cast<size_t>(r + plan_memory_rows(40).history)]})) > 0, "and its plot starts at 0");
  }
  // Giving way (Q6): 3 rows, the legend and 2 plot rows; 2 rows, no legend, and the heading still names the whole.
  {
    const proc_id id{0, 31000, 0, "cpu_burn.exe"};
    const std::vector<line> three = process_history(ch, &id, 160, 3, axis_end(ch, kNow), 60000), two = process_history(ch, &id, 160, 2, axis_end(ch, kNow), 60000);
    check(three.size() == 4 && has(as_text({three[1]}), "all other Windows processes"), "3 rows: heading, legend and 2 plot rows");
    check(two.size() == 3 && !has(as_text(two), "all other") && has(as_text({two[0]}), "all Windows processes"), "2 rows: no legend, 2 plot rows, the heading names the whole");
    const std::vector<line> narrow = process_history(ch, &id, 100, 2, axis_end(ch, kNow), 60000);
    check(has(as_text({narrow[0]}), "all Windows processes") && has(as_text({narrow[0]}), "last 1 min"), "narrow, the whole keeps its name before its figures and now");
  }
  // now: the latest sample where both are known; blank on a whole of 0 or NaN; <1% under half a percent.
  {
    const time_axis a = make_axis(ch, axis_end(ch, kNow), 60000, 30, 2);
    const auto part = [](double v) { return [v](const history_sample&) { return v; }; };
    check(share_text(a, part(1), part(0)).empty(), "now is blank when the whole is 0");
    check(share_text(a, part(1), part(kNaN)).empty(), "and when it is not known");
    check(share_text(a, part(0.004), part(1)) == "<1%" && share_text(a, part(0.005), part(1)) == "1%" && share_text(a, part(0.86), part(1)) == "86%", "<1% under half a percent, else whole percents");
  }
  // The max dots: the whole's in track, the part's in bar; a part's dot at or under its own mean is not drawn.
  {
    std::vector<double> whole(4, 20), part(4, 5), raw(4, 5);
    window_stats ws, ps;
    ws.max = 90; ws.max_at = 0; ws.max_count = 3;
    ps.max = 60; ps.max_at = 3; ps.max_count = 3;
    const std::vector<line> b = plot_part_block(whole, part, raw, ws, ps, 1, 100, 0);
    const auto cells = toned(b[0]);
    size_t i0 = 0, i1 = 0;
    const unsigned c0 = static_cast<unsigned>(next_cp(cells[0].first, i0) - 0x2800), c1 = static_cast<unsigned>(next_cp(cells[1].first, i1) - 0x2800);
    check((c0 & kDotLeft[3]) && cells[0].second == tone::bar, "the whole's dot in its column; that cell holds the part, so it is the part's colour");
    check((c1 & kDotRight[1]) && cells[1].second == tone::bar, "the part's dot in its column, at 60 of 100 the second dot, in bar");
    ps.max = 5;
    const auto none = toned(plot_part_block(whole, part, raw, ws, ps, 1, 100, 0)[0]);
    size_t i2 = 0;
    check(!(static_cast<unsigned>(next_cp(none[1].first, i2) - 0x2800) & kDotRight[1]), "a part's dot at its own mean's level is not drawn");
    std::vector<double> zero_part(4, 0);
    const auto rest = toned(plot_part_block(whole, zero_part, zero_part, ws, window_stats{}, 1, 100, 0)[0]);
    check(rest[1].second == tone::track, "a cell with no part is the rest's, in track");
    std::vector<double> zero(4, 0);
    const auto zeros = toned(plot_part_block(zero, zero, zero, window_stats{}, window_stats{}, 1, 100, 0)[0]);
    check(zeros[0].first == "⣀" && zeros[0].second == tone::bar, "a whole that measured 0 is ⣀ in bar");
  }
  // The heading at every width: never wider than the screen, `last` present while the left side fits.
  for (int w = 40; w <= 300; ++w) {
    const proc_id id{0, 31000, 0, "cpu_burn.exe"};
    const std::vector<line> g = process_history(ch, &id, w, 3, axis_end(ch, kNow), 60000);
    const std::string t = as_text({g[0]});
    check(width_of(g[0]) <= w, "the heading fits at " + std::to_string(w));
    if (w >= 37 + 3 + 10) check(has(t, "last 1 min"), "and keeps `last 1 min` at " + std::to_string(w));
  }
  check(vt_frame(large(cs, ch, 131, 40), 40).size() < 48 * 1024, "two-tone histories stay under the frame budget");
}

// A minute of busy() on panel 2, each second changed by `at` (age in seconds, 0 the newest), pushed with what the
// view keeps selected.
struct gpu_fixture {
  snapshot s;
  history h;
};

gpu_fixture gpu_minute(int seconds, const std::function<void(snapshot&, int)>& at, const view_state& v = state_for(2), int64_t end = kNow) {
  gpu_fixture f;
  for (int age = seconds; age >= 0; --age) {
    snapshot s = busy();
    for (process_row& p : s.processes) p.create_time = 1000 + p.pid;
    s.unix_ms = end - age * 1000;
    at(s, age);
    s.process_totals = process_totals_of(s);
    f.h.push(s, selected_processes(s, v));
    f.s = s;
  }
  return f;
}

std::string marked_row(const std::vector<line>& f) { return line_with(f, "▶"); }

// design/panel-grammar-slice2.md section 5: panel 2's Windows list selectable, its rows from the range on screen and
// the local holders, the WSL list a reference list, and the GPU history below.
void check_gpu_panel() {
  const key_event down{key::down}, up{key::up}, tab{key::tab};
  // The panel at three sizes: one ▶, in the Windows list; ↑↓ and no tab; no top N of M on the Windows list.
  {
    const gpu_fixture g = gpu_minute(60, [](snapshot&, int) {});
    for (const auto& [w, rows] : {std::pair{80, 24}, std::pair{100, 34}, std::pair{131, 40}}) {
      const std::vector<line> f = render(g.s, w, rows, g.s.unix_ms, &g.h, state_for(2));
      const std::string t = as_text(f), n = " at " + std::to_string(w) + "x" + std::to_string(rows);
      check(frame_fits(f, w, rows), "panel 2 fits" + n);
      if (!has(t, "PROCESSES")) continue;
      check(count_of(t, "▶") == 1 && marked_row(f).find("▶") < marked_row(f).find("vmwp.exe"), "one ▶, on the Windows list" + n);
      check(has(as_text({f.back()}), "↑↓ select") && !has(as_text({f.back()}), "tab"), "↑↓ offered, tab not" + n);
      check(!has(line_with(f, "by GPU %, busiest engine"), "top ") && has(t, "\n2 of 2") , "the Windows heading drops top N of M; N of M under it" + n);
      bool wsl_bg = false;
      for (const line& l : f) {
        const std::string r = as_text({l});
        if (!has(r, "python3") && !has(r, "한글추론기")) continue;
        for (const span& p : l) wsl_bg = wsl_bg || (p.bg256 == kSelectBg && (has(p.text, "python3") || has(p.text, "한글추론기")));
      }
      check(!wsl_bg, "the WSL holders have no selection background" + n);
    }
    view_state v = state_for(2);
    check(apply_key(v, tab, g.s, g.h, g.s.unix_ms) && v.panels[2].focus == 0, "tab changes nothing on panel 2");
    const std::string hist = line_with(render(g.s, 131, 40, g.s.unix_ms, &g.h, state_for(2)), "Windows · vmwp.exe 11736 · GPU %, busiest engine");
    check(!hist.empty() && has(hist, "last 1 min") && !has(hist, "scale") && !has(hist, "all Windows processes") && !has(hist, "*"), "the history: GPU %, busiest engine, last 1 min, no scale, no whole: " + hist);
    check(!has(as_text(render(g.s, 131, 40, g.s.unix_ms, &g.h, state_for(2))), "all other"), "and no legend");
  }

  // Membership and order over the range: 9001 used an engine until 30 s ago.
  {
    const auto used_until_30s = [](snapshot& s, int age) {
      if (age >= 30 && age <= 40) s.gpu_engines.push_back({9001, "3D", 20});
    };
    gpu_fixture g = gpu_minute(60, used_until_30s);
    std::vector<gpu_row> rows = gpu_rows(g.s, &g.h, axis_end(g.h, g.s.unix_ms), 60000);
    check(rows.size() == 3 && rows[0].key.pid == 11736 && rows[1].key.pid == 9002 && rows[2].key.pid == 9001, "by GPU % now, then the one used 30 s ago");
    view_state v = state_for(2);
    apply_key(v, down, g.s, g.h, g.s.unix_ms);
    apply_key(v, down, g.s, g.h, g.s.unix_ms);
    check(v.panels[2].rows[0].pid == 9001 && v.panels[2].rows[0].start == 1000 + 9001, "↓↓ selects it, with its start");
    check(has(marked_row(render(g.s, 131, 40, g.s.unix_ms, &g.h, v)), "9001"), "it is drawn selected");
    // Seconds later: listed while its last use is inside the minute, gone the sample it leaves, the selection to the first row.
    for (int k = 1; k <= 30; ++k) {
      snapshot s = g.s;
      s.unix_ms = kNow + k * 1000;
      g.h.push(s, selected_processes(s, v));
      g.s = s;
      const std::vector<line> f = render(s, 131, 40, s.unix_ms, &g.h, v);
      const std::vector<std::vector<row_key>> keys = selectable_lists(s, 2, &g.h, s.unix_ms, 60000);
      rows = gpu_rows(s, &g.h, axis_end(g.h, s.unix_ms), 60000);
      bool same = keys[0].size() == rows.size();
      for (size_t i = 0; same && i < rows.size(); ++i) same = keys[0][i].pid == rows[i].key.pid;
      check(same, "the keys move through the rows drawn, " + std::to_string(k) + " s on");
      if (k == 29) check(rows.size() == 3 && has(marked_row(f), "9001"), "29 s on: still listed and selected");
      if (k == 30) {
        check(rows.size() == 2 && has(marked_row(f), "11736"), "30 s on, its use out of the range: gone, the selection on the first row");
        view_state moved = v;
        apply_key(moved, down, s, g.h, s.unix_ms);
        check(moved.panels[2].rows[0].pid == rows[1].key.pid && has(marked_row(render(s, 131, 40, s.unix_ms, &g.h, moved)), std::to_string(rows[1].key.pid)), "↓ then moves to the second row drawn");
      }
    }
    // At 10 minutes it is back: its use lies inside that range.
    view_state ten = v;
    ten.range = 2;
    check(gpu_rows(g.s, &g.h, axis_end(g.h, g.s.unix_ms), 600000).size() == 3 && has(marked_row(render(g.s, 131, 40, g.s.unix_ms, &g.h, ten)), "9001"), "on 10 minutes it is listed and selected again");
  }
  // Order: GPU % now, then most recent use, then holders by local memory; a local holder without use is listed.
  {
    const gpu_fixture g = gpu_minute(20, [](snapshot& s, int age) {
      s.gpu_engines = {{11736, "3D", 10}, {9002, "3D", 10}};
      if (age == 10) s.gpu_engines.push_back({9001, "3D", 5});
      if (age == 15) s.gpu_engines.push_back({4, "Copy", 5});
      s.gpu_memory = {{10000, 0, 0, 2ull << 30}, {10004, 0, 0, 3ull << 30}, {11736, 0, 0, 1}};
    });
    const std::vector<gpu_row> r = gpu_rows(g.s, &g.h, axis_end(g.h, g.s.unix_ms), 60000);
    std::string order;
    for (const gpu_row& x : r) order += std::to_string(x.key.pid) + " ";
    check(order == "9002 11736 9001 4 10004 10000 ", "GPU % now (a tie by pid), then most recent use, then holders by local: " + order);
    check(has(as_text(render(g.s, 131, 50, g.s.unix_ms, &g.h, state_for(2))), "10004 svchost.exe"), "a local holder with no engine use is listed");
  }
  // The history: 112 drawn at the ceiling, printed as measured in watch, no *; a gap where the engines were not read;
  // ⣀ where they were and the process had no row.
  {
    const gpu_fixture g = gpu_minute(60, [](snapshot& s, int age) {
      if (age == 42) s.gpu_engines = {{11736, "3D", 112}};
      if (age >= 15 && age <= 25) s.gpu_engines_known = false;
      s.gpu_memory.push_back({10000, 0, 0, 1ull << 30});  // svchost 10000 holds memory and never uses an engine
    });
    const std::vector<line> f = render(g.s, 131, 40, g.s.unix_ms, &g.h, state_for(2));
    const int at = row_at(f, 0, "Windows · vmwp.exe");
    bool watch = false;
    for (const span& p : f[static_cast<size_t>(at)]) watch = watch || (has(p.text, "max 112%") && p.color == tone::watch);
    check(at > 0 && watch && !has(as_text({f[static_cast<size_t>(at)]}), "*"), "112 is printed as measured, in watch, with no *");
    const int top_lit = count_braille(as_text({f[static_cast<size_t>(at) + 1]}));
    check(top_lit >= 1 && top_lit <= 4, "drawn at the ceiling: only the 112's cells reach the top row, " + std::to_string(top_lit));
    view_state v = state_for(2);
    for (int i = 0; i < 2; ++i) apply_key(v, down, g.s, g.h, g.s.unix_ms);
    const std::vector<line> hf = render(g.s, 200, 40, g.s.unix_ms, &g.h, v);
    const int ha = row_at(hf, 0, "Windows · svchost.exe 10000");
    check(ha > 0, "the holder's history is shown");
    if (ha > 0) {
      size_t last = static_cast<size_t>(ha) + 1;
      while (last + 2 < hf.size() && !as_text({hf[last + 1]}).empty() && as_text({hf[last + 1]})[0] != '1') ++last;
      const std::string bottom = as_text({hf[last]});
      check(count_of(bottom, "⣀") > 50 && has(bottom, "⣀          "), "a holder with no engine row draws ⣀, with a gap where the engines were not read: " + bottom);
    }
  }
  // A gpu series made late: 0 where the process was there and the engines read, NaN before it was there.
  {
    const gpu_fixture g = gpu_minute(30, [](snapshot& s, int age) {
      if (age > 20) s.processes.erase(std::remove_if(s.processes.begin(), s.processes.end(), [](const process_row& p) { return p.pid == 9001; }), s.processes.end());
      if (age >= 14 && age <= 16) s.gpu_engines_known = false;
      if (age <= 5) s.gpu_engines.push_back({9001, "3D", 30});
    });
    const int slot = g.h.slot_of({0, 9001, 1000 + 9001, "카카오톡.exe"});
    const sample_range all = g.h.samples();
    const auto at_age = [&](int age) { return g.h.gpu_at(slot, g.h.position(all[all.size() - 1 - static_cast<size_t>(age)])); };
    check(slot >= 0 && at_age(0) == 30 && at_age(10) == 0 && std::isnan(at_age(15)) && std::isnan(at_age(25)), "made late: 0 when there and read, NaN when unread or not yet there");
  }
  // A pid reused under the same name does not keep the selection.
  {
    gpu_fixture g = gpu_minute(10, [](snapshot&, int) {});
    view_state v = state_for(2);
    apply_key(v, down, g.s, g.h, g.s.unix_ms);
    check(v.panels[2].rows[0].pid == 9002, "9002 selected");
    snapshot reused = g.s;
    for (process_row& p : reused.processes) {
      if (p.pid == 9002) p.create_time = 777;
    }
    // The old 9002 used an engine inside the range, so it stays listed from its slot, n/a now, and keeps the
    // selection; the new 9002 is another row.
    const std::vector<line> rf = render(reused, 131, 40, reused.unix_ms, &g.h, v);
    check(count_of(as_text(rf), " 9002 Game.Shipping") == 2 && has(marked_row(rf), "9002") && has(marked_row(rf), "n/a"), "the same pid and name with another start is another row: the selection stays with the old one");
    // A holder that never used an engine is not kept by the ring: its pid reused, the selection falls to the first row.
    gpu_fixture hold = gpu_minute(10, [](snapshot& t, int) { t.gpu_memory.push_back({10000, 0, 0, 1ull << 30}); });
    view_state hv = state_for(2);
    for (int i = 0; i < 2; ++i) apply_key(hv, down, hold.s, hold.h, hold.s.unix_ms);
    check(hv.panels[2].rows[0].pid == 10000, "the holder 10000 selected");
    snapshot hreused = hold.s;
    for (process_row& p : hreused.processes) {
      if (p.pid == 10000) p.create_time = 777;
    }
    check(has(marked_row(render(hreused, 131, 40, hreused.unix_ms, &hold.h, hv)), "11736"), "its pid reused under the same name: the selection falls to the first row");
    check(selected_processes(g.s, v).size() == 1 && selected_processes(g.s, v)[0].pid == 9002 && selected_processes(g.s, v)[0].start == 1000 + 9002, "selected_processes names panel 2's selection");
  }
  // At the cap: a process that used an engine and exited is listed under its own name, its key carries its slot's
  // start, and selecting it keeps its slot while 50 new processes arrive each second.
  {
    constexpr int64_t T0 = 1793000000000LL;
    const auto win = [](uint32_t pid, const std::string& name, uint64_t start) {
      process_row p;
      p.pid = pid; p.name = name; p.create_time = start; p.cores = 0.1; p.cores_known = true; p.working_set = 1 << 20;
      return p;
    };
    std::vector<process_row> base;
    for (uint32_t i = 1; i <= 390; ++i) base.push_back(win(i, "p", i));
    history h;
    view_state v = state_for(2);
    snapshot s;
    int64_t t = T0;
    for (int i = 0; i < 5; ++i, t += 1000) {
      s = snapshot{};
      s.unix_ms = t;
      s.interval_s = 1;
      s.processes = base;
      s.processes.push_back(win(5000, "x.exe", 42));
      s.gpu_engines_known = true;
      s.gpu_engines = {{5000, "3D", 70}};
      if (i == 4) apply_key(v, up, s, h, t);  // Moved to: the key is set
      h.push(s, selected_processes(s, v));
    }
    uint32_t next = 100000;
    for (int i = 0; i < 30; ++i, t += 1000) {
      s = snapshot{};
      s.unix_ms = t;
      s.interval_s = 1;
      s.processes = base;
      for (int k = 0; k < 50; ++k, ++next) s.processes.push_back(win(next, "cc", next));
      s.gpu_engines_known = true;
      h.push(s, selected_processes(s, v));
    }
    const std::vector<gpu_row> r = gpu_rows(s, &h, axis_end(h, s.unix_ms), 60000);
    check(v.panels[2].rows[0].pid == 5000 && v.panels[2].rows[0].start == 42, "the key carries the start");
    check(r.size() == 1 && r[0].key.pid == 5000 && r[0].key.name == "x.exe" && r[0].key.start == 42 && std::isnan(r[0].now), "an exited process that used an engine 30 s ago is listed under its own name and start, n/a now");
    check(h.slot_of({0, 5000, 42, "x.exe"}) >= 0 && h.slots_allocated() <= 1024, "its slot survives the cap");
    const std::vector<line> f = render(s, 131, 40, s.unix_ms, &h, v);
    check(has(marked_row(f), "x.exe") && has(as_text(f), "Windows · x.exe 5000 · GPU %, busiest engine"), "and it is drawn selected, with its history");
  }
  // A reused slot carries nothing of its last occupant's GPU; a clock step back moves gpu_seen_ms with the samples.
  {
    constexpr int64_t T0 = 1793000000000LL;
    const auto one = [](int64_t t, uint32_t pid, uint64_t start, double gpu) {
      snapshot s;
      s.unix_ms = t;
      s.interval_s = 1;
      process_row p;
      p.pid = pid; p.name = "g.exe"; p.create_time = start; p.working_set = 1 << 20;
      s.processes = {p};
      s.gpu_engines_known = true;
      if (gpu > 0) s.gpu_engines = {{pid, "3D", gpu}};
      return s;
    };
    history h;
    h.push(one(T0, 7, 1, 80));
    const int first = h.slot_of({0, 7, 1, "g.exe"});
    h.push(one(T0 + 601000, 8, 2, 0));  // 7 gone for what is kept: let go
    h.push(one(T0 + 602000, 9, 3, 0));  // 9 takes the slot let go
    const int reused = h.slot_of({0, 9, 3, "g.exe"});
    check(first >= 0 && reused == first && h.gpu_seen_ms(reused) == 0 && h.gpu_at(reused, h.position(h.samples().back())) == 0, "a reused slot has no gpu series and no gpu_seen_ms of the last occupant");
    history step;
    step.push(one(T0, 7, 1, 80));
    step.push(one(T0 + 1000, 7, 1, 80));
    step.push(one(T0 - 3600000, 7, 1, 0));  // The wall clock stepped back an hour
    const int sl = step.slot_of({0, 7, 1, "g.exe"});
    const int64_t end = step.samples().back().unix_ms;
    check(sl >= 0 && step.gpu_seen_ms(sl) == end - 1000 && end - step.gpu_seen_ms(sl) < 60000, "after a clock step back gpu_seen_ms lies in the range it did before");
  }
  // Every size, three fixtures, and the frame budget.
  {
    const gpu_fixture g = gpu_minute(60, [](snapshot&, int) {});
    snapshot hostile = g.s;
    hostile.processes.push_back(hostile.processes[5]);
    hostile.processes.back().pid = 31337;
    hostile.processes.back().name = "\x1b]0;x\x07\x1b[2Jgpu.exe";
    hostile.gpu_engines.push_back({31337, "3D", 99});
    const snapshot b = bare();
    const history bh = demo_minute(b);
    for (int w = 1; w <= 300; w += 3) {
      for (int rows = 1; rows <= 66; rows += 5) {
        const std::string n = " " + std::to_string(w) + "x" + std::to_string(rows);
        check(frame_fits(render(g.s, w, rows, g.s.unix_ms, &g.h, state_for(2)), w, rows), "panel 2 fits" + n);
        check(frame_fits(render(hostile, w, rows, g.s.unix_ms, &g.h, state_for(2)), w, rows), "panel 2 with a hostile name fits" + n);
        check(frame_fits(render(b, w, rows, kNow, &bh, state_for(2)), w, rows), "bare panel 2 fits" + n);
      }
    }
    check(vt_clean(vt_frame(render(hostile, 131, 40, g.s.unix_ms, &g.h, state_for(2)), 40)), "a hostile name on panel 2, list and history, stays safe");
    check(vt_frame(render(g.s, 131, 40, g.s.unix_ms, &g.h, state_for(2)), 40).size() < 48 * 1024, "panel 2 under the frame budget");
  }
}

// design/panel-grammar.md part 2, "Keys" and "Decoding": key_for on every row of the key table.
void check_keys() {
  const auto is = [](const key_event& e, key k, int panel = 0, bool esc_first = false) { return e.k == k && e.panel == panel && e.esc_first == esc_first; };
  const auto none = [&](const key_event& e) { return is(e, key::none); };
  for (int n = 1; n <= 8; ++n) {
    check(is(key_for(static_cast<uint16_t>('0' + n), static_cast<char32_t>('0' + n), 0), key::panel, n), "digit " + std::to_string(n) + " is its panel");
    check(is(key_for(static_cast<uint16_t>(vk::numpad0 + n), static_cast<char32_t>('0' + n), 0), key::panel, n), "numpad " + std::to_string(n) + " with NumLock on is its panel");
    check(is(key_for(static_cast<uint16_t>('0' + n), U'!', kShift), key::panel, n), "Shift with a digit is still the digit " + std::to_string(n));
  }
  check(none(key_for('9', U'9', 0)) && none(key_for('0', U'0', 0)), "9 and 0 give nothing");
  check(none(key_for(vk::numpad0 + 9, U'9', 0)) && none(key_for(vk::numpad0, U'0', 0)), "numpad 9 and 0 give nothing");
  check(none(key_for('T', U't', 0)) && none(key_for('T', U'T', kShift)), "t gives nothing: the range replaces it");
  check(is(key_for(vk::up, 0, 0), key::up) && is(key_for(vk::down, 0, 0), key::down), "up and down");
  check(is(key_for(vk::left, 0, 0), key::left) && is(key_for(vk::right, 0, 0), key::right), "left and right");
  check(is(key_for(vk::up, 0, kShift), key::up), "NumLock off: the numpad's arrows arrive as the arrows, Shift or not");
  check(is(key_for(vk::tab, U'\t', 0), key::tab), "tab");
  check(none(key_for(vk::tab, U'\t', kShift)) && none(key_for(vk::tab, U'\t', kShift | kLeftAlt)), "Shift+Tab is dropped whole");
  check(is(key_for(vk::escape, 0x1B, 0), key::esc), "esc");
  check(is(key_for('Q', U'q', 0), key::quit) && is(key_for('Q', U'Q', kShift), key::quit), "q quits, Shift or not");
  check(is(key_for('S', U's', 0), key::sort), "s is decoded, though it sorts nothing yet");
  check(none(key_for('Q', 0x11, kLeftCtrl)) && none(key_for('Q', 0x11, kRightCtrl)) && none(key_for('3', U'3', kLeftCtrl)), "Ctrl+Q gives nothing, nor Ctrl with a digit");
  check(is(key_for('Q', U'q', kLeftAlt), key::quit, 0, true) && is(key_for('Q', U'q', kRightAlt), key::quit, 0, true), "Alt+Q is esc, then q");
  check(is(key_for('3', U'3', kLeftAlt), key::panel, 3, true), "Alt+3 is esc, then 3");
  check(is(key_for(vk::escape, 0x1B, kLeftAlt), key::esc, 0, true), "Alt+esc is esc twice");
  check(is(key_for('T', U't', kLeftAlt), key::esc), "Alt with a key that does nothing is esc alone");
  check(none(key_for('Q', U'q', kLeftCtrl | kRightAlt)) && none(key_for('Q', U'q', kLeftCtrl | kLeftAlt)), "Ctrl+Alt+Q, and AltGr, give nothing");
  check(is(key_for(vk::up, 0, kLeftAlt), key::up), "Alt with an arrow is the arrow: a terminal sends one sequence");
  check(none(key_for(vk::numpad0 + 1, 0, kLeftAlt)), "Alt with a numpad digit is Windows' Alt code entry, not a key");
  for (const uint16_t m : {vk::shift, vk::control, vk::menu, vk::capital, vk::lwin, vk::rwin, vk::numlock, vk::scroll, vk::lshift, vk::rmenu}) {
    check(none(key_for(m, 0, 0)) && none(key_for(m, 0, kLeftAlt | kShift)), "a modifier or a lock alone is no key: " + std::to_string(m));
  }
  check(none(key_for(0x2E, 0, 0)) && none(key_for(0x70, 0, kLeftAlt)), "Delete and F1, with Alt or not, give nothing");
  // Korean input on: the virtual key decides first; under VK_PROCESSKEY the jamo does.
  check(is(key_for('Q', 0x3142, 0), key::quit), "ㅂ on the Q key is q by its key code");
  check(is(key_for(vk::processkey, 0x3142, 0), key::quit), "VK_PROCESSKEY with ㅂ is q");
  check(is(key_for(vk::processkey, 0x3134, 0), key::sort), "VK_PROCESSKEY with ㄴ is s");
  check(is(key_for(vk::processkey, 0x3142, kLeftAlt), key::quit, 0, true), "and Alt with it is esc, then q");
  check(none(key_for(vk::processkey, 0x3142, kLeftCtrl)), "and Ctrl with it is nothing");
  for (int i = 0; i < 26; ++i) {
    const char letter = static_cast<char>('a' + i);
    const key_event by_jamo = key_for(vk::processkey, kJamo[i], 0), by_key = key_for(static_cast<uint16_t>('A' + i), static_cast<char32_t>(letter), 0);
    check(latin_for(kJamo[i]) == letter && by_jamo.k == by_key.k, std::string("the jamo for ") + letter + " does what " + letter + " does");
  }
  // machtop's table, letter by letter: the 2-set layout.
  check(kJamo['q' - 'a'] == U'ㅂ' && kJamo['s' - 'a'] == U'ㄴ' && kJamo['a' - 'a'] == U'ㅁ' && kJamo['z' - 'a'] == U'ㅋ' && kJamo['p' - 'a'] == U'ㅔ', "the 2-set table");
  check(latin_for(U'가') == 0 && latin_for(0) == 0 && none(key_for(vk::processkey, U'가', 0)) && none(key_for(vk::processkey, 0, 0)), "a composed syllable or no character is nothing");
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
  const std::vector<line> pf = render(rule_sample(kNow, 0, 999), 131, 40, kNow, &spike, state_for(2, 600));
  const std::string top = panel_cells(upper_graph_rows(as_text(pf)).substr(0, upper_graph_rows(as_text(pf)).find('\n') + 1), 0);
  check(count_braille(top) == 1, "panel 2 at 10 minutes: the maximum's dot on the device graph's top row: " + top);
  check(has(line_with(pf, "-10m"), "max 100%"), "and the label says 100");

  // avg and max, from the samples, by hand.
  history hand;
  for (const double v : {10.0, 20.0, 30.0, 40.0}) hand.push(rule_sample(kNow - static_cast<int64_t>(4 - v / 10) * 1000, v, static_cast<int>(v)));
  const window_stats st = series_stats(hand, &history_sample::gpu_util, kNow, 60000);
  check(st.avg == 25 && st.max == 40, "avg 25 and max 40 of 10, 20, 30, 40");
  check(has(line_with(render(rule_sample(kNow, 40, 99), 131, 40, kNow, &hand, state_for(2)), "-60s"), "avg 25% max 40%"), "and the device graph says so");
  check(std::isnan(series_stats(hand, &history_sample::gpu_util, kNow + 61000, 60000).avg), "a window with no sample has no figures");
  history gaps;
  for (int age = 60; age >= 0; --age) gaps.push(sample_at(bare(), kNow - age * 1000, 0, 0, 0));
  const std::string blank = as_text(render(bare(), 131, 40, kNow, &gaps, state_for(2)));
  check(!has(blank, "avg") && !has(blank, "max"), "no label where every sample in the range is a gap");
  check(has(as_text(render(bare(), 99, 40, kNow, &gaps, state_for(3))), "avg") && !has(panel_cells(graph_rows(as_text(render(bare(), 99, 40, kNow, &gaps, state_for(3)))), 1), "⣀"), "the Windows graph is labelled while the WSL one, with no line, is blank");

  // Over 100: drawn at the ceiling, printed as measured in the watch tone, and the split's * with its caption.
  const history demo = demo_minute(base);
  const std::vector<line> df = render(base, 131, 40, kNow, &demo, state_for(2));
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
  const std::string panel_1 = as_text(render(base, 131, 40, kNow, &demo, state_for(1)));
  check(!has(panel_1, "shares*"), "panel 1 has no graph, so no *");
  history under;
  for (int age = 60; age >= 0; --age) under.push(sample_at(base, kNow - age * 1000, 50, 100, 40));
  const std::string uf = as_text(render(base, 131, 40, kNow, &under, state_for(2)));
  check(!has(uf, "*") && has(uf, "max 100%"), "every split value at or under 100: no * anywhere");
  history over_windows;
  for (int age = 60; age >= 0; --age) over_windows.push(sample_at(base, kNow - age * 1000, 50, 30, age == 10 ? 104 : 40));
  const std::string ow = as_text(render(base, 131, 40, kNow, &over_windows, state_for(2)));
  check(has(ow, "max 104%*") && has(ow, "shares*"), "the Windows side over 100 carries it too");
  check(!has(as_text(render(base, 131, 40, kNow, &over_windows, state_for(2, 60))), "max 30%*"), "a side under 100 has no * of its own");
  history hot_power;
  for (int age = 60; age >= 0; --age) {
    snapshot p = sample_at(base, kNow - age * 1000, 50, 30, 40);
    p.gpu_device.power_w = age == 5 ? 210 : 150;
    hot_power.push(p);
  }
  bool power_watched = false;
  for (const line& l : render(base, 131, 40, kNow, &hot_power, state_for(2))) {
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
  check(std::isnan(nl.samples().back().gpu_power_w) && !has(line_with(render(limitless, 131, 40, kNow, &nl, state_for(2)), " W ─"), "W"), "no limit, no height: no label either");
  snapshot huge = base;
  huge.gpu_split = {true, 1e300, -5, 1, 1};
  huge.unix_ms = kNow;
  history hh;
  hh.push(huge);
  const std::string ht = as_text(render(huge, 131, 50, kNow, &hh, state_for(2)));
  check(has(ht, "max 1e+300%*") && has(ht, "avg -5% max -5%"), "huge and negative figures are printed as measured: " + line_with(render(huge, 131, 50, kNow, &hh, state_for(2)), "-60s"));

  // Scale by measure: 1, 2 or 5 times a power of ten, never below the floor.
  const struct { double max, floor, top; } scales[] = {{13.25, 0.05, 20}, {0.01, 0.05, 0.05}, {1, 0.05, 1}, {1.0001, 0.05, 2}, {2.5, 0.05, 5}, {7, 0.05, 10},
                                                       {0, 0.05, 0.05}, {kNaN, 0.05, 0.05}, {100, 0.05, 100}, {0.051, 0.05, 0.1}, {-3, 0.05, 0.05}, {kInf, 0.05, 0.05}};
  for (const auto& sc : scales) check(scale_top(sc.max, sc.floor) == sc.top, fmt("scale %g for %g", sc.top, sc.max));
  check(plot_rows({10, 10}, 1, -1, 0, 20) == plot_rows({50, 50}, 1), "a scaled graph draws its value against its top");

  // No minimum and no old caption anywhere; every labelled axis keeps its range and now, at every width.
  snapshot c;
  history ch;
  cpu_fixture("contended", 600, c, ch);
  for (const int span_s : {60, 300, 600}) {
    const std::string range = "-" + span_text(span_s * 1000LL) + " ";
    for (int w = 36; w <= 300; w += 7) {
      for (const view_state& v : {state_for(2, span_s), state_for(3, span_s)}) {
        for (const auto& [sn, hn] : {std::pair<const snapshot*, const history*>{&base, &demo}, {&c, &ch}}) {
          const std::vector<line> f = render(*sn, w, 40, kNow, hn, v);
          const std::string t = as_text(f);
          const std::string at = " " + std::to_string(w) + " " + range;
          const std::string body = as_text({f.begin(), f.end() - 1});  // The footer's "←→ 1 min" is the range, not a minimum
          check(!has(body, "min ") && !has(t, "peak over mean") && !has(t, "collecting:"), "no minimum and no old caption" + at);
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
        const view_state o = state_for(3, span_s);
        cpu_fixture(name, span_s, s, h);
        size_t bytes = 0;
        constexpr int kFrames = 300;
        const auto from = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) bytes = vt_frame(render(s, w, rows, kNow, &h, o), rows).size();
        const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
        printf("%-10s %3d s %dx%d  %6zu bytes  %.3f ms a frame\n", name, span_s, w, rows, bytes, took.count() / kFrames);
      }
    }
  }
  for (const int span_s : {60, 600}) {  // Panel 2 on busy's GPU: five graphs, each with its label and the maximum's dot
    const snapshot s = busy();
    const history h = demo_minute(s);
    const view_state o = state_for(2, span_s);
    size_t bytes = 0;
    constexpr int kFrames = 300;
    const auto from = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) bytes = vt_frame(render(s, 131, 40, kNow, &h, o), 40).size();
    const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
    printf("panel 2    %3d s 131x40  %6zu bytes  %.3f ms a frame\n", span_s, bytes, took.count() / kFrames);
  }
  for (const auto& [w, rows] : {std::pair{131, 40}, std::pair{100, 34}}) {  // Panel 4 on busy's memory
    const snapshot s = busy();
    const history h = demo_minute(s);
    size_t bytes = 0;
    constexpr int kFrames = 300;
    const auto from = std::chrono::steady_clock::now();
    for (int i = 0; i < kFrames; ++i) bytes = vt_frame(render(s, w, rows, kNow, &h, state_for(4)), rows).size();
    const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
    printf("panel 4     60 s %dx%d  %6zu bytes  %.3f ms a frame\n", w, rows, bytes, took.count() / kFrames);
  }
  {  // One push with this machine's load: 360 Windows processes and the guest's 30, every slot written
    snapshot s = contended();
    s.processes.clear();
    for (uint32_t i = 1; i <= 360; ++i) {
      process_row p;
      p.pid = i * 4; p.name = "proc" + std::to_string(i) + ".exe"; p.cores = 0.01 * i; p.cores_known = true; p.create_time = i;
      s.processes.push_back(p);
    }
    std::string guest;
    for (int i = 0; i < 30; ++i) guest += (i ? "," : "") + std::string("{\"pid\":") + std::to_string(3000 + i) + ",\"name\":\"g\",\"state\":\"S\",\"rss\":1,\"cores\":0.1,\"rated\":true,\"start\":" + std::to_string(i) + "}";
    history h;
    constexpr int kPushes = 2000;
    const auto from = std::chrono::steady_clock::now();
    for (int i = 0; i < kPushes; ++i) {
      guest_figures g;
      g.seq = i;
      s.wsl_json = guest_line(std::vector<double>(20, 50.0), g, guest);
      s.unix_ms = kNow + i * 1000LL;
      h.push(s);
    }
    const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - from;
    printf("push, 390 processes   %zu slots  %.3f ms a push, with building the guest line\n", h.slots_allocated(), took.count() / kPushes);
  }
  {  // 2000 processes, 50 new a second, past the cap: push and selected_processes, as main.cpp calls them
    std::vector<process_row> rows;
    for (uint32_t i = 1; i <= 2000; ++i) {
      process_row p;
      p.pid = i * 4; p.name = "proc" + std::to_string(i) + ".exe"; p.create_time = i; p.cores = 1; p.cores_known = true; p.working_set = 1 << 20;
      rows.push_back(p);
    }
    snapshot s;
    s.interval_s = 1;
    s.processes = rows;
    s.unix_ms = kNow;
    history first;
    const auto a0 = std::chrono::steady_clock::now();
    first.push(s);
    const std::chrono::duration<double, std::milli> first_took = std::chrono::steady_clock::now() - a0;
    history h;
    view_state v = state_for(3);
    v.panels[3].rows[0] = {true, 40, "proc10.exe", 10};
    uint32_t next = 1000000;
    double total = 0, worst = 0;
    constexpr int kWarm = 300, kPushes = 600;
    for (int i = 0; i < kWarm + kPushes; ++i) {
      s.processes = rows;
      for (int k = 0; k < 50; ++k, ++next) {
        process_row p;
        p.pid = next; p.name = "cc"; p.create_time = next; p.cores = 1; p.cores_known = true;
        s.processes.push_back(p);
      }
      s.unix_ms = kNow + i * 1000LL;
      const auto a = std::chrono::steady_clock::now();
      h.push(s, selected_processes(s, v));
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
      if (i >= kWarm) {
        total += ms;
        worst = std::max(worst, ms);
      }
    }
    printf("push, 2000 processes + 50 new a second   %zu slots  %.3f ms mean, %.3f ms worst, with selected_processes; first push %.3f ms\n", h.slots_allocated(), total / kPushes, worst, first_took.count());
  }
  for (const int threads : {40, 64}) {
    const snapshot s = noisy_now(threads);
    const history h = noisy_history(s);
    printf("noisy, %d threads, 131x40  %zu bytes\n", threads, vt_frame(render(s, 131, 40, kNow, &h, state_for(3)), 40).size());
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
    view_state v = state_for(fixture ? 3 : 1);
    int w = 131, h = 40;
    for (key_event k; !console::stop_requested() && apply_key(v, k, s); k = c.poll()) {
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
  if (argc >= 6 && strcmp(argv[1], "vt") == 0) {  // render_check vt <fixture> W H FILE [span_s]: one frame's VT bytes
    const int span_s = argc >= 7 ? atoi(argv[6]) : 60;
    snapshot s;
    history h;
    if (!cpu_fixture(argv[2], span_s, s, h)) return 2;
    const int rows = atoi(argv[4]);
    const std::string vt = vt_frame(render(s, atoi(argv[3]), rows, kNow, &h, state_for(3, span_s)), rows);
    std::ofstream out(argv[5], std::ios::binary);
    out.write(vt.data(), static_cast<std::streamsize>(vt.size()));
    out.close();
    printf("%zu bytes\n", vt.size());
    return out ? 0 : 1;
  }
  if (argc >= 4) {  // render_check <contended|idle|stale|noisy> W H [span_s]: the large layout of panel 3
    const int span_s = argc >= 5 ? atoi(argv[4]) : 60;
    snapshot s;
    history h;
    if (cpu_fixture(argv[1], span_s, s, h)) {
      const std::string t = as_text(render(s, atoi(argv[2]), atoi(argv[3]), kNow, &h, state_for(3, span_s)));
      fwrite(t.data(), 1, t.size(), stdout);
      return 0;
    }
  }
  if (argc == 4 || argc == 5) {
    const snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    const history demo = demo_minute(s);
    int panel = 1;
    if (argc == 5) {
      static constexpr const char* kNames[] = {"", "overall", "gpu", "cpu", "mem", "disk", "net", "procs", "vm"};
      for (int i = 1; i <= kPanels; ++i) panel = strcmp(argv[4], kNames[i]) == 0 ? i : panel;
      if (argv[4][0] >= '1' && argv[4][0] <= '8' && argv[4][1] == 0) panel = argv[4][0] - '0';
    }
    const std::string t = as_text(render(s, atoi(argv[2]), atoi(argv[3]), kNow, argc == 5 ? &demo : nullptr, state_for(panel)));
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
  check_keys();
  check_view_keys();
  check_audit_fixes();
  check_process_ring();
  check_selection();
  check_unchecked_marks();
  check_memory_panel();
  check_final_audit();
  check_selection_identity();
  check_two_columns();
  check_history_heading();
  check_part_of_whole();
  check_gpu_panel();
  if (!quick) {
    check_denied_note();
    check_holder_wording();
    check_sizes_with_history();
    check_large_sweep();
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
