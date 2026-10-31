#include "ui/render.h"
#include "ui/wsl_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>

namespace wwt {
namespace {

// When a value turns amber or red. Judgement calls, not measurements: tune them here.
constexpr double kHotPct = 90;           // CPU, one core, one disk
constexpr double kMemWatchPct = 10;      // Available memory left, Windows and WSL; free GPU memory
constexpr double kMemActPct = 4;
constexpr double kGpuTempWatch = 83;     // The value sshvllmtop's spec uses
constexpr double kGpuTempAct = 90;
constexpr double kPressureWatch = 20;    // PSI some avg10
constexpr double kStaleS = 5;            // Data older than this is called out

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr int kLabel = 6;  // Width of "CPU", "MEM", ... at the start of a row

// ---- UTF-8 and columns ---------------------------------------------------------------------------

// Reads one code point at s[i]. Anything malformed comes back as U+FFFD, one byte consumed.
char32_t next_cp(std::string_view s, size_t& i) {
  const auto byte = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char b = byte(i);
  int more = 0;
  char32_t c = 0, lowest = 0;
  if (b < 0x80) { ++i; return b; }
  if (b >= 0xC2 && b <= 0xDF) { more = 1; c = b & 0x1Fu; lowest = 0x80; }
  else if (b >= 0xE0 && b <= 0xEF) { more = 2; c = b & 0x0Fu; lowest = 0x800; }
  else if (b >= 0xF0 && b <= 0xF4) { more = 3; c = b & 0x07u; lowest = 0x10000; }
  else { ++i; return 0xFFFD; }
  if (s.size() - i <= static_cast<size_t>(more)) { ++i; return 0xFFFD; }
  for (int k = 1; k <= more; ++k) {
    const unsigned char n = byte(i + static_cast<size_t>(k));
    if ((n & 0xC0u) != 0x80u) { ++i; return 0xFFFD; }
    c = (c << 6) | (n & 0x3Fu);
  }
  if (c < lowest || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) { ++i; return 0xFFFD; }
  i += static_cast<size_t>(more) + 1;
  return c;
}

void put_cp(std::string& out, char32_t c) {
  const auto u = static_cast<uint32_t>(c);
  if (u < 0x80) {
    out += static_cast<char>(u);
  } else if (u < 0x800) {
    out += static_cast<char>(0xC0 | (u >> 6));
    out += static_cast<char>(0x80 | (u & 0x3F));
  } else if (u < 0x10000) {
    out += static_cast<char>(0xE0 | (u >> 12));
    out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (u & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (u >> 18));
    out += static_cast<char>(0x80 | ((u >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (u & 0x3F));
  }
}

struct cp_range { char32_t lo, hi; };

constexpr cp_range kZero[] = {
    {0x0300, 0x036F}, {0x1160, 0x11FF}, {0x200B, 0x200D}, {0x20D0, 0x20F0},
    {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xE0100, 0xE01EF}};

constexpr cp_range kWide[] = {
    {0x1100, 0x115F},   {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xA960, 0xA97F},   {0xAC00, 0xD7A3},
    {0xF900, 0xFAFF},   {0xFE10, 0xFE19},   {0xFE30, 0xFE6F},   {0xFF00, 0xFF60},
    {0xFFE0, 0xFFE6},   {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF},
    {0x20000, 0x3FFFD}};

template <size_t N> bool in_table(const cp_range (&t)[N], char32_t c) {
  return std::any_of(t, t + N, [c](const cp_range& r) { return c >= r.lo && c <= r.hi; });
}

// A process name is arbitrary: an ESC or a bidi override in one must not act on the terminal.
bool unsafe(char32_t c) {
  return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x200E || c == 0x200F || (c >= 0x2028 && c <= 0x202E) ||
         (c >= 0x2066 && c <= 0x2069);
}

int cp_width(char32_t c) {
  if (unsafe(c)) return 1;  // Shown as '?'
  if (in_table(kZero, c)) return 0;
  return in_table(kWide, c) ? 2 : 1;
}

// The safe prefix of s that fits `cols`. used gets its width, whole whether all of s fitted.
std::string safe_prefix(std::string_view s, int cols, int& used, bool& whole) {
  std::string out;
  used = 0;
  whole = true;
  for (size_t i = 0; i < s.size();) {
    const char32_t c = next_cp(s, i);
    const int w = cp_width(c);
    if (used + w > cols) { whole = false; break; }
    used += w;
    put_cp(out, unsafe(c) ? U'?' : c);
  }
  return out;
}

// At most w columns, marked with an ellipsis when cut.
std::string fit(std::string_view s, int w) {
  if (w <= 0) return {};
  int used = 0;
  bool whole = true;
  std::string out = safe_prefix(s, w, used, whole);
  if (whole) return out;
  out = safe_prefix(s, w - 1, used, whole);
  return out + "…";
}

std::string pad(std::string_view s, int w) {
  std::string out = fit(s, w);
  out.append(static_cast<size_t>(std::max(0, w - display_width(out))), ' ');
  return out;
}

std::string pad_left(std::string_view s, int w) {
  const std::string out = fit(s, w);
  return std::string(static_cast<size_t>(std::max(0, w - display_width(out))), ' ') + out;
}

// ---- Numbers -------------------------------------------------------------------------------------

template <class... A> std::string fmt(const char* f, A... a) {
  char b[96];
  std::snprintf(b, sizeof b, f, a...);
  return b;
}

std::string repeat(std::string_view s, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

double gib(uint64_t b) { return static_cast<double>(b) / kGiB; }

std::string pct_text(double v) { return std::isfinite(v) ? fmt("%3.0f%%", v) : "  ?%"; }

std::string rate(double b) {
  if (!std::isfinite(b) || b < 0) return "?";
  if (b >= 1e9) return fmt("%.1f GB/s", b / 1e9);
  if (b >= 1e6) return fmt("%.1f MB/s", b / 1e6);
  if (b >= 1e3) return fmt("%.0f KB/s", b / 1e3);
  return fmt("%.0f B/s", b);
}

std::string link_speed(uint64_t bits) {
  const double b = static_cast<double>(bits);
  return b >= 1e9 ? fmt("%g Gb/s", b / 1e9) : fmt("%g Mb/s", b / 1e6);
}

std::string clock_text(int64_t unix_ms) {
  if (unix_ms <= 0) return "--:--:--";
  const auto t = static_cast<std::time_t>(unix_ms / 1000);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char b[16];
  std::strftime(b, sizeof b, "%H:%M:%S", &tm);
  return b;
}

// ---- Building rows -------------------------------------------------------------------------------

class row {
 public:
  row& add(std::string_view text, tone c = tone::text, bool bold = false) {
    cols_ += display_width(text);
    l_.push_back({std::string(text), c, bold});
    return *this;
  }
  row& to(int col) {
    if (col > cols_) add(std::string(static_cast<size_t>(col - cols_), ' '));
    return *this;
  }
  // ━ filled, ─ empty: the level reads without colour too.
  row& bar(int w, double pct, tone c = tone::bar) {
    const double f = std::isfinite(pct) ? std::clamp(pct, 0.0, 100.0) : 0.0;
    const int n = static_cast<int>(std::lround(f / 100.0 * w));
    add(repeat("━", n), c);
    return add(repeat("─", w - n), tone::track);
  }
  // Optional detail: added only if the whole of it fits, so a row never ends in half a number.
  row& add_if_fits(std::string_view text, int width, tone c = tone::dim) {
    if (cols_ + display_width(text) <= width) add(text, c);
    return *this;
  }
  int cols() const { return cols_; }
  operator line() const { return l_; }

 private:
  line l_;
  int cols_ = 0;
};

int bar_width(int width) { return std::clamp(width / 5, 10, 40); }

tone hot(double pct) { return pct >= kHotPct ? tone::watch : tone::text; }

tone left_tone(double left_pct) {
  return left_pct < kMemActPct ? tone::act : left_pct < kMemWatchPct ? tone::watch : tone::text;
}

line rule(const std::string& title, const std::string& right, int width) {
  row r;
  r.add(title, tone::title, true);
  const std::string tail = fit(right, width - display_width(title) - 3);
  const int gap = width - r.cols() - 1 - (tail.empty() ? 0 : display_width(tail) + 1);
  if (gap > 0) r.add(" ").add(repeat("─", gap), tone::faint);
  if (!tail.empty()) r.add(" ").add(tail, tone::dim);
  return r;
}

line message(std::string_view label, std::string_view text, tone c = tone::watch, int col = kLabel) {
  row r;
  r.add(label, tone::dim).to(col).add(text, c);
  return r;
}

void memory_row(std::vector<line>& out, int width, uint64_t total, uint64_t available, const std::vector<std::string>& details) {
  const double left = 100.0 * static_cast<double>(available) / static_cast<double>(total);
  const uint64_t used = total - std::min(total, available);
  const tone t = left_tone(left);
  row r;
  r.add("MEM", tone::title, true).to(kLabel).bar(bar_width(width), 100.0 - left, t == tone::text ? tone::bar : t);
  r.add(fmt("  %.1f/%.1f GiB used", gib(used), gib(total)), t, t != tone::text);
  for (const std::string& d : details) r.add_if_fits("   " + d, width);
  out.push_back(r);
}

// One braille cell per two cores, four levels each.
void core_rows(std::vector<line>& out, int width, const std::vector<double>& cores) {
  if (cores.empty()) {
    out.push_back(message("cores", "per-core data unavailable"));
    return;
  }
  static constexpr unsigned kLeft[] = {0x40, 0x04, 0x02, 0x01}, kRight[] = {0x80, 0x20, 0x10, 0x08};
  const size_t top = static_cast<size_t>(std::max_element(cores.begin(), cores.end()) - cores.begin());
  const std::string note = fmt("  hottest core %d at %.0f%%", static_cast<int>(top), cores[top]);
  const size_t per_row = static_cast<size_t>(std::max(1, width - kLabel - 1));
  const size_t cells = (cores.size() + 1) / 2;
  for (size_t first = 0; first < cells && first < per_row * 2; first += per_row) {
    row r;
    r.add(first ? "" : "cores", tone::dim).to(kLabel);
    for (size_t c = first; c < std::min(cells, first + per_row); ++c) {
      unsigned bits = 0;
      double peak = 0;
      for (size_t side = 0; side < 2 && 2 * c + side < cores.size(); ++side) {
        const double v = std::isfinite(cores[2 * c + side]) ? std::clamp(cores[2 * c + side], 0.0, 100.0) : 0.0;
        peak = std::max(peak, v);
        const int dots = std::max(v > 2 ? 1 : 0, static_cast<int>(std::lround(v / 25)));
        for (int k = 0; k < dots; ++k) bits |= (side == 0 ? kLeft : kRight)[k];
      }
      std::string cell;
      put_cp(cell, static_cast<char32_t>(0x2800 + bits));
      r.add(cell, peak >= kHotPct ? tone::watch : tone::bar);
    }
    if (first == 0 && r.cols() + display_width(note) <= width) r.add(note, tone::dim);
    out.push_back(r);
  }
}

// ---- Sections ------------------------------------------------------------------------------------

std::vector<line> gpu_section(const snapshot& s, const wsl_view& w, int width) {
  const gpu_device_view& g = s.gpu_device;
  const gpu_split_view& split = s.gpu_split;
  std::vector<line> out;
  std::string right = "split is VM level, not exact shares";
  if (g.present && !g.name.empty() && width >= 100) right = g.name + " · " + right;
  out.push_back(rule("GPU", right, width));

  const bool roomy = width >= 100;
  const int bw = std::clamp((width - (roomy ? 80 : 56)) / 2, 6, 40);
  constexpr int kCol = 13;  // Bars of the device and split rows start together

  if (!g.present) {
    out.push_back(message("device", "unavailable: no answer from the GPU driver", tone::watch, kCol - 5));
  } else {
    row r;
    r.add("device", tone::dim).to(kCol - 5).add("util ", tone::dim);
    if (g.has_util) {
      r.bar(bw, g.util_pct).add(" ").add(pct_text(g.util_pct), tone::text, true);
    } else {
      r.add(pad("n/a", bw + 5), tone::dim);
    }
    r.add("   mem ", tone::dim);
    if (g.mem_total > 0) {
      const double free_pct = 100.0 * (1.0 - gib(g.mem_used) / gib(g.mem_total));
      const tone t = left_tone(free_pct);
      r.bar(bw, 100.0 - free_pct, t == tone::text ? tone::bar : t);
      r.add(fmt(" %.1f/%.1f GiB", gib(g.mem_used), gib(g.mem_total)), t, t != tone::text);
      if (roomy) r.add(fmt("  free %.1f GiB", gib(g.mem_total - std::min(g.mem_total, g.mem_used))), tone::dim);
    } else {
      r.add("n/a", tone::dim);
    }
    r.add("  ");
    if (!g.has_power) r.add("power n/a", tone::dim);
    else if (g.power_limit_w > 0) r.add(fmt("%.0f/%.0f W", g.power_w, g.power_limit_w));
    else r.add(fmt("%.0f W", g.power_w));
    r.add("  ");
    if (!g.has_temp) {
      r.add("temp n/a", tone::dim);
    } else {
      const tone t = g.temp_c >= kGpuTempAct ? tone::act : g.temp_c >= kGpuTempWatch ? tone::watch : tone::text;
      r.add(fmt("%.0f°C", g.temp_c), t, t != tone::text);
    }
    if (roomy && g.has_clock) r.add(fmt("  %.0f MHz", g.sm_mhz), tone::dim);
    out.push_back(r);
  }

  if (!split.valid) {
    const char* why = s.vm.vmwp_pid == 0 ? "unavailable: WSL's VM process (vmwp) is not known" : "unavailable: no GPU engine data";
    out.push_back(message("split", why, tone::watch, kCol - 5));
  } else {
    const auto side = [&](const char* name, double pct, uint64_t local, const char* note) {
      row r;
      r.add(name, tone::title).to(kCol).bar(bw, pct).add(" ").add(pct_text(pct), tone::text, true);
      r.add(fmt("   local %.1f GiB", gib(local))).add(note, tone::dim);
      return line(r);
    };
    out.push_back(side("WSL VM", split.vm_pct, split.vm_local, "   per-process use not measured"));
    out.push_back(side("Windows", split.windows_pct, split.windows_local, ""));
  }

  if (!w.ok) {
    out.push_back(message("on GPU", "unavailable: no WSL data", tone::watch, kCol - 5));
  } else {
    // Names only. /dev/dxg says who holds the GPU; nothing here says how much each one uses.
    static constexpr std::string_view kTail = "  /dev/dxg holders, no usage measured";
    row r;
    r.add("on GPU", tone::dim).to(kCol - 5);
    const int room = width - r.cols() - static_cast<int>(kTail.size()) - 8;
    std::vector<std::string> names;
    for (const uint32_t pid : w.dxg_pids) {
      const auto it = std::find_if(w.processes.begin(), w.processes.end(), [&](const wsl_process& p) { return p.pid == pid; });
      names.push_back(it == w.processes.end() ? fmt("pid %u", pid) : fit(it->name, 20) + fmt(" %u", pid));
    }
    size_t shown = 0;
    int used = 0;
    for (; shown < names.size(); ++shown) {
      const int n = display_width(names[shown]) + 2;
      if (used + n > room) break;
      r.add(names[shown]).add("  ");
      used += n;
    }
    if (names.empty()) r.add("no holder seen  ", tone::dim);
    else if (shown < names.size()) r.add(fmt("+%d more", static_cast<int>(names.size() - shown)));
    r.add(kTail, tone::dim);
    out.push_back(r);
    if (w.dxg_denied > 0) {
      out.push_back(message("", fmt("dxg_denied %lld: unreadable, a root GPU process could be missing",
                                    static_cast<long long>(w.dxg_denied)), tone::watch, kCol - 5));
    }
  }
  return out;
}

void disk_rows(std::vector<line>& out, const snapshot& s, int width) {
  if (s.disks.empty()) {
    out.push_back(message("DISK", "unavailable"));
    return;
  }
  const int bw = bar_width(width) / 2 + 2, nw = std::clamp(width / 6, 12, 26);
  std::vector<const disk_row*> rows;
  for (const disk_row& d : s.disks) rows.push_back(&d);
  std::stable_sort(rows.begin(), rows.end(), [](const disk_row* a, const disk_row* b) { return a->busy_pct > b->busy_pct; });
  const size_t n = std::min<size_t>(rows.size(), 4);
  for (size_t i = 0; i < n; ++i) {
    const disk_row& d = *rows[i];
    const tone t = hot(d.busy_pct);
    row r;
    r.add(i ? "" : "DISK", tone::title, true).to(kLabel).add(pad(d.name, nw)).add(" ");
    r.bar(bw, d.busy_pct, t == tone::text ? tone::bar : t).add(" ").add(pct_text(d.busy_pct), t).add(" busy", tone::dim);
    r.add("   read ", tone::dim).add(pad(rate(d.read_bytes_s), 10));
    r.add(" write ", tone::dim).add(rate(d.write_bytes_s));
    if (i + 1 == n && rows.size() > n) r.add(fmt("   +%d more", static_cast<int>(rows.size() - n)), tone::dim);
    out.push_back(r);
  }
}

void net_rows(std::vector<line>& out, const snapshot& s, int width) {
  if (s.nets.empty()) {
    out.push_back(message("NET", "unavailable"));
    return;
  }
  const int bw = bar_width(width) / 2 + 2, nw = std::clamp(width / 6, 12, 26);
  std::vector<const net_row*> rows;
  for (const net_row& d : s.nets) rows.push_back(&d);
  std::stable_sort(rows.begin(), rows.end(), [](const net_row* a, const net_row* b) {
    if (a->hardware != b->hardware) return a->hardware;
    return a->in_bytes_s + a->out_bytes_s > b->in_bytes_s + b->out_bytes_s;
  });
  const size_t n = std::min<size_t>(rows.size(), 4);
  for (size_t i = 0; i < n; ++i) {
    const net_row& d = *rows[i];
    row r;
    r.add(i ? "" : "NET", tone::title, true).to(kLabel).add(pad(d.name, nw)).add(" ");
    if (d.link_bits_s > 0) {
      const double peak = std::max(d.in_bytes_s, d.out_bytes_s) * 8.0;
      const double pct = 100.0 * peak / static_cast<double>(d.link_bits_s);
      r.bar(bw, pct).add(" ").add(pct_text(pct)).add(" link", tone::dim);
    } else {
      r.add(pad("no link speed", bw + 10), tone::dim);
    }
    r.add("   in ", tone::dim).add(pad(rate(d.in_bytes_s), 10));
    r.add("   out ", tone::dim).add(pad(rate(d.out_bytes_s), 10));
    if (d.link_bits_s > 0) r.add_if_fits(link_speed(d.link_bits_s), width);
    if (!d.hardware) r.add_if_fits("  virtual", width);
    if (i + 1 == n && rows.size() > n) r.add(fmt("   +%d more", static_cast<int>(rows.size() - n)), tone::dim);
    out.push_back(r);
  }
}

std::vector<line> windows_section(const snapshot& s, int width) {
  std::vector<line> out;
  std::string right = "whole machine, includes the WSL VM";
  if (!s.cpu.core_pct.empty() && s.memory.total > 0) {
    right = fmt("%d threads · %.1f GiB · ", static_cast<int>(s.cpu.core_pct.size()), gib(s.memory.total)) + right;
  }
  out.push_back(rule("WINDOWS", right, width));

  const tone t = hot(s.cpu.busy_pct);
  row cpu;
  cpu.add("CPU", tone::title, true).to(kLabel).bar(bar_width(width), s.cpu.busy_pct, t == tone::text ? tone::bar : t);
  cpu.add(" ").add(pct_text(s.cpu.busy_pct), t, true);
  out.push_back(cpu);
  core_rows(out, width, s.cpu.core_pct);

  if (s.memory.total == 0) {
    out.push_back(message("MEM", "unavailable"));
  } else {
    std::vector<std::string> details = {fmt("available %.1f GiB", gib(s.memory.available)), fmt("cache %.1f GiB", gib(s.memory.cache))};
    if (s.memory.commit_limit > 0) details.push_back(fmt("commit %.1f/%.1f GiB", gib(s.memory.commit_used), gib(s.memory.commit_limit)));
    memory_row(out, width, s.memory.total, s.memory.available, details);
  }
  disk_rows(out, s, width);
  net_rows(out, s, width);
  return out;
}

std::string collector_status(const snapshot& s) {
  std::string t = s.wsl_age_s >= 0 ? fmt("collector line %.1f s old", s.wsl_age_s) : std::string("collector age unknown");
  if (s.wsl_restarts > 0) t += fmt(", restarted %d times", s.wsl_restarts);
  return t;
}

void vm_row(std::vector<line>& out, const snapshot& s) {
  row r;
  r.add("VM", tone::dim).to(kLabel);
  if (s.vm.vmmem_pid == 0) {
    r.add("vmmemWSL not found on Windows", tone::watch);
  } else {
    r.add(fmt("vmmemWSL %.1f GiB, %.2f cores", gib(s.vm.vmmem_working_set), s.vm.vmmem_cores));
    r.add("   Windows' view, included in WINDOWS", tone::dim);
  }
  out.push_back(r);
}

std::vector<line> wsl_section(const snapshot& s, const wsl_view& w, int width) {
  std::vector<line> out;
  if (!w.ok) {
    out.push_back(rule("WSL", "", width));
    const char* why = s.wsl_json.empty()      ? "WSL data unavailable: the collector has sent nothing"
                      : w.schema > 1          ? "WSL data unavailable: newer collector schema than this screen reads"
                                              : "WSL data unavailable: the collector's line could not be read";
    out.push_back(message("", why, tone::watch, 0));
    if (s.wsl_restarts > 0) out.push_back(message("", collector_status(s), tone::watch, 0));
    vm_row(out, s);
    return out;
  }
  std::string right = "guest view, inside the VM";
  if (!w.core_pct.empty()) right = fmt("%d threads · %.1f GiB · ", static_cast<int>(w.core_pct.size()), gib(w.mem_total)) + right;
  if (!w.distro.empty()) right = w.distro + " · " + right;
  out.push_back(rule("WSL", right, width));

  const tone t = hot(w.busy_pct);
  row cpu;
  cpu.add("CPU", tone::title, true).to(kLabel).bar(bar_width(width), w.busy_pct, t == tone::text ? tone::bar : t);
  cpu.add(" ").add(pct_text(w.busy_pct), t, true);
  if (w.steal_pct > 0) cpu.add(fmt("   steal %.1f%%", w.steal_pct), tone::dim);
  out.push_back(cpu);
  core_rows(out, width, w.core_pct);

  if (w.mem_total == 0) {
    out.push_back(message("MEM", "unavailable"));
  } else {
    memory_row(out, width, w.mem_total, w.mem_available,
               {fmt("available %.1f GiB", gib(w.mem_available)), fmt("cache %.1f GiB", gib(w.mem_cached)), fmt("swap %.1f GiB", gib(w.swap_used))});
  }

  row p;
  p.add("pressure ", tone::dim);
  const auto psi = [&](const char* name, const std::optional<double>& v) {
    if (!v) { p.add(fmt("%s n/a  ", name), tone::dim); return; }
    const bool high = *v >= kPressureWatch;
    p.add(fmt("%s %.1f%%", name, *v), high ? tone::watch : tone::text, high).add("  ");
  };
  psi("cpu", w.psi_cpu);
  psi("mem", w.psi_memory);
  psi("io", w.psi_io);
  const bool stale = s.wsl_age_s < 0 || s.wsl_age_s >= kStaleS;
  p.add(" ").add(collector_status(s), stale || s.wsl_restarts > 0 ? tone::watch : tone::dim, stale);
  out.push_back(p);
  vm_row(out, s);
  return out;
}

// ---- Processes -----------------------------------------------------------------------------------

// Each list is built on its own, in its own units, and they only ever sit next to each other.
std::vector<line> windows_list(const snapshot& s, int col, int rows) {
  std::vector<line> out;
  std::vector<const process_row*> list;
  for (const process_row& p : s.processes) {
    if (p.pid != 0) list.push_back(&p);  // PID 0 is the idle process: its cores are idle time
  }
  row head;
  head.add("Windows", tone::title, true);
  if (list.empty()) {
    out.push_back(head);
    out.push_back(message("", "unavailable", tone::watch, 0));
    return out;
  }
  std::map<uint32_t, double> gpu_pct;  // The busiest engine: engines are not additive
  for (const gpu_engine_row& e : s.gpu_engines) gpu_pct[e.pid] = std::max(gpu_pct[e.pid], e.pct);
  std::map<uint32_t, uint64_t> vram;
  for (const gpu_memory_row& m : s.gpu_memory) vram[m.pid] = m.local;
  const auto gpu_of = [&](uint32_t pid) { const auto it = gpu_pct.find(pid); return it == gpu_pct.end() ? -1.0 : it->second; };

  std::stable_sort(list.begin(), list.end(), [&](const process_row* a, const process_row* b) {
    if (a->cores != b->cores) return a->cores > b->cores;
    const double ga = gpu_of(a->pid), gb = gpu_of(b->pid);
    if (ga != gb) return ga > gb;
    return a->working_set > b->working_set;
  });
  const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(0, rows)));
  head.add(fmt("  top %d of %d by cores", static_cast<int>(shown), static_cast<int>(list.size())), tone::dim);
  out.push_back(head);

  const bool pids = col >= 56;
  const int name_w = std::clamp(col - (pids ? 39 : 31), 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("WS GiB", 7), tone::dim);
  cols.add(pad_left("GPU%", 5), tone::dim).add(pad_left("LOCAL", 5), tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const process_row& p = *list[i];
    const bool is_vm = p.pid == s.vm.vmmem_pid || p.pid == s.vm.vmwp_pid;
    const double g = gpu_of(p.pid);
    const auto v = vram.find(p.pid);
    row r;
    r.add(pad_left(fmt("%.2f", p.cores), 6)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), is_vm ? tone::title : tone::text).add(pad_left(fmt("%.1f", gib(p.working_set)), 7));
    r.add(pad_left(g < 0 ? "-" : fmt("%.0f", g), 5)).add(pad_left(v == vram.end() ? "-" : fmt("%.1f", gib(v->second)), 5));
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(r);
  }
  return out;
}

std::vector<line> wsl_list(const wsl_view& w, int col, int rows) {
  std::vector<line> out;
  row head;
  head.add("WSL", tone::title, true);
  if (!w.ok) {
    out.push_back(head);
    out.push_back(message("", "unavailable", tone::watch, 0));
    return out;
  }
  std::vector<const wsl_process*> list;
  for (const wsl_process& p : w.processes) list.push_back(&p);
  std::stable_sort(list.begin(), list.end(), [](const wsl_process* a, const wsl_process* b) {
    if (a->on_gpu != b->on_gpu) return a->on_gpu;  // A short list must not lose who holds the GPU
    return a->cores != b->cores ? a->cores > b->cores : a->rss > b->rss;
  });
  const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(0, rows)));
  if (!w.distro.empty()) head.add(" ").add(w.distro, tone::title, true);
  head.add(fmt("  top %d of %lld, on GPU first, then cores", static_cast<int>(shown), static_cast<long long>(w.process_count)), tone::dim);
  out.push_back(head);

  const bool pids = col >= 48;
  const int name_w = std::clamp(col - (pids ? 34 : 26), 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("RSS GiB", 8), tone::dim).add("  st", tone::dim);
  cols.add("  GPU", tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const wsl_process& p = *list[i];
    row r;
    r.add(pad_left(fmt("%.2f", p.cores), 6)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text).add(pad_left(fmt("%.1f", gib(p.rss)), 8));
    r.add("  ").add(pad(p.state, 1), p.state == "D" ? tone::watch : tone::dim);
    if (p.on_gpu) r.add("  on GPU");
    out.push_back(r);
  }
  return out;
}

line clip(const line& in, int width);  // Below

void process_section(std::vector<line>& out, const snapshot& s, const wsl_view& w, int width, int avail) {
  constexpr int kGap = 3;  // " │ " between the lists
  if (avail < 2) return;
  out.push_back(rule("PROCESSES", "two systems, two lists: never compare or add across them", width));
  if (width >= 100) {
    const int col = (width - kGap) / 2;
    const std::vector<line> left = windows_list(s, col, avail - 3), right = wsl_list(w, width - col - kGap, avail - 3);
    for (size_t i = 0; i < std::max(left.size(), right.size()); ++i) {
      line joined;
      int used = 0;
      if (i < left.size()) {
        for (const span& p : clip(left[i], col)) { joined.push_back(p); used += display_width(p.text); }
      }
      joined.push_back({std::string(static_cast<size_t>(col - used), ' ')});
      joined.push_back({" │ ", tone::faint});
      if (i < right.size()) joined.insert(joined.end(), right[i].begin(), right[i].end());
      out.push_back(std::move(joined));
    }
    return;
  }
  const int room = avail - 1;
  const int wsl_rows = w.ok ? static_cast<int>(w.processes.size()) : 0;
  const int total = std::max(0, room - (w.ok ? 5 : 4));
  const int second = w.ok ? std::min(wsl_rows, total / 2 + (total % 2 == 0 ? 0 : 1)) : 0;
  const std::vector<line> top = windows_list(s, width, total - second), bottom = wsl_list(w, width, second);
  out.insert(out.end(), top.begin(), top.end());
  out.insert(out.end(), bottom.begin(), bottom.end());
}

// Cuts a finished row at `width` and makes every span safe to print.
line clip(const line& in, int width) {
  line out;
  int used = 0;
  for (const span& p : in) {
    if (used >= width) break;
    if (p.text.empty()) continue;
    int w = 0;
    bool whole = true;
    std::string text = safe_prefix(p.text, width - used, w, whole);
    if (!whole && used + w == width) {  // Make room for the ellipsis
      text = safe_prefix(p.text, width - used - 1, w, whole) + "…";
      ++w;
    } else if (!whole) {  // A wide character straddled the edge
      text += "…";
      ++w;
    }
    used += w;
    if (!text.empty()) out.push_back({std::move(text), p.color, p.bold});
    if (!whole) break;
  }
  return out;
}

}  // namespace

int display_width(std::string_view s) {
  int w = 0;
  for (size_t i = 0; i < s.size();) w += cp_width(next_cp(s, i));
  return w;
}

std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms) {
  if (width <= 0 || height <= 0) return {};
  const wsl_view w = read_wsl(s.wsl_json);

  row title;
  title.add("winwsltop", tone::title, true).add("  " + clock_text(s.unix_ms), tone::dim);
  if (now_ms > 0 && s.unix_ms > 0) {
    const double age = static_cast<double>(now_ms - s.unix_ms) / 1000.0;
    title.add(fmt("  data %.1f s old", std::max(0.0, age)), age >= kStaleS ? tone::watch : tone::dim, age >= kStaleS);
  } else {
    title.add("  data age unknown", tone::dim);
  }
  if (s.interval_s > 0) title.add(fmt("  every %.1f s", s.interval_s), tone::dim);

  const std::vector<std::vector<line>> parts = {gpu_section(s, w, width), windows_section(s, width), wsl_section(s, w, width)};
  int fixed = 1;
  for (const auto& p : parts) fixed += static_cast<int>(p.size());
  constexpr int kBlanks = 4, kMinList = 8;  // Blank rows go first when the window is short
  const bool spaced = height >= fixed + kBlanks + kMinList;

  std::vector<line> frame;
  frame.push_back(title);
  if (spaced) frame.emplace_back();
  for (const auto& p : parts) {
    frame.insert(frame.end(), p.begin(), p.end());
    if (spaced) frame.emplace_back();
  }
  process_section(frame, s, w, width, height - static_cast<int>(frame.size()));

  std::vector<line> out;
  const size_t limit = static_cast<size_t>(height);
  const bool cut = frame.size() > limit;
  for (size_t i = 0; i < std::min(frame.size(), limit) - (cut ? 1 : 0); ++i) out.push_back(clip(frame[i], width));
  if (cut) {
    line more = {{fmt("… %d more rows, window too short", static_cast<int>(frame.size() - limit + 1)), tone::watch, false}};
    out.push_back(clip(more, width));
  }
  return out;
}

std::string as_text(const std::vector<line>& lines) {
  std::string out;
  for (const line& l : lines) {
    for (const span& p : l) out += p.text;
    out += '\n';
  }
  return out;
}

std::string vt_frame(const std::vector<line>& lines, int rows) {
  static constexpr const char* kColor[] = {
      "200;200;200", "236;236;236", "122;122;122", "60;60;60",  // text, title, dim, faint
      "166;196;218", "58;58;58",    "212;184;96",  "224;96;96"};  // bar, track, watch, act
  std::string out = "\x1b[0m";
  int y = 0;
  for (const line& l : lines) {
    if (++y > rows) break;
    out += fmt("\x1b[%d;1H\x1b[2K", y);
    for (const span& p : l) {
      out += p.bold ? "\x1b[0;1;38;2;" : "\x1b[0;38;2;";
      out += kColor[static_cast<size_t>(p.color)];
      out += 'm';
      out += p.text;
    }
    out += "\x1b[0m";
  }
  if (y < rows) out += fmt("\x1b[%d;1H\x1b[J", y + 1);
  return out;
}

}  // namespace wwt
