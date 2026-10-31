// Checks the renderer on hand-built snapshots. render_check <busy|bare> W H prints a frame as text;
// render_check <busy|bare> draws it on the console until Ctrl+C.
// Build: scripts/build.sh src/ui/render_check.cpp   (it includes the sources it tests)
#include "ui/render.cpp"
#include "ui/wsl_view.cpp"
#ifdef _WIN32
#include "ui/console.cpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace wwt;

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
    check(has(b, "on GPU  unavailable"), "no holders says so" + n);
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

  const std::string vt = vt_frame(render(busy(), 131, 40, kNow), 40);
  bool clean = true;
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
  check(clean, "only our own sequences reach the terminal");
  check(!has(vt, "\x1b[2J") && !has(vt, "\x1b]0"), "names cannot clear the screen or set the title");
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

  for (const char* bad : {"", "{", "[]", "null", "{\"schema\":1}", "{\"schema\":1,\"busy_pct\":1,\"memory\":{}} x", "{\"schema\":1,\"busy_pct\":nan,\"memory\":{}}",
                          "{\"schema\":1,\"busy_pct\":-inf,\"memory\":{}}", "{\"schema\":1,\"busy_pct\":1,\"memory\":{},\"distro\":\"a\\q\"}",
                          "{\"schema\":1,\"busy_pct\":1,\"memory\":{},\"distro\":\"a\\u12\"}", "{\"schema\":\"1\",\"busy_pct\":1,\"memory\":{}}"}) {
    check(!read_wsl(bad).ok, std::string("rejects ") + bad);
  }
  check(!read_wsl(std::string(100000, '[')).ok, "deep nesting is refused, not recursed into");
  check(read_wsl("{\"schema\":1,\"busy_pct\":1,\"memory\":{\"total\":-5},\"process_count\":1e300}").mem_total == 0, "out of range numbers do not wrap");
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  { console c; int w = 0, h = 0; check(!c.is_terminal() || c.size(w, h), "console opens"); }
#endif
#ifdef _WIN32
  if (argc == 2) {  // A frame on the real console, until Ctrl+C
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
  if (argc == 4) {
    const snapshot s = strcmp(argv[1], "bare") == 0 ? bare() : busy();
    const std::string t = as_text(render(s, atoi(argv[2]), atoi(argv[3]), kNow));
    fwrite(t.data(), 1, t.size(), stdout);
    return 0;
  }
  check_width();
  check_reader();
  check_size(busy(), "busy");
  check_size(bare(), "bare");
  check_honesty();
  check_edges();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
