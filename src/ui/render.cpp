#include "ui/render.h"
#include "ui/history.h"
#include "core/wsl_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <ctime>
#include <iterator>
#include <limits>
#include <map>
#include <utility>

namespace wwt {
namespace {

// When a value turns amber or red. Judgement calls, not measurements: tune them here.
constexpr double kHotPct = 90;           // CPU, one core, one disk, GPU util, both split figures
constexpr double kMemWatchPct = 10;      // Available memory left, Windows and WSL; free GPU memory
constexpr double kMemActPct = 4;
constexpr double kGpuTempWatch = 83;     // The value sshvllmtop's spec uses
constexpr double kGpuTempAct = 90;
constexpr double kPressureWatch = 20;    // PSI some avg10
constexpr double kQuietPct = 5;          // Heat ramp, below which: above the 1.5625-point tick, so idle does not twinkle
constexpr double kLowPct = 30;           // kHotPct is its top step
constexpr double kMidPct = 60;
constexpr double kLostFloor = 1;         // vCPUs. WSL alone read up to 0.767 here: a judgement, not a clean noise floor

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
  // One painted cell. Equal neighbours share a span, so a frame says their colour once.
  row& cell(std::string_view glyph, int fg256, int bg256 = -1) {
    ++cols_;
    if (!l_.empty() && l_.back().fg256 == fg256 && l_.back().bg256 == bg256 && l_.back().color == tone::text && !l_.back().bold) l_.back().text += glyph;
    else l_.push_back({std::string(glyph), tone::text, false, fg256, bg256});
    return *this;
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

// The warning under a /dev/dxg holder list, in the columns its row has. Those processes could not be
// read, so a holder running as root could be unseen: whether one exists is not known, and this never
// says one does. The shorter wording keeps it whole down to the 31 columns of the narrowest side list.
std::string denied_note(int64_t denied, int room) {
  const long long n = denied;
  const std::string full = fmt("dxg_denied %lld: may miss a root holder", n);
  return display_width(full) <= room ? full : fmt("dxg_denied %lld: may miss root", n);
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

constexpr unsigned kDotLeft[] = {0x40, 0x04, 0x02, 0x01}, kDotRight[] = {0x80, 0x20, 0x10, 0x08};  // Braille, from the bottom

// One braille cell per two cores, four levels each.
void core_rows(std::vector<line>& out, int width, const std::vector<double>& cores) {
  if (cores.empty()) {
    out.push_back(message("cores", "per-core data unavailable"));
    return;
  }
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
        for (int k = 0; k < dots; ++k) bits |= (side == 0 ? kDotLeft : kDotRight)[k];
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

constexpr int kDeviceCol = 13;  // Bars of the device and split rows start together

int device_bar(int width) { return std::clamp((width - (width >= 100 ? 80 : 56)) / 2, 6, 40); }

line device_row(const gpu_device_view& g, int width) {
  const bool roomy = width >= 100;
  const int bw = device_bar(width);
  constexpr int kCol = kDeviceCol;
  if (!g.present) return message("device", "unavailable: no answer from the GPU driver", tone::watch, kCol - 5);
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
  return r;
}

std::vector<line> gpu_section(const snapshot& s, const wsl_view& w, int width) {
  const gpu_device_view& g = s.gpu_device;
  const gpu_split_view& split = s.gpu_split;
  std::vector<line> out;
  std::string right = "split is VM level, not exact shares";
  if (g.present && !g.name.empty() && width >= 100) right = g.name + " · " + right;
  out.push_back(rule("GPU", right, width));

  const int bw = device_bar(width);
  constexpr int kCol = kDeviceCol;
  out.push_back(device_row(g, width));

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

  // Who has /dev/dxg open, which is not who is using the GPU, so every state of this row says so. The
  // short wording keeps "no usage measured" whole on a row too narrow for the long one.
  static constexpr std::string_view kTail = "  /dev/dxg holders, no usage measured", kShortTail = "  no usage measured";
  static constexpr std::string_view kNoHolder = "no holder seen  ", kNoData = "unavailable: no WSL data";
  row r;
  r.add("dxg", tone::dim).to(kCol - 5);
  const auto tail_after = [&](int least) { return r.cols() + least + static_cast<int>(kTail.size()) <= width ? kTail : kShortTail; };
  if (!w.ok) {
    r.add(kNoData, tone::watch);
    r.add_if_fits(tail_after(static_cast<int>(kNoData.size())), width);
    out.push_back(r);
  } else {
    // Names only. /dev/dxg says who holds the GPU; nothing here says how much each one uses.
    std::vector<std::string> names;
    for (const uint32_t pid : w.dxg_pids) {
      const auto it = std::find_if(w.processes.begin(), w.processes.end(), [&](const wsl_process& p) { return p.pid == pid; });
      names.push_back(it == w.processes.end() ? fmt("pid %u", pid) : fit(it->name, 20) + fmt(" %u", pid));
    }
    const std::string_view tail = tail_after(names.empty() ? static_cast<int>(kNoHolder.size()) : display_width(fmt("+%d more", static_cast<int>(names.size()))));
    const int room = width - r.cols() - static_cast<int>(tail.size()) - 8;
    size_t shown = 0;
    int used = 0;
    for (; shown < names.size(); ++shown) {
      const int n = display_width(names[shown]) + 2;
      if (used + n > room) break;
      r.add(names[shown]).add("  ");
      used += n;
    }
    if (names.empty()) r.add(kNoHolder, tone::dim);
    else if (shown < names.size()) r.add(fmt("+%d more", static_cast<int>(names.size() - shown)));
    r.add(tail, tone::dim);
    out.push_back(r);
    if (w.dxg_denied > 0) {
      out.push_back(message("", denied_note(w.dxg_denied, width - (kCol - 5)), tone::watch, kCol - 5));
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

line windows_rule(const snapshot& s, int width) {
  std::string right = "whole machine, includes the WSL VM";
  if (!s.cpu.core_pct.empty() && s.memory.total > 0) {
    right = fmt("%d threads · %.1f GiB · ", static_cast<int>(s.cpu.core_pct.size()), gib(s.memory.total)) + right;
  }
  return rule("WINDOWS", right, width);
}

std::vector<line> windows_section(const snapshot& s, int width) {
  std::vector<line> out;
  out.push_back(windows_rule(s, width));

  const tone t = hot(s.cpu.busy_pct);
  row cpu;
  cpu.add("CPU", tone::title, true).to(kLabel);
  if (s.cpu.core_pct.empty()) {  // A failed read leaves a default view: its 0 was not measured
    cpu.add("n/a", tone::dim);
  } else {
    cpu.bar(bar_width(width), s.cpu.busy_pct, t == tone::text ? tone::bar : t);
    cpu.add(" ").add(pct_text(s.cpu.busy_pct), t, true);
  }
  out.push_back(cpu);
  core_rows(out, width, s.cpu.core_pct);

  if (s.memory.total == 0) {
    out.push_back(message("MEM", "unavailable"));
  } else {
    // cache? : no ledger row ties it to a known \Memory figure yet (design/panel-grammar.md part 1, experiment 06).
    std::vector<std::string> details = {fmt("available %.1f GiB", gib(s.memory.available)), fmt("cache? %.1f GiB", gib(s.memory.cache))};
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
    r.add(fmt("vmmemWSL %.1f GiB, ", gib(s.vm.vmmem_working_set)) + (s.vm.vmmem_cores_known ? fmt("%.2f cores", s.vm.vmmem_cores) : "n/a cores"));  // Without the flag, an unmeasured 0
    r.add("   Windows' view, included in WINDOWS", tone::dim);
  }
  out.push_back(r);
}

const char* wsl_unavailable(const snapshot& s, const wsl_view& w) {
  return s.wsl_json.empty() ? "WSL data unavailable: the collector has sent nothing"
         : w.schema > 1     ? "WSL data unavailable: newer collector schema than this screen reads"
                            : "WSL data unavailable: the collector's line could not be read";
}

line wsl_rule(const wsl_view& w, int width) {
  std::string right = "guest view, inside the VM";
  if (!w.core_pct.empty()) right = fmt("%d threads · %.1f GiB · ", static_cast<int>(w.core_pct.size()), gib(w.mem_total)) + right;
  if (!w.distro.empty()) right = w.distro + " · " + right;
  return rule("WSL", right, width);
}

std::vector<line> wsl_section(const snapshot& s, const wsl_view& w, int width) {
  std::vector<line> out;
  if (!w.ok) {
    out.push_back(rule("WSL", "", width));
    out.push_back(message("", wsl_unavailable(s, w), tone::watch, 0));
    if (s.wsl_restarts > 0) out.push_back(message("", collector_status(s), tone::watch, 0));
    vm_row(out, s);
    return out;
  }
  out.push_back(wsl_rule(w, width));

  const tone t = hot(w.busy_pct);
  row cpu;
  cpu.add("CPU", tone::title, true).to(kLabel).bar(bar_width(width), w.busy_pct, t == tone::text ? tone::bar : t);
  cpu.add(" ").add(pct_text(w.busy_pct), t, true);
  if (w.steal_pct > 0) cpu.add(fmt("   steal? %.1f%%", w.steal_pct), tone::dim);
  out.push_back(cpu);
  core_rows(out, width, w.core_pct);

  if (w.mem_total == 0) {
    out.push_back(message("MEM", "unavailable"));
  } else {
    memory_row(out, width, w.mem_total, w.mem_available,
               {fmt("available %.1f GiB", gib(w.mem_available)), fmt("cache %.1f GiB", gib(w.mem_cached)), fmt("swap? %.1f GiB", gib(w.swap_used))});  // swap? : not checked
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

constexpr int kListGap = 3;  // " │ " between two lists
constexpr const char* kTwoLists = "two systems, two lists: never compare or add across them";

std::map<uint32_t, double> busiest_engine(const snapshot& s) {  // The busiest engine: engines are not additive
  std::map<uint32_t, double> gpu_pct;
  for (const gpu_engine_row& e : s.gpu_engines) gpu_pct[e.pid] = std::max(gpu_pct[e.pid], e.pct);
  return gpu_pct;
}

// A process's cores as printed: n/a on its first reading, when there was no earlier time to subtract and the
// 0 read was not measured (cores_known, the collector's rated).
std::string cores_text(bool known, double cores) {
  if (!known || !std::isfinite(cores)) return "n/a";
  return std::fabs(cores) < 1e6 ? fmt("%.2f", cores) : fmt("%.3g", cores);
}

bool windows_cores_known(const snapshot& s, const process_row& p) { return p.cores_known && !(p.pid == s.vm.vmmem_pid && !s.vm.vmmem_cores_known); }

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
  const std::map<uint32_t, double> gpu_pct = busiest_engine(s);
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
  cols.add(pad_left("GPU%", 5), tone::dim).add(pad_left("LOCAL", 6), tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const process_row& p = *list[i];
    const bool is_vm = p.pid == s.vm.vmmem_pid || p.pid == s.vm.vmwp_pid;
    const double g = gpu_of(p.pid);
    const auto v = vram.find(p.pid);
    row r;
    r.add(pad_left(cores_text(windows_cores_known(s, p), p.cores), 6)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), is_vm ? tone::title : tone::text).add(pad_left(fmt("%.1f", gib(p.working_set)), 7));
    r.add(pad_left(g < 0 ? "-" : fmt("%.0f", g), 5)).add(pad_left(v == vram.end() ? "-" : fmt("%.1f", gib(v->second)), 6));
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
  // The "holder" column is /dev/dxg open, not GPU use, and this head is where the list says so. The
  // longest wording that fits, and the distro, which the WSL section names too, gives way before it.
  static constexpr std::string_view kHeads[] = {", /dev/dxg holders first, then cores, no usage measured", ", holders first, no usage measured", ", dxg: no usage measured"};
  const std::string count = fmt("  top %d of %lld?", static_cast<int>(shown), static_cast<long long>(w.process_count));  // process_count: no ledger row
  const int taken = head.cols() + display_width(count), distro = w.distro.empty() ? 0 : display_width(w.distro) + 1;
  std::string_view label = kHeads[std::size(kHeads) - 1];
  for (const std::string_view h : kHeads) {
    if (taken + distro + static_cast<int>(h.size()) <= col) { label = h; break; }
  }
  const std::string name = fit(w.distro, col - taken - static_cast<int>(label.size()) - 1);
  if (!name.empty()) head.add(" ").add(name, tone::title, true);
  head.add(count, tone::dim).add(label, tone::dim);
  out.push_back(head);

  const bool pids = col >= 48;
  const int name_w = std::clamp(col - (pids ? 39 : 31), 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("stat rss GiB", 13), tone::dim).add("  st", tone::dim);
  cols.add("  dxg", tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const wsl_process& p = *list[i];
    row r;
    r.add(pad_left(cores_text(p.rated, p.cores), 6)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text).add(pad_left(fmt("%.1f", gib(p.rss)), 13));
    r.add("  ").add(pad(p.state, 1), p.state == "D" ? tone::watch : tone::dim);
    if (p.on_gpu) r.add("  holder");
    out.push_back(r);
  }
  return out;
}

line clip(const line& in, int width);  // Below

void side_by_side(std::vector<line>& out, const std::vector<line>& left, const std::vector<line>& right, int col) {
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
}

void process_section(std::vector<line>& out, const snapshot& s, const wsl_view& w, int width, int avail) {
  if (avail < 2) return;
  out.push_back(rule("PROCESSES", kTwoLists, width));
  if (width >= 100) {
    const int col = (width - kListGap) / 2;
    side_by_side(out, windows_list(s, col, avail - 3), wsl_list(w, width - col - kListGap, avail - 3), col);
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
    if (!text.empty()) out.push_back({std::move(text), p.color, p.bold, p.fg256, p.bg256});
    if (!whole) break;
  }
  return out;
}

// ---- The overall screen --------------------------------------------------------------------------

line title_row(const snapshot& s, int64_t now_ms) {
  row title;
  title.add("winwsltop", tone::title, true).add("  " + clock_text(s.unix_ms), tone::dim);
  if (now_ms > 0 && s.unix_ms > 0) {
    const double age = static_cast<double>(now_ms - s.unix_ms) / 1000.0;
    title.add(fmt("  data %.1f s old", std::max(0.0, age)), age >= kStaleS ? tone::watch : tone::dim, age >= kStaleS);
  } else {
    title.add("  data age unknown", tone::dim);
  }
  if (s.interval_s > 0) title.add(fmt("  every %.1f s", s.interval_s), tone::dim);
  return title;
}

std::vector<line> overall_frame(const snapshot& s, const wsl_view& w, int width, int height, int64_t now_ms) {
  if (height <= 0) return {};
  const std::vector<std::vector<line>> parts = {gpu_section(s, w, width), windows_section(s, width), wsl_section(s, w, width)};
  int fixed = 1;
  for (const auto& p : parts) fixed += static_cast<int>(p.size());
  constexpr int kBlanks = 4, kMinList = 8;  // Blank rows go first when the window is short
  const bool spaced = height >= fixed + kBlanks + kMinList;

  std::vector<line> frame;
  row title;
  for (const span& p : title_row(s, now_ms)) title.add(p.text, p.color, p.bold);
  if (s.memory.total > 0 || w.ok) title.add_if_fits("  ? not yet checked", width);  // Once on the panel: cache?, swap?, steal?, the guest's count
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

// ---- Graphs --------------------------------------------------------------------------------------
//
// Every graph is one figure, width mapped to the range shown. A measure with a ceiling is drawn 0-100; any
// other from 0 to its scale_top. Time places a sample, never its index. A cell is the mean of its samples,
// and the cell holding the window's maximum marks it with one dot. Graphs are not stacked, summed or
// overlaid: each names its own source.

constexpr int kGutter = 4;       // "100│"
constexpr int kMinGraph = 2;     // Rows of a graph when the window is short: room for "100" and "0"
constexpr int kMinPlot = kGutter + 9;  // Narrower than this "-60s" and "now" do not fit: no graph
constexpr int kMinPanel = 36;    // Fewer panels to a row rather than narrower than this
constexpr int kPanelGap = 2;
constexpr int kTitleLines = 3;
constexpr int kMaxGpuList = 14;  // Rows of the GPU panel's lists; height beyond this goes to the graphs
constexpr int kSideBySideGpu = 64;  // Those lists are short and stay side by side down to here

// What a graph's labels say about its window, from the samples and not the cells, so a maximum can sit
// above every cell. As measured: over 100 stays over 100. NaN with no sample in the window.
struct window_stats {
  double avg = std::numeric_limits<double>::quiet_NaN(), max = std::numeric_limits<double>::quiet_NaN();
  int max_at = -1;    // The column or cell holding the latest sample at the maximum
  int max_count = 0;  // Samples in that column or cell
  double min = std::numeric_limits<double>::quiet_NaN();  // Never printed: a zoomed graph's bottom comes from it
};

// Adds one sample to a window's figures. A later sample at the same maximum takes its place.
void add_to(window_stats& st, double& total, int& n, double v, int at) {
  total += v;
  ++n;
  if (!(v >= st.min)) st.min = v;
  if (!(v < st.max)) {  // Also the first, while max is NaN
    st.max = v;
    st.max_at = at;
  }
}

struct plotted {
  std::vector<double> cols;  // One per dot column, oldest on the left, 0-100; NaN where nothing was measured
  window_stats stats;
};

// A sample stands for the time since the previous one, if that one is close enough to join and measured
// this figure, and otherwise for its own column. A column is the mean of the samples that stand for some of
// it, each clamped to 0-100: the graph clamps, the label does not.
plotted plot_series(const history& h, double history_sample::*series, int64_t end_ms, int dots, int64_t span_ms) {
  plotted out;
  out.cols.assign(static_cast<size_t>(std::max(0, dots)), std::numeric_limits<double>::quiet_NaN());
  if (dots <= 0) return out;
  std::vector<double> sum(out.cols.size(), 0);
  std::vector<int> count(out.cols.size(), 0);
  const double end = static_cast<double>(end_ms), span = static_cast<double>(span_ms);
  const auto x_of = [&](double t) { return std::lround((dots - 1) * (1.0 - (end - t) / span)); };
  bool joined = false;
  double before = 0, total = 0;
  int n = 0;
  for (const history_sample& p : h.samples()) {
    const double t = static_cast<double>(p.unix_ms), value = p.*series;
    const bool known = std::isfinite(value);
    if (known && t <= end && end - t < span) {  // A sample stands for the second before it: one at -span is outside
      const long x = x_of(t);
      // Joined, it covers the columns since the one before; that one outside the window, from the left edge.
      const long after = end - before >= span ? 0L : std::max(0L, x_of(before) + 1);
      const long from = joined && t - before <= kGapMs ? std::min(x, after) : x;
      const double v = std::clamp(value, 0.0, 100.0);
      for (long k = from; k <= x; ++k) {
        sum[static_cast<size_t>(k)] += v;
        ++count[static_cast<size_t>(k)];
      }
      add_to(out.stats, total, n, value, static_cast<int>(x));
    }
    joined = known;
    before = t;
  }
  for (size_t k = 0; k < out.cols.size(); ++k) {
    if (count[k] > 0) out.cols[k] = sum[k] / count[k];
  }
  if (n > 0) {
    out.stats.avg = total / n;
    out.stats.max_count = count[static_cast<size_t>(out.stats.max_at)];
  }
  return out;
}

std::vector<double> plot_columns(const history& h, double history_sample::*series, int64_t end_ms, int dots, int64_t span_ms = history::kSpanMs) {
  return plot_series(h, series, end_ms, dots, span_ms).cols;
}

// The average and maximum of one figure over a window, placed as plot_series places its samples.
window_stats series_stats(const history& h, double history_sample::*series, int64_t end_ms, int64_t span_ms) {
  window_stats st;
  double total = 0;
  int n = 0;
  for (const history_sample& p : h.samples()) {
    const double value = p.*series;
    if (std::isfinite(value) && p.unix_ms <= end_ms && end_ms - p.unix_ms < span_ms) add_to(st, total, n, value, 0);
  }
  if (n > 0) st.avg = total / n;
  return st;
}

// The top of a graph with no ceiling: the window's maximum rounded up to 1, 2 or 5 times a power of ten,
// never below floor.
double scale_top(double max, double floor) {
  const double v = std::isfinite(max) ? std::max(max, floor) : floor;
  if (!(v > 0)) return floor;
  const double decade = std::pow(10.0, std::floor(std::log10(v)));
  for (const double step : {1.0, 2.0, 5.0, 10.0}) {
    if (step * decade >= v * (1 - 1e-12)) return step * decade;
  }
  return 10 * decade;
}

// Which dot column carries the maximum's dot, or -1: it holds more than one sample, so its mean can hide
// the maximum, and the dot is above the dot row the mean reaches. The dot's height is the maximum's.
int max_mark(const window_stats& st) { return st.max_count > 1 ? st.max_at : -1; }

// Filled braille, one string per row from the top, 0 to top. A measured value always shows at least its
// bottom dot, so a measured 0 is a line and a gap is empty. Column mark, if any, also gets one dot at
// mark_value's height when that is above its own.
std::vector<std::string> plot_rows(const std::vector<double>& cols, int rows, int mark = -1, double mark_value = 0, double top = 100) {
  std::vector<std::string> out(static_cast<size_t>(rows));
  const auto high = [&](double v) { return std::max(1, static_cast<int>(std::lround(std::clamp(v, 0.0, top) / top * 4 * rows))); };
  int mark_high = 0;
  if (mark >= 0 && mark < static_cast<int>(cols.size()) && !std::isnan(cols[static_cast<size_t>(mark)]) && std::isfinite(mark_value)) {
    const int h = high(mark_value);
    if (h > high(cols[static_cast<size_t>(mark)])) mark_high = h;
  }
  const int cells = static_cast<int>(cols.size()) / 2;
  for (int y = 0; y < rows; ++y) {
    const int below = (rows - 1 - y) * 4;  // Dots under this row
    for (int c = 0; c < cells; ++c) {
      unsigned bits = 0;
      for (int side = 0; side < 2; ++side) {
        const int at = 2 * c + side;
        const double v = cols[static_cast<size_t>(at)];
        if (std::isnan(v)) continue;
        const unsigned* dot = side == 0 ? kDotLeft : kDotRight;
        const int dots = std::clamp(high(v) - below, 0, 4);
        for (int k = 0; k < dots; ++k) bits |= dot[k];
        if (at == mark && mark_high > below && mark_high <= below + 4) bits |= dot[mark_high - below - 1];
      }
      if (bits) put_cp(out[static_cast<size_t>(y)], static_cast<char32_t>(0x2800 + bits));
      else out[static_cast<size_t>(y)] += ' ';
    }
  }
  return out;
}

// ---- Time buckets --------------------------------------------------------------------------------
//
// docs/design/cpu-panel.md section 5. A cell is a fixed stretch of the clock, floor(t / bucket), so a
// sample never changes cell and everything moves one cell at a time. Every row of a column uses one
// axis: cause and effect line up top to bottom.

struct time_axis {
  int cells = 0;                               // Columns: per_cell to a braille or heat cell
  int per_cell = 1;
  int64_t bucket_ms = 1;                       // A column's
  int64_t end_ms = 0;
  int64_t last = 0;                            // The column of the right edge: the last of the cell end_ms falls in
  std::vector<const history_sample*> samples;  // Those on the axis and the one before, oldest first
  std::vector<int> cell;                       // The cell of each; below 0 for that one before
};

int64_t floor_div(int64_t a, int64_t b) { return a / b - (a % b != 0 && a < 0 ? 1 : 0); }

// The right edge is the cell end_ms falls in. A cell is whole milliseconds, rounded up so cells cover the
// span and then up to an even number, on every axis, so that an axis of two columns a cell (per_cell 2,
// design/panel-grammar-slice2.md section 4) halves it exactly: its columns 2c and 2c + 1 lie inside cell c of
// the one-column axis over the same span and cells, whose right edge it shares. The last column is the second
// half of the cell end_ms falls in, which may not have begun: bucket_cells draws that cell from its first half.
time_axis make_axis(const history& h, int64_t end_ms, int64_t span_ms, int cells, int per_cell = 1) {
  time_axis a;
  a.per_cell = std::clamp(per_cell, 1, 2);
  a.end_ms = end_ms;
  const int n = std::max(0, cells);
  a.cells = n * a.per_cell;
  if (n == 0) return a;
  int64_t cell_ms = std::max<int64_t>(2, (span_ms + n - 1) / n);
  cell_ms += cell_ms % 2;
  a.bucket_ms = cell_ms / a.per_cell;
  const int64_t last = a.last = floor_div(end_ms, cell_ms) * a.per_cell + a.per_cell - 1;
  const sample_range all = h.samples();
  for (size_t i = 0; i < all.size(); ++i) {
    const int64_t back = last - floor_div(all[i].unix_ms, a.bucket_ms);  // Cells left of the right edge
    if (back < 0) break;                                                 // Ahead of the axis, and so is the rest
    const bool on_axis = back < a.cells && end_ms - all[i].unix_ms < span_ms;  // The window, as plot_series has it
    if (!on_axis && !a.samples.empty()) {  // Only the newest sample before the left edge is kept
      a.samples.back() = &all[i];
      continue;
    }
    a.samples.push_back(&all[i]);
    a.cell.push_back(on_axis ? a.cells - 1 - static_cast<int>(back) : -1);
  }
  return a;
}

// One figure on an axis, 0-100, NaN where nothing was measured. A bucket is the mean of the samples in it,
// each clamped to 0-100, and NaN never counts. A bucket no sample fell in, between two samples close enough
// to join, takes the later one, which stands for the time since the earlier: a cell narrower than a second
// would stripe.
template <class Value> std::vector<double> bucket_cells(const time_axis& a, Value value, double top = 100) {
  std::vector<double> out(static_cast<size_t>(a.cells), std::numeric_limits<double>::quiet_NaN());
  std::vector<int> count(static_cast<size_t>(a.cells), 0);
  bool joined = false;
  int from = 0;
  int64_t before = 0;
  for (size_t i = 0; i < a.samples.size(); ++i) {
    const history_sample& p = *a.samples[i];
    const double raw = value(p);
    const bool known = std::isfinite(raw);
    if (known) {
      const double v = std::clamp(raw, 0.0, top);
      const int c = a.cell[i];
      if (joined && p.unix_ms - before <= kGapMs) {
        for (int k = std::max(0, from + 1); k < std::min(c, a.cells); ++k) out[static_cast<size_t>(k)] = v;
      }
      if (c >= 0) {
        double& cell = out[static_cast<size_t>(c)];
        int& n = count[static_cast<size_t>(c)];
        cell = n == 0 ? v : cell + v;
        ++n;
      }
      from = c;
    }
    joined = known;
    before = p.unix_ms;
  }
  for (size_t c = 0; c < out.size(); ++c) {
    if (count[c] > 1) out[c] /= count[c];
  }
  // The right edge's second half, when it has not begun: the cell is what its first half measured, as a cell
  // of one column would be, not a gap at now.
  if (a.per_cell == 2 && a.cells >= 2 && a.last * a.bucket_ms > a.end_ms) out[static_cast<size_t>(a.cells - 1)] = out[static_cast<size_t>(a.cells - 2)];
  return out;
}

// The window's average and maximum on an axis, from the samples in its cells, and the cell of the maximum.
template <class Value> window_stats bucket_stats(const time_axis& a, Value value) {
  window_stats st;
  double total = 0;
  int n = 0;
  for (size_t i = 0; i < a.samples.size(); ++i) {
    const double v = value(*a.samples[i]);
    if (a.cell[i] >= 0 && std::isfinite(v)) add_to(st, total, n, v, a.cell[i]);
  }
  if (n == 0) return st;
  st.avg = total / n;
  for (size_t i = 0; i < a.samples.size(); ++i) {
    st.max_count += a.cell[i] == st.max_at && std::isfinite(value(*a.samples[i])) ? 1 : 0;
  }
  return st;
}

// Cells from the left that lie wholly before watching began: no sample can be there, and blank would
// read as a gap. Before any push, all of them; after pushes that could not be placed, none. Placed as
// plot_columns places the first sample, for the graphs it draws.
int unwatched_cells(const history& h, int64_t end_ms, int cells, int64_t span_ms = history::kSpanMs) {
  if (!h.watching()) return std::max(0, cells);
  if (h.samples().empty()) return 0;
  const double span = static_cast<double>(span_ms), ago = static_cast<double>(end_ms - h.watched_from_ms());
  if (cells <= 0 || ago >= span) return 0;
  const long x = std::lround((2.0 * cells - 1) * (1.0 - std::max(0.0, ago) / span));
  return static_cast<int>(std::clamp(x / 2, 0L, static_cast<long>(cells)));
}

// The same on a time axis, placed as make_axis places the first sample: the buckets before the one
// watching began in. That bucket holds the first sample, so it is watched. On an axis of two columns a cell,
// columns.
int unwatched_cells(const history& h, const time_axis& a) {
  if (!h.watching()) return a.cells;
  if (h.samples().empty()) return 0;
  const int64_t left = a.last - a.cells + 1, before = floor_div(h.watched_from_ms(), a.bucket_ms) - left;
  return static_cast<int>(std::clamp<int64_t>(before, 0, a.cells));
}

// A plot row as spans: the braille in the bar tone and, on the bottom row, a faint dot in every other
// blank cell of the unwatched part, so a fresh start reads as not watched yet rather than as missing.
// The rule sshvllmtop and machtop share: ⣀ a measured 0, blank a gap, · before watching began.
line plot_line(const std::string& plot, int unwatched) {
  line out;
  const auto put = [&](std::string_view text, tone t) {
    if (!out.empty() && out.back().color == t) out.back().text += text;
    else out.push_back({std::string(text), t});
  };
  int cell = 0;
  for (size_t i = 0; i < plot.size(); ++cell) {
    const size_t from = i;
    next_cp(plot, i);
    const std::string_view c(plot.data() + from, i - from);
    if (cell < unwatched && c == " ") put(cell % 2 == 0 ? "·" : " ", tone::faint);
    else put(c, tone::bar);
  }
  return out;
}

// The colour of a heat cell, or -1 for the terminal's own background: nothing measured is no colour at all.
int ramp256(double pct) {
  if (!std::isfinite(pct)) return -1;
  return pct < kQuietPct ? 236 : pct < kLowPct ? 60 : pct < kMidPct ? 67 : pct < kHotPct ? 110 : 179;
}

struct panel {
  std::vector<std::string> title;  // Pieces kept whole when the title wraps, while they fit
  std::string value;               // As measured
  tone color = tone::text;
  double history_sample::*series = nullptr;  // The graph's height, 0-100
  // What the graph's label prints, in its own unit, or null for series itself as a percent. The unit follows
  // the maximum; a percent follows each number.
  double history_sample::*label_series = nullptr;
  const char* label_format = "%.0f";
  const char* label_unit = "";
  bool star = false;  // Over 100 also carries `*`, which a caption on the panel explains
};

// No percent is printed for bytes or watts: those keep their own units, and the graph is their height.
std::string number(double v, const char* format) {
  if (!std::isfinite(v)) return "n/a";
  return std::fabs(v) < 1e6 ? fmt(format, v) : fmt("%.3g", v);  // Over a million is not a column's width
}

panel pct_panel(std::vector<std::string> title, bool known, double v, double history_sample::*series) {
  panel p{std::move(title), "n/a", tone::dim, series};
  if (known && std::isfinite(v)) {
    p.value = number(v, "%.0f") + "%";
    if (p.value.size() < 4) p.value.insert(0, 4 - p.value.size(), ' ');
    p.color = hot(v);
  }
  return p;
}

std::vector<std::string> wrap(const std::vector<std::string>& pieces, int w) {
  std::vector<std::string> lines;
  std::string current;
  const auto put = [&](const std::string& piece) {
    if (current.empty()) {
      current = piece;
    } else if (display_width(current) + 1 + display_width(piece) <= w) {
      current += " " + piece;
    } else {
      lines.push_back(current);
      current = piece;
    }
  };
  for (const std::string& piece : pieces) {
    if (display_width(piece) <= w) { put(piece); continue; }
    size_t from = 0;
    while (from <= piece.size()) {
      const size_t space = std::min(piece.find(' ', from), piece.size());
      put(piece.substr(from, space - from));
      from = space + 1;
    }
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

// The value first, so a long title never pushes it out of the panel.
std::vector<line> panel_title(const panel& p, int col) {
  int indent = display_width(p.value) + 2;
  const bool inline_value = col - indent >= 12;
  if (!inline_value) indent = 0;
  std::vector<std::string> lines = wrap(p.title, col - indent);
  if (!inline_value) lines.insert(lines.begin(), "");
  if (static_cast<int>(lines.size()) > kTitleLines) {
    std::string rest;
    for (size_t i = kTitleLines - 1; i < lines.size(); ++i) rest += (rest.empty() ? "" : " ") + lines[i];
    lines.resize(kTitleLines);
    lines.back() = fit(rest, col - indent);
  }
  std::vector<line> out;
  for (size_t i = 0; i < lines.size(); ++i) {
    row r;
    if (i == 0) r.add(p.value, p.color, p.color != tone::dim).add(inline_value ? "  " : "");
    else r.to(indent);
    r.add(lines[i], tone::title);
    out.push_back(r);
  }
  return out;
}

std::string span_text(int64_t span_ms) {
  return span_ms % 60000 == 0 && span_ms > 60000 ? fmt("%dm", static_cast<int>(span_ms / 60000)) : fmt("%ds", static_cast<int>(span_ms / 1000));
}

// "avg 60% max 95%", or "avg 8.4 max 9.8 GiB" in a unit that is not a percent: label's figures, as measured.
// Past the graph's ceiling, which drawn's maximum says, the maximum is in the watch tone, with `*` where the
// panel's caption explains it. Nothing with no sample in the window.
line stats_label(const window_stats& drawn, const window_stats& label, const char* format = "%.0f", const char* unit = nullptr, bool star = false) {
  if (std::isnan(label.avg)) return {};
  const auto text = [&](double v) { return number(v, format) + (unit ? "" : "%"); };
  const bool over = drawn.max > 100;
  return {{"avg " + text(label.avg) + " max ", tone::dim}, {text(label.max) + (unit ? unit : "") + (over && star ? "*" : ""), over ? tone::watch : tone::dim}};
}

line stats_label(const panel& p, const history& h, const window_stats& drawn, int64_t end_ms, int64_t span_ms) {
  if (!p.label_series) return stats_label(drawn, drawn, p.label_format, nullptr, p.star);
  return stats_label(drawn, series_stats(h, p.label_series, end_ms, span_ms), p.label_format, p.label_unit, p.star);
}

int width_of(const line& l) {
  int w = 0;
  for (const span& p : l) w += display_width(p.text);
  return w;
}

// "-60s ──── label ──── now" over `cells` columns: the first of the labels that leaves two rule characters,
// or none. The range and now are never the ones dropped.
int axis_rules(int cells, int64_t span_ms) { return cells - display_width("-" + span_text(span_ms) + " ") - 4; }  // " now"

bool axis_label_fits(int cells, int64_t span_ms, const line& label) { return !label.empty() && axis_rules(cells, span_ms) - width_of(label) - 2 >= 2; }

void axis_line(row& r, int cells, int64_t span_ms, const std::vector<line>& labels) {
  const std::string left = "-" + span_text(span_ms) + " ";
  const int rules = axis_rules(cells, span_ms);
  r.add(left, tone::dim);
  const auto fits = std::find_if(labels.begin(), labels.end(), [&](const line& l) { return axis_label_fits(cells, span_ms, l); });
  if (fits == labels.end()) {
    r.add(repeat("─", rules), tone::faint);
  } else {
    const int rest = rules - width_of(*fits) - 2;
    r.add(repeat("─", rest / 2), tone::faint).add(" ");
    for (const span& p : *fits) r.add(p.text, p.color);
    r.add(" ").add(repeat("─", rest - rest / 2), tone::faint);
  }
  r.add(" now", tone::dim);
}

std::vector<line> panel_graph(const panel& p, const history& h, int64_t end_ms, int col, int rows, int64_t span_ms) {
  const int cells = col - kGutter;
  const plotted d = plot_series(h, p.series, end_ms, 2 * cells, span_ms);
  const std::vector<std::string> plot = plot_rows(d.cols, rows, max_mark(d.stats), d.stats.max);
  const int unwatched = unwatched_cells(h, end_ms, cells, span_ms);
  std::vector<line> out;
  for (int y = 0; y < rows; ++y) {
    row r;
    r.add(y == 0 ? "100" : y == rows - 1 ? "  0" : "   ", tone::dim).add("│", tone::faint);
    for (const span& c : plot_line(plot[static_cast<size_t>(y)], y == rows - 1 ? unwatched : 0)) r.add(c.text, c.color);
    out.push_back(r);
  }
  row axis;
  axis.to(kGutter);
  axis_line(axis, cells, span_ms, {stats_label(p, h, d.stats, end_ms, span_ms)});
  out.push_back(axis);
  return out;
}

struct grid_row {
  std::vector<const panel*> cells;
  int col = 0, title_h = 0, graph_h = kMinGraph;
  bool plot = true;
  int height() const { return title_h + (plot ? graph_h + 1 : 0); }
};

// Panels of a group share a row while each keeps kMinPanel columns, and stack below that. Every panel
// of a group has the same width, so one column means the same time in all of them.
void add_panels(std::vector<grid_row>& rows, const std::vector<panel>& group, int width) {
  const int per = std::clamp((width + kPanelGap) / (kMinPanel + kPanelGap), 1, static_cast<int>(group.size()));
  const int col = (width - kPanelGap * (per - 1)) / per;
  for (size_t i = 0; i < group.size(); i += static_cast<size_t>(per)) {
    grid_row r;
    r.col = col;
    r.plot = col >= kMinPlot;
    for (size_t k = i; k < std::min(group.size(), i + static_cast<size_t>(per)); ++k) {
      r.cells.push_back(&group[k]);
      r.title_h = std::max(r.title_h, static_cast<int>(panel_title(group[k], col).size()));
    }
    rows.push_back(std::move(r));
  }
}

int rows_height(const std::vector<grid_row>& rows) {
  int total = 0;
  for (const grid_row& r : rows) total += r.height();
  return total;
}

// Graphs at their minimum. From the bottom, a panel that does not fit gives up its plot and keeps its
// value and label; only when those do not fit either does it go.
bool fit_rows(std::vector<grid_row>& rows, int room) {
  bool hidden = false;
  for (size_t i = rows.size(); i-- > 0 && rows_height(rows) > room;) {
    rows[i].plot = false;
    hidden = true;
  }
  while (rows.size() > 1 && rows_height(rows) > room) {
    rows.pop_back();
    hidden = true;
  }
  return hidden;
}

void grow_rows(std::vector<grid_row>& rows, int extra) {
  int plotted = 0;
  for (const grid_row& r : rows) plotted += r.plot ? 1 : 0;
  int seen = 0;
  for (grid_row& r : rows) {
    if (!r.plot) continue;
    r.graph_h += extra / plotted + (seen < extra % plotted ? 1 : 0);
    ++seen;
  }
}

void draw_rows(std::vector<line>& out, const std::vector<grid_row>& rows, const history& h, int64_t end_ms, int64_t span_ms) {
  for (const grid_row& r : rows) {
    std::vector<std::vector<line>> columns;
    for (const panel* p : r.cells) {
      std::vector<line> c = panel_title(*p, r.col);
      c.resize(static_cast<size_t>(r.title_h));
      if (r.plot) {
        const std::vector<line> graph = panel_graph(*p, h, end_ms, r.col, r.graph_h, span_ms);
        c.insert(c.end(), graph.begin(), graph.end());
      }
      columns.push_back(std::move(c));
    }
    for (size_t i = 0; i < columns[0].size(); ++i) {
      line joined;
      for (size_t k = 0; k < columns.size(); ++k) {
        int used = 0;
        for (const span& p : clip(columns[k][i], r.col)) { joined.push_back(p); used += display_width(p.text); }
        if (k + 1 < columns.size()) joined.push_back({std::string(static_cast<size_t>(r.col - used + kPanelGap), ' ')});
      }
      out.push_back(std::move(joined));
    }
  }
}

int64_t graph_end(const history& h, int64_t now_ms) {  // A clock behind the newest sample is not "now"
  return h.samples().empty() ? 0 : std::max(now_ms, h.samples().back().unix_ms);
}

// The newest sample is the right edge unless it is late: a redraw between samples must not open an empty
// bucket at now, because blank means not measured.
int64_t axis_end(const history& h, int64_t now_ms) {
  const int64_t last_sample = h.samples().empty() ? 0 : h.samples().back().unix_ms;
  return !h.samples().empty() && static_cast<double>(now_ms - last_sample) <= kGapMs ? last_sample : graph_end(h, now_ms);
}

// ---- The panels ----------------------------------------------------------------------------------

struct panel_frame {
  std::vector<line> lines;
  bool hidden = false;  // A block was dropped for want of rows
  bool graph = false;   // A graph over the range is drawn, so ←→ changes something on screen
  int selectable = 0;   // Lists drawn that ↑↓ can move in, so the footer offers them, and tab with two
};

bool any_plot(const std::vector<grid_row>& rows) {
  return std::any_of(rows.begin(), rows.end(), [](const grid_row& r) { return r.plot; });
}

// Whether a label carrying `*` is drawn: a starred panel's plot is shown and its label, over 100, fits.
bool star_drawn(const std::vector<grid_row>& rows, const history& h, int64_t end_ms, int64_t span_ms) {
  for (const grid_row& r : rows) {
    if (!r.plot) continue;
    for (const panel* p : r.cells) {
      if (!p->star) continue;
      const window_stats st = series_stats(h, p->series, end_ms, span_ms);
      if (st.max > 100 && axis_label_fits(r.col - kGutter, span_ms, stats_label(*p, h, st, end_ms, span_ms))) return true;
    }
  }
  return false;
}

// ---- Panel 3, the large layout -------------------------------------------------------------------
//
// docs/design/cpu-panel.md: the top rows are the answer, the timeline below them the evidence. Windows
// left, WSL right, two columns that sit next to each other and never share a graph or a sort.

constexpr int kCpuWide = 100;        // Narrower than this is the small layout, as is too short a window
constexpr int kCpuGutter = 5;        // "100 │", "kern│", "lost≈"
constexpr int kListHeads = 2;        // A list's head and column rows
constexpr int kLeastListRows = 3, kGoodListRows = 6;
constexpr int kLeastBusyRows = 3, kMostBusyRows = 12;
constexpr int kLaneRows = 2;
constexpr int kFloorHeatRows = 10, kMostHeatRows = 20;  // Below the first the heat block has what it asks for
constexpr int kHistoryRows = 2, kMostHistoryRows = 6;  // The selected process's history, under its title
// Title, rule, 3 heads, the lanes, caption, axis, a list's head and columns, its N of M line, the footer.
constexpr int kCpuFixedRows = 5 + kLaneRows + 2 + kListHeads + 1 + 1;
constexpr int kHeadValue = 4;        // Columns of the value that starts a head
constexpr int kLeastDistro = 6;      // A name cut shorter than this says nothing

// Rows of each block. height includes the footer. design/panel-grammar.md part 3, "Rows", in its order;
// false when the window is below this layout's floor.
struct cpu_rows {
  int heat = 0, busy = kLeastBusyRows, lists = kLeastListRows, history = kHistoryRows;
};

bool plan_cpu_rows(int natural, int height, cpu_rows& p) {
  p = {};
  p.heat = std::min(natural, kFloorHeatRows);
  int left = height - kCpuFixedRows - p.heat - p.busy - p.lists - (p.history + 1);
  if (left < 0) {  // A list keeps 3 rows: the history goes first, title and all
    p.history = 0;
    left += kHistoryRows + 1;
  }
  if (left < 0) return false;
  const auto take = [&](int& rows, int most) {
    const int extra = std::clamp(most - rows, 0, left);
    rows += extra;
    left -= extra;
  };
  take(p.lists, kGoodListRows);  // The lists are guaranteed before anything graphs more
  // A part-of-whole history wants one row more than its least, for its legend (design/panel-grammar-slice2.md Q6).
  if (p.history > 0) take(p.history, kHistoryRows + 1);
  take(p.heat, std::min(natural, kMostHeatRows));
  take(p.busy, kMostBusyRows);
  if (p.history > 0) take(p.history, kMostHistoryRows);
  p.lists += left;
  return true;
}

// ---- Which thread is drawn where

// Rows in layout order, each a group of threads, and what a row starting at a thread is called.
struct thread_order {
  std::vector<std::vector<int>> groups;
  std::vector<std::string> label;  // By logical CPU
};

// Two neighbours to a row, in number order: the guest's vCPUs, and Windows with no usable layout.
thread_order numbered_threads(int n, const char* prefix) {
  thread_order o;
  for (int i = 0; i < n; ++i) o.label.push_back(prefix + std::to_string(i));
  for (int i = 0; i < n; i += 2) o.groups.push_back(i + 1 < n ? std::vector<int>{i, i + 1} : std::vector<int>{i});
  return o;
}

// P is the highest efficiency class and E all others, each numbered by core. docs/measurable.md, "CPU
// layout". Only a P-core's row holds real siblings. With no hybrid cores or no SMT they are all `C`.
thread_order windows_order(const cpu_layout_view& layout, int n) {
  if (!layout.known || static_cast<int>(layout.rows.size()) != n || n == 0) return numbered_threads(n, "");
  struct core_threads {
    uint32_t core, efficiency;
    std::vector<int> threads;
  };
  std::vector<core_threads> cores;
  for (int i = 0; i < n; ++i) {
    const cpu_layout_row& r = layout.rows[static_cast<size_t>(i)];
    const auto c = std::find_if(cores.begin(), cores.end(), [&](const core_threads& k) { return k.core == r.core; });
    if (c == cores.end()) cores.push_back({r.core, r.efficiency, {i}});
    else c->threads.push_back(i);
  }
  for (core_threads& c : cores) {
    std::stable_sort(c.threads.begin(), c.threads.end(), [&](int a, int b) { return layout.rows[static_cast<size_t>(a)].thread < layout.rows[static_cast<size_t>(b)].thread; });
  }
  std::stable_sort(cores.begin(), cores.end(), [](const core_threads& a, const core_threads& b) {
    return a.efficiency != b.efficiency ? a.efficiency > b.efficiency : a.core < b.core;
  });
  const uint32_t top = cores.front().efficiency;
  const bool hybrid = cores.back().efficiency != top;
  const bool smt = std::any_of(cores.begin(), cores.end(), [](const core_threads& c) { return c.threads.size() > 1; });

  thread_order o;
  o.label.resize(static_cast<size_t>(n));
  std::vector<int> neighbours;  // Waiting for a second thread to share a row with
  int numbered[2] = {0, 0};     // Next core number: P or C, then E
  for (const core_threads& c : cores) {
    const bool named = hybrid && smt, lead = !named || c.efficiency == top;
    const std::string name = (named ? (lead ? "P" : "E") : "C") + std::to_string(numbered[lead ? 0 : 1]++);
    for (const int t : c.threads) o.label[static_cast<size_t>(t)] = name;
    if (smt && lead) {  // Siblings
      o.groups.push_back(c.threads);
      continue;
    }
    for (const int t : c.threads) {
      neighbours.push_back(t);
      if (neighbours.size() == 2) {
        o.groups.push_back(neighbours);
        neighbours.clear();
      }
    }
  }
  if (!neighbours.empty()) o.groups.push_back(neighbours);
  return o;
}

struct heat_row {
  std::string label;
  std::vector<int> top, bottom;  // The threads of each half; a half that holds several shows the highest
};

// Fits an order into rows: a row a group, or, past that, the threads shared evenly between the 2 lines
// of pixels each row has.
std::vector<heat_row> heat_rows(const thread_order& o, int rows, bool& highest) {
  highest = false;
  std::vector<heat_row> out;
  const auto put = [&](std::vector<int> top, std::vector<int> bottom) {
    if (top.empty()) return;
    highest = highest || top.size() > 1 || bottom.size() > 1;
    out.push_back({o.label[static_cast<size_t>(top[0])], std::move(top), std::move(bottom)});
  };
  if (static_cast<int>(o.groups.size()) <= rows) {
    for (const std::vector<int>& g : o.groups) {
      const auto half = g.begin() + static_cast<std::ptrdiff_t>((g.size() + 1) / 2);
      put({g.begin(), half}, {half, g.end()});
    }
    return out;
  }
  std::vector<int> all;
  for (const std::vector<int>& g : o.groups) all.insert(all.end(), g.begin(), g.end());
  const int n = static_cast<int>(all.size()), lines = 2 * std::max(rows, 0);
  const auto pixels = [&](int l) {
    // Up to one thread a line goes in order; past that, floor(l * n / lines) gives each line at least one.
    const int from = n <= lines ? std::min(l, n) : l * n / lines, to = n <= lines ? std::min(l + 1, n) : (l + 1) * n / lines;
    return std::vector<int>(all.begin() + from, all.begin() + to);
  };
  for (int r = 0; r < rows; ++r) put(pixels(2 * r), pixels(2 * r + 1));
  return out;
}

using thread_value = float (*)(const history_sample&, int);

// The cells of one half: each thread's mean, and the highest of them where the half holds several.
std::vector<double> half_cells(const time_axis& a, const std::vector<int>& threads, thread_value value) {
  std::vector<double> out(static_cast<size_t>(a.cells), std::numeric_limits<double>::quiet_NaN());
  for (const int t : threads) {
    const std::vector<double> one = bucket_cells(a, [&](const history_sample& p) { return static_cast<double>(value(p, t)); });
    for (size_t c = 0; c < out.size(); ++c) {
      if (std::isnan(out[c]) || one[c] > out[c]) out[c] = std::isnan(one[c]) ? out[c] : one[c];
    }
  }
  return out;
}

// ---- Drawing the blocks

void heat_cell(row& r, double top, double bottom) {
  const int a = ramp256(top), b = ramp256(bottom);
  if (a >= 0) r.cell("▀", a, b);
  else if (b >= 0) r.cell("▄", b);
  else r.cell(" ", -1);
}

void gutter(row& r, std::string_view label, std::string_view bar = "│", tone c = tone::faint) {
  r.add(pad(label, kCpuGutter - 1), tone::dim).add(bar, c);
}

// A plot over an axis of two columns a cell (design/panel-grammar-slice2.md section 4): each dot column of a
// braille cell is its own column of the axis, and the maximum's dot is in the column that holds it, left or
// right. unwatched counts columns; the faint dot goes only in a cell both of whose columns are unwatched.
// With no gutter, a history's form (design/panel-grammar-slice2.md section 6): the plot is the full width and its
// figures are in the heading.
std::vector<line> plot_block(const std::vector<double>& columns, int rows, std::string_view top, std::string_view bottom, int unwatched, const window_stats& st, double ceiling = 100, bool with_gutter = true) {
  const std::vector<std::string> plot = plot_rows(columns, rows, max_mark(st), st.max, ceiling);
  std::vector<line> out;
  for (int y = 0; y < rows; ++y) {
    row r;
    if (with_gutter) gutter(r, y == 0 ? top : y == rows - 1 ? bottom : std::string_view());
    for (const span& c : plot_line(plot[static_cast<size_t>(y)], y == rows - 1 ? unwatched / 2 : 0)) r.add(c.text, c.color);
    out.push_back(r);
  }
  return out;
}

line lane(std::string_view label, const std::vector<double>& cells) {
  row r;
  gutter(r, label);
  for (const double v : cells) {
    const int c = ramp256(v);
    if (c >= 0) r.cell("█", c);
    else r.cell(" ", -1);
  }
  return r;
}

// Under 1 vCPU is quiet, which wsl-20 alone reads, and from 1 vCPU it is amber, the cell the panel exists for.
line lost_lane(const std::vector<double>& vcpus) {
  row r;
  r.add(pad("lost", kCpuGutter - 1), tone::dim).add("≈", tone::watch);
  for (const double v : vcpus) {
    if (std::isnan(v)) r.cell(" ", -1);
    else r.cell("█", v >= kLostFloor ? 179 : 236);
  }
  return r;
}

// "-60s ──── busy avg 60% max 100% · each cell 1.0 s ──── now": the column's busy plot over the window,
// then how long a cell is, which goes first when the row is short.
line axis_row(const time_axis& a, int64_t span_ms, const window_stats& busy) {
  const double bucket_s = static_cast<double>(a.bucket_ms) / 1000.0;
  line label = stats_label(busy, busy);
  if (!label.empty()) label.insert(label.begin(), span{"busy ", tone::dim});
  line cell = label;
  cell.push_back({fmt(bucket_s < 10 ? " · each cell %.1f s" : " · each cell %.0f s", bucket_s), tone::dim});
  row r;
  r.to(kCpuGutter);
  axis_line(r, a.cells, span_ms, {cell, label});
  return r;
}

int hot_threads(const std::vector<double>& pct) {
  return static_cast<int>(std::count_if(pct.begin(), pct.end(), [](double v) { return std::isfinite(v) && v >= kHotPct; }));
}

// "20 of 20 ≥90%", or n/a with no figures to count.
std::string hot_count(const std::vector<double>& pct, bool usable) {
  if (!usable || pct.empty()) return "n/a ≥" + fmt("%.0f%%", kHotPct);
  return fmt("%d of %d ≥%.0f%%", hot_threads(pct), static_cast<int>(pct.size()), kHotPct);
}

// Drops the legend before the count, and keeps the block's name and "highest of each row" to the end.
line heat_caption(std::string_view name, const std::string& count, bool highest, bool legend, int col) {
  static constexpr std::string_view kHighest = " · highest of each row";
  static constexpr int kSwatches[] = {236, 60, 67, 110, 179};
  const int need = display_width(name) + (highest ? display_width(kHighest) : 0);
  row r;
  r.add(name, tone::dim);
  if (need + 3 + display_width(count) <= col) r.add(" · " + count);
  if (highest) r.add(kHighest, tone::dim);
  if (legend && r.cols() + 5 + 15 <= col) {
    r.add("     idle ", tone::dim);
    for (const int c : kSwatches) r.cell("█", c);
    r.add(" ≥" + fmt("%.0f%%", kHotPct), tone::dim);
  }
  return r;
}

// ---- The four head rows

double or_nan(const std::optional<double>& v) { return v ? *v : std::numeric_limits<double>::quiet_NaN(); }

std::string part(const char* name, double v) { return std::string(name) + " " + number(v, "%.0f"); }

// "100%  WINDOWS  ": the value as measured, in the columns the head has kept for it.
void head_value(row& r, bool known, double busy, bool stale, std::string_view name) {
  const panel p = pct_panel({}, known, busy, nullptr);
  r.add(p.value, stale ? tone::watch : p.color, p.color != tone::dim).to(kHeadValue).add("  ").add(name, tone::title, true).add("  ");
}


std::vector<line> windows_heads(const snapshot& s, int threads, int col) {
  const bool read = !s.cpu.core_pct.empty();  // A failed read leaves a default view: its 0 was not measured
  const bool vm = s.vm.vmmem_cores_known;     // Without it vmmem_cores is an unmeasured 0
  const double missing = std::numeric_limits<double>::quiet_NaN();
  row busy;
  head_value(busy, read, s.cpu.busy_pct, false, "WINDOWS");
  busy.add(part("user", read ? s.cpu.user_pct : missing) + " · " + part("kernel", read && vm ? s.cpu_derived.own_kernel_pct : missing) + " · " +
               part("VM", read && vm ? s.cpu_derived.vm_cpu_pct : missing),
           read ? tone::text : tone::dim);

  row source;
  source.to(kHeadValue + 2).add("whole machine, includes the WSL VM", tone::dim);

  row ran;
  ran.add("VM ran ", tone::dim).add(vm ? number(s.vm.vmmem_cores, "%.2f") : "n/a", vm ? tone::text : tone::dim);
  ran.add(threads > 0 ? fmt(" of %d threads", threads) : " of n/a threads", tone::dim);
  const double ready = s.cpu_derived.ready_threads_mean, vm_ready = s.cpu_derived.vm_ready_threads_mean;
  if (std::isfinite(ready)) {  // 5-sample means: one instant's count is noise, and the mark says so
    const std::string count = " · ≈" + number(ready, "%.0f") + " ready";
    const std::string all = count + (std::isfinite(vm_ready) ? ", " + number(vm_ready, "%.0f") + " VM" : s.vm_ready_threads ? ", VM collecting" : ", VM n/a");
    ran.add_if_fits(ran.cols() + display_width(all) <= col ? all : count, col);
  } else {
    ran.add_if_fits(s.ready_threads ? " · ready collecting" : " · ready n/a", col);
  }

  return {busy, source, ran};
}

// What stands in the head's lost figure, named for each reason it is not a number.
const char* lost_reason(lost_state state) {
  switch (state) {
    case lost_state::collecting: return " · lost CPU collecting";
    case lost_state::guest_under_99: return " · lost CPU n/a under 99% busy";
    case lost_state::windows_under_95: return " · lost CPU n/a, Windows under 95%";
    default: return " · lost CPU n/a";
  }
}

void short_head(row& r, const snapshot& s, const wsl_view& w, int col) {
  const std::string load = "load? " + number(or_nan(w.load[0]), "%.1f");  // No ledger row: /proc/loadavg is not checked
  if (s.lost.state != lost_state::known || !std::isfinite(s.lost.vcpus)) {
    r.add(load);
    r.add_if_fits(s.lost.state == lost_state::vcpus_differ ? " · lost CPU n/a, vCPUs differ" : lost_reason(s.lost.state), col);
    return;
  }
  const bool real = s.lost.vcpus >= kLostFloor;  // watch from here: this row must not look calmer than a healthy busy machine
  const int threads = static_cast<int>(w.core_pct.size());
  if (real) r.add("WSL short ≈" + number(s.lost.vcpus, "%.0f") + fmt(" of %d vCPUs, inferred", threads), tone::watch, true);
  else r.add("WSL short <1 vCPU, inferred", tone::text);
  r.add_if_fits(" · " + load, col);
}


std::vector<line> wsl_heads(const snapshot& s, const wsl_view& parsed, int col, int64_t waited_ms) {
  static const wsl_view kNothing;
  const wsl_view& w = parsed.ok ? parsed : kNothing;  // A line that was refused says nothing, even in the part that parsed
  const bool stale = w.ok && s.wsl_age_s >= kStaleS;
  row busy;
  head_value(busy, w.ok, w.busy_pct, stale, "WSL");
  busy.add(part("user", or_nan(w.cpu_user)) + " · " + part("sys", or_nan(w.cpu_sys)) + " · " + part("iowait", or_nan(w.cpu_iowait)), w.ok ? tone::text : tone::dim);
  busy.add_if_fits(", as idle", col);

  // The descriptor stays whole. The distro gives way to the age and the steal figure.
  static constexpr std::string_view kSource = "guest view, inside the VM";
  std::string age, steal;
  if (stale) age = " · " + number(s.wsl_age_s, "%.0f") + " s old";
  else if (w.ok && s.wsl_age_s < 0) age = " · age unknown";
  if (w.ok && w.steal_pct > 0) steal = " · steal? " + number(w.steal_pct, "%.1f") + "%";
  row source;
  source.to(kHeadValue + 2).add(kSource, tone::dim);
  const int room = col - source.cols() - display_width(age) - display_width(steal) - 3;
  if (room >= kLeastDistro && !w.distro.empty()) source.add(" · " + fit(w.distro, room), tone::dim);
  source.add(age, tone::watch, true).add_if_fits(steal, col);

  // Memory and its pressure are panel 4's (design/panel-grammar.md part 3, "Rows").
  row third;
  if (w.ok) {
    short_head(third, s, w, col);
  } else if (s.wsl_json.empty() && s.wsl_restarts == 0) {
    third.add(fmt("WSL starting, no data yet: %d s", static_cast<int>(waited_ms / 1000)), tone::watch);
  } else {
    third.add(s.wsl_json.empty() ? "WSL unavailable: the collector sent nothing" : parsed.schema > 1 ? "WSL unavailable: newer schema than this screen" : "WSL unavailable: unreadable collector line", tone::watch);
  }
  if (!w.ok && s.wsl_restarts > 0) {  // That it keeps dying outranks why, which panel 1 also gives
    const std::string restarted = fmt(" · collector restarted %d times", s.wsl_restarts);
    if (third.cols() + display_width(restarted) > col) third = row().add(fmt("WSL unavailable, collector restarted %d times", s.wsl_restarts), tone::watch);
    else third.add(restarted, tone::watch);
  }
  return {busy, source, third};
}

// ---- The lists: each sorted on its own, in its own units

// Rounded to 0.1 and then PID, so that rows of similar cores do not swap each second.
int64_t cores_key(double cores) { return std::isfinite(cores) ? std::llround(std::clamp(cores, 0.0, 1e9) * 10) : -1; }

// Panel 3's lists in the order they are drawn, which is the order the selection moves through.
std::vector<const process_row*> windows_cpu_order(const snapshot& s) {
  std::vector<const process_row*> list;
  for (const process_row& p : s.processes) {
    if (p.pid != 0) list.push_back(&p);  // PID 0 is the idle process: its cores are idle time
  }
  std::stable_sort(list.begin(), list.end(), [](const process_row* a, const process_row* b) {
    const int64_t ka = cores_key(a->cores), kb = cores_key(b->cores);
    return ka != kb ? ka > kb : a->pid < b->pid;
  });
  return list;
}

std::vector<const wsl_process*> wsl_cpu_order(const wsl_view& w) {
  std::vector<const wsl_process*> list;
  if (!w.ok) return list;
  for (const wsl_process& p : w.processes) list.push_back(&p);
  std::stable_sort(list.begin(), list.end(), [](const wsl_process* a, const wsl_process* b) {
    const int64_t ka = cores_key(a->cores), kb = cores_key(b->cores);
    return ka != kb ? ka > kb : a->pid < b->pid;
  });
  return list;
}

// Panel 4's lists in the order drawn: Windows by working set, the guest by stat rss, then PID.
std::vector<const process_row*> windows_mem_order(const snapshot& s) {
  std::vector<const process_row*> list;
  for (const process_row& p : s.processes) {
    if (p.pid != 0) list.push_back(&p);  // PID 0 is the idle process
  }
  std::stable_sort(list.begin(), list.end(), [](const process_row* a, const process_row* b) {
    return a->working_set != b->working_set ? a->working_set > b->working_set : a->pid < b->pid;
  });
  return list;
}

std::vector<const wsl_process*> wsl_mem_order(const wsl_view& w) {
  std::vector<const wsl_process*> list;
  if (!w.ok) return list;
  for (const wsl_process& p : w.processes) list.push_back(&p);
  std::stable_sort(list.begin(), list.end(), [](const wsl_process* a, const wsl_process* b) { return a->rss != b->rss ? a->rss > b->rss : a->pid < b->pid; });
  return list;
}

// A row of panel 2's Windows list: a process using an engine at some sample inside the range on screen, or
// holding GPU `local` memory now. Its key is pid, name and create time from the snapshot, or from its slot in the
// ring once it has left the snapshot, never made up, so its slot is found and kept (audit finding 4).
struct gpu_row {
  row_key key;
  double now = std::numeric_limits<double>::quiet_NaN();  // GPU %, busiest engine, this sample; NaN unread or gone
  int64_t seen_ms = 0;                                     // Its latest use inside the range, or 0
  bool used = false;                                       // An engine above 0 inside the range
  bool has_local = false;
  uint64_t local = 0;
};

// The first millisecond a history `width` columns wide draws: the left edge of make_axis(h, end_ms, span_ms, width,
// 2), which lies up to one cell inside the span, since its cells are whole buckets of the clock. With no width, the
// span's own start.
int64_t history_from_ms(int64_t end_ms, int64_t span_ms, int width) {
  if (width <= 0) return std::numeric_limits<int64_t>::min();
  int64_t cell_ms = std::max<int64_t>(2, (span_ms + width - 1) / width);
  cell_ms += cell_ms % 2;
  return (floor_div(end_ms, cell_ms) - width + 1) * cell_ms;
}

// In the order drawn: by GPU % now, then most recent use, then pid; a holder with no use in the range last, by
// `local` memory. end_ms is the range's right edge, axis_end, as the frame draws it, and a use counts from the
// first column the history below draws (history_from_ms), so no row is listed whose use it cannot show (audit
// finding 5).
std::vector<gpu_row> gpu_rows(const snapshot& s, const history* h, int64_t end_ms, int64_t span_ms, int width = 0) {
  const std::map<uint32_t, double> engines = busiest_engine(s);
  std::map<uint32_t, uint64_t> local;
  for (const gpu_memory_row& m : s.gpu_memory) local[m.pid] = m.local;
  const int64_t from_ms = history_from_ms(end_ms, span_ms, width);
  const auto in_range = [&](int64_t t) { return t != 0 && t <= end_ms && end_ms - t < span_ms && t >= from_ms; };
  std::vector<gpu_row> out;
  for (const process_row& p : s.processes) {
    if (p.pid == 0) continue;
    gpu_row r;
    r.key = {true, p.pid, p.name, p.create_time};
    if (s.gpu_engines_known) {
      const auto e = engines.find(p.pid);
      r.now = e == engines.end() ? 0 : e->second;
    }
    if (h) {
      const int slot = h->slot_of({0, p.pid, p.create_time, p.name});
      if (slot >= 0 && in_range(h->gpu_seen_ms(slot))) r.seen_ms = h->gpu_seen_ms(slot);
    }
    r.used = r.now > 0 || r.seen_ms != 0;
    const auto l = local.find(p.pid);
    r.has_local = l != local.end();
    r.local = r.has_local ? l->second : 0;
    if (r.used || r.local > 0) out.push_back(r);
  }
  if (h) {  // Used inside the range and gone since: from the ring, under the slot's own name and start
    for (const history::gpu_slot& g : h->gpu_slots()) {
      if (g.id.side != 0 || !in_range(g.seen_ms)) continue;
      const bool listed = std::any_of(out.begin(), out.end(), [&](const gpu_row& r) { return r.key.pid == g.id.pid && r.key.name == g.id.name && r.key.start == g.id.start; });
      if (listed) continue;
      gpu_row r;
      r.key = {true, g.id.pid, g.id.name, g.id.start};
      r.seen_ms = g.seen_ms;
      r.used = true;
      out.push_back(r);
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const gpu_row& a, const gpu_row& b) {
    if (a.used != b.used) return a.used;
    if (a.used) {
      const double na = std::isfinite(a.now) ? a.now : -1, nb = std::isfinite(b.now) ? b.now : -1;
      if (na != nb) return na > nb;
      if (a.seen_ms != b.seen_ms) return a.seen_ms > b.seen_ms;
    } else if (a.local != b.local) {
      return a.local > b.local;
    }
    return a.key.pid < b.key.pid;
  });
  return out;
}

// The selectable lists of a panel, each as its rows' keys in the order drawn: the Windows and WSL lists of
// panels 3 and 4. A list with no row is not selectable, and nor is the guest's when its line is unreadable
// or stale: design/panel-grammar.md part 3, "WSL unavailable or stale ... nothing to select". Panel 2 has one,
// its Windows list, whose rows read the ring over the range ending where the frame's does, axis_end(h, now_ms),
// so a key moves through the rows drawn (design/panel-grammar-slice2.md section 5, audit finding 8); its WSL list
// is a reference list. Without a history panel 2's list is this sample's alone.
std::vector<std::vector<row_key>> selectable_lists(const snapshot& s, int panel, const history* h = nullptr, int64_t now_ms = 0, int64_t span_ms = kSpansMs[0], int width = 0) {
  std::vector<std::vector<row_key>> lists(2);
  if (panel == 2) {
    for (const gpu_row& r : gpu_rows(s, h, h ? axis_end(*h, now_ms) : s.unix_ms, span_ms, width)) lists[0].push_back(r.key);
    return lists;
  }
  if (panel != 3 && panel != 4) return {};
  for (const process_row* p : panel == 3 ? windows_cpu_order(s) : windows_mem_order(s)) lists[0].push_back({true, p->pid, p->name, p->create_time});
  if (s.wsl_age_s < kStaleS) {
    const wsl_view w = read_wsl(s.wsl_json);
    for (const wsl_process* p : panel == 3 ? wsl_cpu_order(w) : wsl_mem_order(w)) lists[1].push_back({true, p->pid, p->name, p->start.value_or(0)});
  }
  return lists;
}

// The list that has focus: the one chosen, unless it has no rows and another has, which then has it.
int focused_list(int chosen, const std::vector<std::vector<row_key>>& lists) {
  const int n = static_cast<int>(lists.size());
  if (n == 0) return 0;
  const int c = std::clamp(chosen, 0, n - 1);
  if (!lists[static_cast<size_t>(c)].empty()) return c;
  for (int i = 0; i < n; ++i) {
    if (!lists[static_cast<size_t>(i)].empty()) return i;
  }
  return c;
}

int selectable_count(const std::vector<std::vector<row_key>>& lists) {
  return static_cast<int>(std::count_if(lists.begin(), lists.end(), [](const std::vector<row_key>& l) { return !l.empty(); }));
}

// A row is its key's when pid and name are equal and, where both starts are known (non-zero), the starts are
// too: a reused pid under the same name is another process (design/panel-grammar-slice2.md section 1). The name
// stays beside pid and start until the ledger has a row on pid reuse. A start of 0 on either side leaves pid
// and name to decide.
bool same_row(const row_key& r, const row_key& k) { return r.pid == k.pid && r.name == k.name && (r.start == 0 || k.start == 0 || r.start == k.start); }

// Where a list's selection is: its key's row, or the first row when the key is unset or has left the list.
size_t selected_row(const std::vector<row_key>& rows, const row_key& k) {
  const auto it = std::find_if(rows.begin(), rows.end(), [&](const row_key& r) { return k.set && same_row(r, k); });
  return it == rows.end() ? 0 : static_cast<size_t>(it - rows.begin());
}

// One list's place on panel 3: its selected row in the order drawn, whether it has focus, and its rows.
struct list_view {
  size_t selected = 0;
  bool focused = false;
  int rows = 0;
};

// machtop's scroll: the first row drawn is 0 while the selection is among the first n, else selection - n + 1,
// so past the bottom the selection stays on the last row shown. Worked out each frame; no offset is stored.
size_t first_drawn(size_t selected, int n) { return n <= 0 || selected < static_cast<size_t>(n) ? 0 : selected - static_cast<size_t>(n) + 1; }

// The ▶ lead every row has, and the selection's background on the focused list's selected row.
line list_row(row r, bool marked) {
  line l = r;
  if (!marked) return l;
  for (span& p : l) p.bg256 = kSelectBg;
  return l;
}

void list_lead(row& r, bool marked) {
  if (marked) r.add("▶ ", tone::title, true);
  else r.add("  ");
}

tone list_head_tone(bool focused) { return focused ? tone::title : tone::dim; }

std::vector<line> cpu_windows_list(const snapshot& s, int col, const list_view& v) {
  const std::vector<const process_row*> list = windows_cpu_order(s);
  row head;
  head.add("Windows", list_head_tone(v.focused), v.focused);
  if (list.empty()) return {head, message("", "unavailable", tone::watch, 0)};
  head.add("  by cores", tone::dim);

  const int name_w = std::clamp(col - 35, 4, 40);
  row cols;
  cols.add("  ").add(pad_left("cores", 6), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("thr", 4), tone::dim).add(pad_left("WS GiB", 7), tone::dim);
  std::vector<line> out = {head, cols};

  const size_t first = first_drawn(v.selected, v.rows), end = std::min(list.size(), first + static_cast<size_t>(std::max(0, v.rows)));
  for (size_t i = first; i < end; ++i) {
    const process_row& p = *list[i];
    const bool is_vm = p.pid == s.vm.vmmem_pid || p.pid == s.vm.vmwp_pid, marked = v.focused && i == v.selected;
    row r;
    list_lead(r, marked);
    r.add(pad_left(cores_text(windows_cores_known(s, p), p.cores), 6)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), is_vm ? tone::title : tone::text).add(pad_left(fmt("%u", p.threads), 4), tone::dim);
    r.add(pad_left(fmt("%.1f", gib(p.working_set)), 7));
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(list_row(r, marked));
  }
  out.push_back({{fmt("%d of %d", static_cast<int>(end - first), static_cast<int>(list.size())), tone::dim}});
  return out;
}

// stat rss: /proc/pid/stat field 24 times the page size, which reads below VmRSS (design/panel-grammar.md
// part 1, experiment 06), so it is named for what it is.
std::vector<line> cpu_wsl_list(const wsl_view& w, int col, const list_view& v) {
  row head;
  head.add("WSL", list_head_tone(v.focused), v.focused);
  if (!w.ok) return {head, message("", "unavailable", tone::watch, 0)};
  const std::vector<const wsl_process*> list = wsl_cpu_order(w);
  // The "holder" column is /dev/dxg open, not GPU use, and this head is where the list says so.
  static constexpr std::string_view kBy = "  by cores", kCount = ", guest's count", kNoUsage = " · dxg: no usage measured";
  if (head.cols() + static_cast<int>(kBy.size() + kNoUsage.size()) <= col) head.add(kBy, tone::dim);
  if (head.cols() + display_width(kCount) + display_width(kNoUsage) <= col) head.add(kCount, tone::dim);
  head.add(kNoUsage, tone::dim);

  const int name_w = std::clamp(col - 44, 4, 40);
  row cols;
  cols.add("  ").add(pad_left("cores", 6), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("thr", 4), tone::dim).add(pad_left("stat rss GiB", 13), tone::dim).add(" st dxg", tone::dim);
  std::vector<line> out = {head, cols};

  const size_t first = first_drawn(v.selected, v.rows), end = std::min(list.size(), first + static_cast<size_t>(std::max(0, v.rows)));
  for (size_t i = first; i < end; ++i) {
    const wsl_process& p = *list[i];
    const bool marked = v.focused && i == v.selected;
    row r;
    list_lead(r, marked);
    r.add(pad_left(cores_text(p.rated, p.cores), 6)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text);
    r.add(pad_left(p.threads ? fmt("%u", *p.threads) : "n/a", 4), tone::dim).add(pad_left(fmt("%.1f", gib(p.rss)), 13));
    r.add("  ").add(pad(p.state, 1), p.state == "D" ? tone::watch : tone::dim);
    if (p.on_gpu) r.add(" holder");
    out.push_back(list_row(r, marked));
  }
  out.push_back({{fmt("%d of %d · guest count? %lld", static_cast<int>(end - first), static_cast<int>(list.size()), static_cast<long long>(w.process_count)), tone::dim}});
  return out;
}

// Where panel 3's selection is this frame: the lists that can be selected, the focused one, each list's row.
struct selection_view {
  std::vector<std::vector<row_key>> lists;
  int focus = 0;
  size_t row[2] = {0, 0};
};

selection_view selection_of(const snapshot& s, const view_state& v, int panel, const history* h, int64_t now_ms, int width = 0) {
  selection_view sv;
  sv.lists = selectable_lists(s, panel, h, now_ms, kSpansMs[std::clamp(v.range, 0, 2)], width);
  const view_state::panel_view& pv = v.panels[panel];
  sv.focus = focused_list(pv.focus, sv.lists);
  for (size_t i = 0; i < 2; ++i) sv.row[i] = selected_row(sv.lists[i], pv.rows[i]);
  return sv;
}

list_view list_for(const selection_view& sv, int list, int rows) {
  const bool selectable = !sv.lists[static_cast<size_t>(list)].empty();
  return {selectable ? sv.row[list] : 0, selectable && sv.focus == list, rows};
}

// The process the one history shows: the focused list's selected row, or none when no list has a row.
bool selected_process(const selection_view& sv, proc_id& id) {
  const std::vector<row_key>& rows = sv.lists[static_cast<size_t>(sv.focus)];
  if (rows.empty()) return false;
  const row_key& k = rows[sv.row[sv.focus]];  // A row of this sample's list, so its start is this sample's
  id = {static_cast<uint8_t>(sv.focus), k.pid, k.start, k.name};
  return true;
}

constexpr double kCoresFloor = 0.05;              // machtop's per-app CPU: the least a process's cores graph spans
constexpr double kProcessFloorGiB = 4.0 / 1024;   // machtop's per-app memory

// One part of a history heading's right side and what follows it: two spaces between figures, ` · ` between
// layers, three before the scale and the range.
struct heading_part {
  std::string text;
  std::string after = "  ";
  tone color = tone::dim;
};

// A history's heading (design/panel-grammar-slice2.md sections 3 and 6, machtop's form): the left side, already in
// title, names the list, the process and the measure; the right side is the first of the candidates that fits, then
// `last N min`, which stays while the left side leaves room for it and is never cut. bare, if given, is tried last,
// alone and without `last N min`: what must survive when nothing else fits.
void history_heading(row& title, const std::vector<std::vector<heading_part>>& candidates, int64_t span_ms, int width, const std::string& bare = "") {
  const std::string last = fmt("last %d min", static_cast<int>(span_ms / 60000));
  std::vector<std::vector<heading_part>> tries = candidates;
  if (bare.empty()) tries.push_back({});  // `last N min` alone, after bare when there is one
  for (const std::vector<heading_part>& parts : tries) {
    int need = display_width(last);
    for (size_t i = 0; i < parts.size(); ++i) need += display_width(parts[i].text) + (i + 1 < parts.size() ? display_width(parts[i].after) : 3);
    if (title.cols() + 3 + need > width) continue;
    title.to(width - need);
    for (size_t i = 0; i < parts.size(); ++i) title.add(parts[i].text, parts[i].color).add(i + 1 < parts.size() ? parts[i].after : "   ", tone::dim);
    title.add(last, tone::dim);
    return;
  }
  if (bare.empty()) return;
  if (title.cols() + 3 + display_width(bare) <= width) title.to(width - display_width(bare)).add(bare, tone::dim);
  else history_heading(title, {}, span_ms, width);
}

// Short of room the figures go from the left.
std::vector<std::vector<heading_part>> from_left(const std::vector<heading_part>& parts) {
  std::vector<std::vector<heading_part>> out;
  for (size_t from = 0; from < parts.size(); ++from) out.emplace_back(parts.begin() + static_cast<std::ptrdiff_t>(from), parts.end());
  return out;
}

// What a history is drawn to and printed in: 0 to top, printed times per in unit.
struct history_scale {
  double top = 1, per = 1;
  const char* unit = "";
  const char* format = "%.2f";
};

history_scale cores_scale(double max) { return {scale_top(max, kCoresFloor), 1, "", "%.2f"}; }

// A process's memory: 0 to its maximum rounded up (scale_top), never under 4 MiB, in MiB while that top is under
// 1 GiB, else in GiB. Values are GiB.
history_scale memory_scale_of(double max_gib) {
  const double mib = scale_top(std::isfinite(max_gib) ? max_gib * 1024 : 0, kProcessFloorGiB * 1024);
  if (mib < 1024) return {mib / 1024, 1024, " MiB", "%.1f"};
  return {scale_top(max_gib, kProcessFloorGiB), 1, " GiB", "%.2f"};
}

std::string scaled(double v, const history_scale& sc) { return number(v * sc.per, sc.format); }

// Which measure a process history shows, and the Windows whole it is part of.
struct history_measure {
  const char* windows_name;  // After the pid in the title
  const char* wsl_name;
  float (history::*at)(int, size_t) const;
  float history_sample::*whole;  // Windows only
  history_scale (*scale)(double);
  const char* whole_name;  // In the heading
  const char* rest_name;   // In the legend
  const char* short_name;  // In the heading when narrow and no legend names the grey
};

const history_measure kCoresHistory = {" · cores", " · cores, guest's count", &history::cores_at, &history_sample::windows_cores_all, cores_scale,
                                       "all Windows processes", "all other Windows processes", "all Windows"};
const history_measure kMemoryHistory = {" · working set", " · stat rss", &history::memory_at, &history_sample::windows_ws_all, memory_scale_of,
                                        "Σ working sets, all Windows processes", "all other Windows processes' working sets", "Σ all Windows"};

// machtop's part_equals_whole: one series when, at every sample on the axis where both are known, the part is the
// whole within 1e-6 relative, or no sample has both (design/panel-grammar-slice2.md section 3, "When it draws as one
// series"). A second row appearing inside the window makes it two layers again.
template <class Part, class Whole> bool part_is_whole(const time_axis& a, Part part, Whole whole) {
  for (size_t i = 0; i < a.samples.size(); ++i) {
    if (a.cell[i] < 0) continue;
    const double p = part(*a.samples[i]), w = whole(*a.samples[i]);
    if (std::isfinite(p) && std::isfinite(w) && std::fabs(p - w) > 1e-6 * std::max(std::fabs(w), 1e-12)) return false;
  }
  return true;
}

// machtop's share_text: the part over the whole at the latest sample on the axis where both are known; blank when
// the whole there is 0 or there is none; `<1%` above 0 and under half a percent, and `0%` for a part of 0. A
// ratio of two held values for the selected row: display, not derivation (contracts/ui.md rule 9).
template <class Part, class Whole> std::string share_text(const time_axis& a, Part part, Whole whole) {
  for (size_t i = a.samples.size(); i-- > 0;) {
    if (a.cell[i] < 0) continue;
    const double p = part(*a.samples[i]), w = whole(*a.samples[i]);
    if (!std::isfinite(p) || !std::isfinite(w)) continue;
    if (!(w > 0)) return "";
    const double pct = 100 * p / w;
    if (!(pct >= 0)) return "";
    return pct > 0 && pct < 0.5 ? "<1%" : fmt("%ld%%", std::lround(pct));  // A part that measured 0 is 0%, as machtop
  }
  return "";
}

// machtop's draw_part_of_whole on columns, two to a braille cell: the whole in the track tone, the part in the bar
// tone, on one scale from 0. A braille cell takes one colour: the part's where either column has a part dot in that
// row, else the rest's. A whole that measured 0 is ⣀ in the bar tone; a part that measured 0 holds no cell. Each
// layer's maximum gets its own dot by its own samples, the part's compared with its own mean before it is held
// inside the whole. Gaps are blank; unwatched cells a faint · as plot_line draws them.
std::vector<line> plot_part_block(const std::vector<double>& whole, const std::vector<double>& part, const std::vector<double>& part_raw, const window_stats& whole_st,
                                  const window_stats& part_st, int rows, double top, int unwatched) {
  const auto high = [&](double v) { return std::max(1, static_cast<int>(std::lround(std::clamp(v, 0.0, top) / top * 4 * rows))); };
  const int whole_mark = max_mark(whole_st), part_mark = max_mark(part_st);
  const auto mark_height = [&](int mark, const std::vector<double>& cols, double max) {
    if (mark < 0 || mark >= static_cast<int>(cols.size()) || std::isnan(cols[static_cast<size_t>(mark)]) || !std::isfinite(max)) return 0;
    const int h = high(max);
    return h > high(cols[static_cast<size_t>(mark)]) ? h : 0;
  };
  const int whole_h = mark_height(whole_mark, whole, whole_st.max), part_h = mark_height(part_mark, part_raw, part_st.max);
  const int cells = static_cast<int>(whole.size()) / 2;
  std::vector<line> out;
  for (int y = 0; y < rows; ++y) {
    const int below = (rows - 1 - y) * 4;
    line l;
    const auto put = [&](const std::string& text, tone t) {
      if (!l.empty() && l.back().color == t) l.back().text += text;
      else l.push_back({text, t});
    };
    for (int c = 0; c < cells; ++c) {
      unsigned bits = 0;
      bool part_here = false, known = false, all_zero = true;
      for (int side = 0; side < 2; ++side) {
        const int at = 2 * c + side;
        const double w = whole[static_cast<size_t>(at)], pv = part[static_cast<size_t>(at)];
        if (std::isnan(w)) continue;
        known = true;
        all_zero = all_zero && w <= 0;
        const unsigned* dot = side == 0 ? kDotLeft : kDotRight;
        for (int k = 0; k < std::clamp(high(w) - below, 0, 4); ++k) bits |= dot[k];
        if (std::isfinite(pv) && pv > 0 && high(pv) > below) part_here = true;
        if (at == whole_mark && whole_h > below && whole_h <= below + 4) bits |= dot[whole_h - below - 1];
        if (at == part_mark && part_h > below && part_h <= below + 4) {
          bits |= dot[part_h - below - 1];
          part_here = true;
        }
      }
      if (bits) {
        std::string g;
        put_cp(g, static_cast<char32_t>(0x2800 + bits));
        put(g, part_here || (y == rows - 1 && known && all_zero) ? tone::bar : tone::track);
      } else if (y == rows - 1 && c < unwatched / 2) {
        put(c % 2 == 0 ? "·" : " ", tone::faint);
      } else {
        put(" ", tone::bar);
      }
    }
    out.push_back(l);
  }
  return out;
}

// One history, full width, under both lists: the focused list's selected process, its measure over the range, 0
// to the window's maximum rounded up. Never one per list: two would invite comparing rows that rule 3 keeps apart.
// A guest's figure is what the guest counted, not what the VM got. No gutter and no time axis: the figures and the
// range are in the heading. A Windows process is drawn as part of all Windows processes, the whole in grey under
// it, with a legend (design/panel-grammar-slice2.md section 3); the guest has no whole and is one series.
std::vector<line> process_history(const history& h, const proc_id* id, int width, int rows, int64_t end_ms, int64_t span_ms, const history_measure& m = kCoresHistory) {
  std::vector<line> out;
  row title;
  if (!id) {
    title.add("history", tone::dim).add("  nothing to select", tone::dim);
    out.push_back(title);
    out.resize(static_cast<size_t>(rows) + 1);
    return out;
  }
  title.add(id->side == 0 ? "Windows" : "WSL", tone::title, true).add(" · ", tone::dim).add(fit(id->name, 40)).add(fmt(" %u", id->pid), tone::dim);
  title.add(id->side == 0 ? m.windows_name : m.wsl_name, tone::dim);
  const int slot = h.slot_of(*id);
  const time_axis axis = make_axis(h, end_ms, span_ms, width, 2);
  const double kNone = std::numeric_limits<double>::quiet_NaN();
  const auto part = [&](const history_sample& p) { return slot < 0 ? kNone : static_cast<double>((h.*m.at)(slot, h.position(p))); };
  const auto whole = [&](const history_sample& p) { return id->side == 0 ? static_cast<double>(p.*m.whole) : kNone; };
  const int unwatched = unwatched_cells(h, axis);

  if (id->side != 0 || slot < 0 || part_is_whole(axis, part, whole)) {  // One series
    const window_stats st = bucket_stats(axis, part);
    const history_scale sc = m.scale(st.max);
    std::vector<heading_part> parts;
    if (!std::isnan(st.avg)) parts = {{"avg " + scaled(st.avg, sc)}, {"max " + scaled(st.max, sc)}, {"scale " + number(sc.top * sc.per, "%g") + sc.unit}};
    history_heading(title, from_left(parts), span_ms, width);
    out.push_back(title);
    const std::vector<line> plot = plot_block(bucket_cells(axis, part, sc.top), rows, "", "", unwatched, st, sc.top, false);
    out.insert(out.end(), plot.begin(), plot.end());
    return out;
  }

  // Part of the whole: the part only where both are known, held at or under the whole in each column.
  const auto both = [&](const history_sample& p) {
    const double a = part(p), b = whole(p);
    return std::isfinite(a) && std::isfinite(b) ? a : kNone;
  };
  const window_stats whole_st = bucket_stats(axis, whole), part_st = bucket_stats(axis, both);
  const history_scale sc = m.scale(whole_st.max);
  const std::vector<double> whole_cols = bucket_cells(axis, whole, sc.top), part_raw = bucket_cells(axis, both, sc.top);
  std::vector<double> part_cols(part_raw.size(), kNone);
  for (size_t i = 0; i < part_raw.size(); ++i) {
    if (!std::isnan(whole_cols[i]) && !std::isnan(part_raw[i])) part_cols[i] = std::min(part_raw[i], whole_cols[i]);
  }
  // Short of room: the process's figures go, then the whole's, then `now`; the whole's name goes last, since with
  // no legend it is what names the grey.
  const heading_part scale_part{"scale " + number(sc.top * sc.per, "%g") + sc.unit};
  const std::string share = share_text(axis, both, whole);
  const auto right = [&](bool process, bool whole_figures, bool now, bool whole_name) {
    std::vector<heading_part> p;
    if (process && !std::isnan(part_st.avg)) p.push_back({"process avg " + scaled(part_st.avg, sc) + "  max " + scaled(part_st.max, sc), " · "});
    if (whole_name) p.push_back({std::string(m.whole_name) + (whole_figures && !std::isnan(whole_st.avg) ? " avg " + scaled(whole_st.avg, sc) + "  max " + scaled(whole_st.max, sc) : ""), " · "});
    if (now && !share.empty()) p.push_back({"now " + share + " of all"});
    if (!p.empty()) p.back().after = "   ";
    p.push_back(scale_part);
    return p;
  };
  // design/panel-grammar-slice2.md Q6: the legend takes one of the history's rows while the plot keeps 2, and goes
  // only when it would not; the heading's whole then names the grey, so with no legend it outlasts the tail: the
  // name shortened first, keeping machtop's tail, then `scale`, then `last N min` go before it.
  const bool legend = rows >= 3;
  std::vector<std::vector<heading_part>> tries = {right(true, true, true, true), right(false, true, true, true), right(false, false, true, true), right(false, false, false, true)};
  if (legend) {
    tries.push_back({scale_part});
  } else {
    tries.push_back({{m.short_name, "   "}, scale_part});
    tries.push_back({{m.short_name}});
  }
  history_heading(title, tries, span_ms, width, legend ? "" : m.short_name);
  out.push_back(title);
  if (legend) {
    row l;
    l.add("█", tone::bar).add(" " + fit(id->name, 40) + "  ", tone::dim).add("█", tone::track).add(" ").add(m.rest_name, tone::dim);
    out.push_back(l);
  }
  const std::vector<line> plot = plot_part_block(whole_cols, part_cols, part_raw, whole_st, part_st, legend ? rows - 1 : rows, sc.top, unwatched);
  out.insert(out.end(), plot.begin(), plot.end());
  return out;
}

// ---- Panel 2, GPU: its lists and history

// ---- Panel 2's Windows list (design/panel-grammar-slice2.md section 5, Q4)

std::vector<line> gpu_users_list(const snapshot& s, const std::vector<gpu_row>& list, int col, const list_view& v) {
  row head;
  head.add("Windows", list_head_tone(v.focused), v.focused);
  if (list.empty()) return {head, message("", s.gpu_engines_known ? "no process with GPU use" : "unavailable: no GPU engine data", tone::watch, 0)};
  head.add("  by GPU %, busiest engine", tone::dim);
  std::vector<line> out = {head};

  const bool pids = col >= 38;
  const int name_w = std::clamp(col - (pids ? 29 : 21), 4, 40);
  row cols;
  cols.add("  ").add(pad_left("GPU%", 5), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("LOCAL", 6), tone::dim);
  out.push_back(cols);

  const size_t first = first_drawn(v.selected, v.rows), end = std::min(list.size(), first + static_cast<size_t>(std::max(0, v.rows)));
  for (size_t i = first; i < end; ++i) {
    const gpu_row& g = list[i];
    const bool is_vm = g.key.pid == s.vm.vmmem_pid || g.key.pid == s.vm.vmwp_pid, marked = v.focused && i == v.selected;
    row r;
    list_lead(r, marked);
    r.add(pad_left(std::isfinite(g.now) ? number(g.now, "%.0f") : "n/a", 5)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", g.key.pid), 7), tone::dim).add(" ");
    r.add(pad(g.key.name, name_w), is_vm ? tone::title : tone::text);
    r.add(pad_left(g.has_local ? fmt("%.1f", gib(g.local)) : "-", 6));
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(list_row(r, marked));
  }
  out.push_back({{fmt("%d of %d", static_cast<int>(end - first), static_cast<int>(list.size())), tone::dim}});
  return out;
}

// The warning goes with the list: a list with no warning reads as complete.
int denied_rows(const wsl_view& w) { return w.ok && w.dxg_denied > 0 ? 1 : 0; }

// Names only. /dev/dxg says who holds the GPU; nothing here says how much each one uses.
// rows is for names: the head, the columns and the dxg_denied warning come on top of it.
std::vector<line> dxg_holder_list(const wsl_view& w, int col, int rows, bool& cut) {
  std::vector<line> out;
  row head;
  head.add("WSL", tone::title, true).add(col >= 39 ? " /dev/dxg holders, no usage measured" : " holders, no usage measured", tone::dim);
  out.push_back(head);
  if (!w.ok) {
    out.push_back(message("", "unavailable: no WSL data", tone::watch, 0));
    return out;
  }
  if (w.dxg_pids.empty()) {
    out.push_back(message("", "no holder seen", tone::dim, 0));
  } else {
    const size_t shown = std::min(w.dxg_pids.size(), static_cast<size_t>(std::max(0, rows)));
    cut = cut || shown < w.dxg_pids.size();
    row cols;
    cols.add(pad_left("pid", 7), tone::dim).add(" ").add("name", tone::dim);
    cols.add(fmt("  top %d of %d", static_cast<int>(shown), static_cast<int>(w.dxg_pids.size())), tone::dim);
    out.push_back(cols);
    for (size_t i = 0; i < shown; ++i) {
      const uint32_t pid = w.dxg_pids[i];
      const auto it = std::find_if(w.processes.begin(), w.processes.end(), [&](const wsl_process& p) { return p.pid == pid; });
      row r;
      r.add(pad_left(fmt("%u", pid), 7), tone::dim).add(" ").add(it == w.processes.end() ? std::string_view("-") : std::string_view(it->name));
      out.push_back(r);
    }
  }
  if (denied_rows(w)) {
    out.push_back(message("", denied_note(w.dxg_denied, col), tone::watch, 0));
  }
  return out;
}

// avail covers everything drawn here. cut comes back true if the holder list left a row out; the Windows list
// scrolls and says N of M.
void gpu_lists(std::vector<line>& out, const snapshot& s, const wsl_view& w, const std::vector<gpu_row>& rows, const list_view& v, int width, int avail, bool& cut) {
  out.push_back(rule("PROCESSES", kTwoLists, width));
  list_view lv = v;
  if (width >= kSideBySideGpu) {
    const int col = (width - kListGap) / 2;
    lv.rows = avail - 4;  // The rule, its head, its columns and N of M
    side_by_side(out, gpu_users_list(s, rows, col, lv), dxg_holder_list(w, width - col - kListGap, avail - 3 - denied_rows(w), cut), col);
    return;
  }
  // Stacked: rule, two heads, two column rows, N of M, the warning. The rest is shared, and what the first
  // list leaves over goes to the second.
  const int total = std::max(0, avail - 6 - denied_rows(w));
  const int holders = w.ok ? static_cast<int>(std::min<size_t>(w.dxg_pids.size(), static_cast<size_t>(total))) : 0;
  lv.rows = total - std::min(total / 2, holders);
  const std::vector<line> top = gpu_users_list(s, rows, width, lv);
  const std::vector<line> bottom = dxg_holder_list(w, width, avail - 3 - denied_rows(w) - static_cast<int>(top.size()), cut);
  out.insert(out.end(), top.begin(), top.end());
  out.insert(out.end(), bottom.begin(), bottom.end());
}

// The selected Windows process's GPU %, busiest engine, the figure the list shows: one series always, since a
// process's busiest engine does not add across processes. 0 to 100; over 100 drawn at the ceiling and its max
// printed as measured, in watch (rule 5). No `*`: that points at the split's caption. A sample whose engines were
// not read is a gap; a process there with no engine row measured 0.
std::vector<line> gpu_history(const history& h, const proc_id* id, int width, int rows, int64_t end_ms, int64_t span_ms) {
  std::vector<line> out;
  row title;
  if (!id) {
    out.push_back(title.add("history", tone::dim).add("  nothing to select", tone::dim));
    out.resize(static_cast<size_t>(rows) + 1);
    return out;
  }
  title.add("Windows", tone::title, true).add(" · ", tone::dim).add(fit(id->name, 40)).add(fmt(" %u", id->pid), tone::dim).add(" · GPU %, busiest engine", tone::dim);
  const int slot = h.slot_of(*id);
  const time_axis axis = make_axis(h, end_ms, span_ms, width, 2);
  const auto value = [&](const history_sample& p) { return slot < 0 ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(h.gpu_at(slot, h.position(p))); };
  const window_stats st = bucket_stats(axis, value);
  std::vector<heading_part> parts;
  if (!std::isnan(st.avg)) parts = {{"avg " + number(st.avg, "%.0f") + "%"}, {"max " + number(st.max, "%.0f") + "%", "  ", st.max > 100 ? tone::watch : tone::dim}};
  history_heading(title, from_left(parts), span_ms, width);
  out.push_back(title);
  const std::vector<line> plot = plot_block(bucket_cells(axis, value), rows, "", "", unwatched_cells(h, axis), st, 100, false);
  out.insert(out.end(), plot.begin(), plot.end());
  return out;
}

panel_frame gpu_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms, int64_t span_ms, const view_state& v) {
  const gpu_device_view& g = s.gpu_device;
  const gpu_split_view& split = s.gpu_split;
  // The split is engine time grouped by side and can pass 100: docs/measurable.md, Experiment 01 result,
  // "Shares add to 100%". A window that did says so beside the caption that explains it.
  const int64_t end = graph_end(h, now_ms);
  // The caption's * goes with a label that carries one, and only when that label is drawn (part 5). Where the
  // starred caption costs the plot its rows, both go: the label still prints the number, in watch.
  const auto make_top = [&](bool star) {
    std::vector<panel> top = {
        pct_panel({"device · NVML"}, g.present && g.has_util, g.util_pct, &history_sample::gpu_util),
        pct_panel({"WSL VM ·", "engine counters,", star ? "VM level, not exact shares*" : "VM level, not exact shares"}, split.valid, split.vm_pct, &history_sample::vm_pct),
        pct_panel({"Windows · engine counters"}, split.valid, split.windows_pct, &history_sample::windows_pct)};
    top[1].star = top[2].star = star;  // The label's * and the caption's go together, or neither
    return top;
  };

  panel memory{{"GPU memory · NVML,", "used of total"}, "n/a", tone::dim, &history_sample::gpu_mem_pct, &history_sample::gpu_mem_gib, "%.1f", " GiB"};
  if (g.present && g.has_mem && g.mem_total > 0) {
    memory.value = fmt("%.1f/%.1f GiB", gib(g.mem_used), gib(g.mem_total));
    memory.color = tone::text;
  }
  panel power{{"power · NVML,", "of limit"}, "n/a", tone::dim, &history_sample::gpu_power_pct, &history_sample::gpu_power_w, "%.0f", " W"};
  if (g.present && g.has_power && std::isfinite(g.power_w)) {
    power.value = number(g.power_w, "%.0f");
    if (g.power_limit_w > 0) power.value += "/" + number(g.power_limit_w, "%.0f");
    else power.title.push_back("no limit reported, not graphed");
    power.value += " W";
    power.color = tone::text;
  }
  const std::vector<panel> second = {std::move(memory), std::move(power)};

  panel_frame f;
  f.lines.push_back(title_row(s, now_ms));
  int room = height - 1;
  std::vector<panel> top;
  std::vector<grid_row> rows;
  for (const bool star : {true, false}) {  // With the caption's *, unless no label with one is drawn
    top = make_top(star);
    rows.clear();
    add_panels(rows, top, width);
    add_panels(rows, second, width);
    f.hidden = fit_rows(rows, room);
    if (!star || star_drawn(rows, h, end, span_ms)) break;
  }
  f.graph = any_plot(rows);
  room -= rows_height(rows);

  // Lower blocks: as much of the lists as they can use, then the history, then the device line goes last of the
  // three. The history (a title and kHistoryRows) takes rows from the lists only while the Windows list keeps
  // kLeastListRows; the rule line under the title is kept for after them, as before.
  const bool device = room >= 1;
  if (device) --room;
  const std::vector<gpu_row> rows_drawn = gpu_rows(s, &h, axis_end(h, now_ms), span_ms, width);  // The history is width wide
  const selection_view sv = selection_of(s, v, 2, &h, now_ms, width);
  const bool side = width >= kSideBySideGpu;
  const int history_least = 1 + kHistoryRows + 1;  // Its title and rows, and the rule line after
  std::vector<line> lists;
  // At least one row of each list, N of M, and the warning.
  if (const int least = (side ? 5 : 8) + denied_rows(w); room >= least) {
    int list_room = std::min(room, kMaxGpuList);
    if (const int keep = (side ? 4 : 7) + kLeastListRows + denied_rows(w); room - list_room < history_least && room - history_least >= keep) list_room = room - history_least;
    bool cut = false;
    gpu_lists(lists, s, w, rows_drawn, list_for(sv, 0, 0), width, list_room, cut);
    room -= static_cast<int>(lists.size());
    f.hidden = f.hidden || cut;
    f.selectable = rows_drawn.empty() ? 0 : 1;
  } else {
    f.hidden = true;
  }
  if (!device) f.hidden = true;
  int history_rows = 0;
  if (!lists.empty() && room >= history_least) {
    history_rows = std::clamp((room - 2) / 2, kHistoryRows, kMostHistoryRows);
    room -= history_rows + 1;
  } else {
    f.hidden = true;
  }

  const bool head = room >= 1;
  if (head) {
    // docs/measurable.md: close under load, not at idle, and the cause is unresolved.
    f.lines.push_back(rule("GPU", width >= 100 ? "device and engine counters can differ, the difference is unexplained" : "device vs engine gap unexplained", width));
    --room;
  } else {
    f.hidden = true;
  }
  if (room > 0) grow_rows(rows, room);
  draw_rows(f.lines, rows, h, end, span_ms);
  if (device) f.lines.push_back(device_row(g, width));
  f.lines.insert(f.lines.end(), lists.begin(), lists.end());
  if (history_rows > 0) {
    proc_id id;
    const bool any = selected_process(sv, id);
    const std::vector<line> hist = gpu_history(h, any ? &id : nullptr, width, history_rows, axis_end(h, now_ms), span_ms);
    f.lines.insert(f.lines.end(), hist.begin(), hist.end());
  }
  return f;
}


// ---- The frame

// Thread counts follow the newest sample. With no live figure, the last one seen keeps the rows in place.
int threads_of(const std::vector<double>& live, const history& h, uint16_t history_sample::*count) {
  if (!live.empty()) return std::min(static_cast<int>(live.size()), kHistoryThreads);
  const sample_range all = h.samples();
  for (size_t i = all.size(); i-- > 0;) {
    if (all[i].*count > 0) return all[i].*count;
  }
  return 0;
}

float windows_core_at(const history_sample& p, int t) { return p.windows_core[t]; }
float wsl_core_at(const history_sample& p, int t) { return p.wsl_core[t]; }

// One column's heat block, rows long however many threads it has: fewer are blank below.
std::vector<line> heat_block(const std::vector<heat_row>& rows, const time_axis& a, thread_value value) {
  std::vector<line> out;
  for (const heat_row& hr : rows) {
    const std::vector<double> top = half_cells(a, hr.top, value), bottom = half_cells(a, hr.bottom, value);
    row r;
    gutter(r, hr.label);
    for (int c = 0; c < a.cells; ++c) heat_cell(r, top[static_cast<size_t>(c)], bottom[static_cast<size_t>(c)]);
    out.push_back(r);
  }
  return out;
}

// The rows of each block in this window, or false for the small layout.
bool plan_large_cpu(const snapshot& s, const wsl_view& w, const history& h, int width, int height, cpu_rows& plan, thread_order& left, thread_order& right) {
  if (width < kCpuWide) return false;
  left = windows_order(s.cpu_layout, threads_of(s.cpu.core_pct, h, &history_sample::windows_threads));
  static const std::vector<double> kNone;
  right = numbered_threads(threads_of(w.ok ? w.core_pct : kNone, h, &history_sample::wsl_threads), "v");
  return plan_cpu_rows(static_cast<int>(std::max(left.groups.size(), right.groups.size())), height, plan);
}


int selectable_drawn(const selection_view& sv) { return selectable_count(sv.lists); }

// The small layout, under 100 columns or too short for the large one: the two graphs, the core rows, then the
// two lists stacked and the one history under both. A list keeps 3 rows; the history goes before that.
panel_frame cpu_small_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms, int64_t span_ms, const view_state& v) {
  const bool stale = s.wsl_age_s >= kStaleS;
  panel guest = pct_panel({"WSL CPU ·", "guest view, inside the VM"}, w.ok, w.busy_pct, &history_sample::wsl_cpu);
  if (w.ok && stale) {
    guest.title.push_back(collector_status(s));
    guest.color = tone::watch;
  }
  const std::vector<panel> graphs = {
      pct_panel({"Windows CPU ·", "whole machine, includes the WSL VM"}, !s.cpu.core_pct.empty(), s.cpu.busy_pct, &history_sample::windows_cpu),
      std::move(guest)};

  panel_frame f;
  f.lines.push_back(title_row(s, now_ms));
  int room = height - 1;
  std::vector<grid_row> rows;
  add_panels(rows, graphs, width);
  f.hidden = fit_rows(rows, room);
  f.graph = any_plot(rows);
  room -= rows_height(rows);

  std::vector<line> cores;
  cores.push_back(windows_rule(s, width));
  core_rows(cores, width, s.cpu.core_pct);
  if (w.ok) {
    cores.push_back(wsl_rule(w, width));
    core_rows(cores, width, w.core_pct);
  } else {
    cores.push_back(rule("WSL", "", width));
    cores.push_back(message("", wsl_unavailable(s, w), tone::watch, 0));
  }
  const bool show_cores = room >= static_cast<int>(cores.size());
  if (show_cores) room -= static_cast<int>(cores.size());
  else f.hidden = true;

  const bool head = room >= 1;
  if (head) --room;
  const selection_view sv = selection_of(s, v, 3, &h, now_ms);
  const auto lists = [&](int n) {
    std::vector<line> out = cpu_windows_list(s, width, list_for(sv, 0, n));
    const std::vector<line> guest_list = cpu_wsl_list(w, width, list_for(sv, 1, n));
    out.insert(out.end(), guest_list.begin(), guest_list.end());
    return out;
  };
  int n = 0, history_rows = 0;
  if (show_cores && room >= static_cast<int>(lists(kLeastListRows).size())) {
    n = kLeastListRows;
    if (room >= static_cast<int>(lists(n).size()) + kHistoryRows + 1) history_rows = kHistoryRows;
    const auto used = [&](int k, int hr) { return static_cast<int>(lists(k).size()) + (hr > 0 ? hr + 1 : 0); };
    while (n < kGoodListRows && used(n + 1, history_rows) <= room && used(n + 1, history_rows) > used(n, history_rows)) ++n;
    while (history_rows > 0 && history_rows < kMostHistoryRows && used(n, history_rows + 1) <= room) ++history_rows;
    room -= used(n, history_rows);
  }
  if (n == 0 || history_rows == 0) f.hidden = true;
  if (head) f.lines.push_back(rule("CPU", w.ok ? "? not yet checked · two systems' own views of overlapping work, never added" : "two systems' own views of overlapping work, never added", width));
  else f.hidden = true;
  if (room > 0) grow_rows(rows, room);
  draw_rows(f.lines, rows, h, graph_end(h, now_ms), span_ms);
  if (show_cores) f.lines.insert(f.lines.end(), cores.begin(), cores.end());
  if (n > 0) {
    const std::vector<line> l = lists(n);
    f.lines.insert(f.lines.end(), l.begin(), l.end());
    f.selectable = selectable_drawn(sv);
  }
  if (history_rows > 0) {
    proc_id id;
    const bool any = selected_process(sv, id);
    const std::vector<line> hist = process_history(h, any ? &id : nullptr, width, history_rows, axis_end(h, now_ms), span_ms);
    f.lines.insert(f.lines.end(), hist.begin(), hist.end());
  }
  return f;
}

// height here leaves out the footer, as in the other panels.
panel_frame cpu_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms, int64_t span_ms, const view_state& v) {
  cpu_rows plan;
  thread_order left_order, right_order;
  if (!plan_large_cpu(s, w, h, width, height + 1, plan, left_order, right_order)) return cpu_small_frame(s, w, h, width, height, now_ms, span_ms, v);

  const int col = (width - kListGap) / 2, right_col = width - col - kListGap;
  const int64_t end = axis_end(h, now_ms);
  const time_axis axis = make_axis(h, end, span_ms, col - kCpuGutter);  // The smaller column: both plots are this wide
  const time_axis halves = make_axis(h, end, span_ms, col - kCpuGutter, 2);  // The busy plots': two columns in each of axis's cells
  const int64_t collected = h.samples().empty() ? 0 : end - h.samples().front().unix_ms;
  const auto of = [](float history_sample::*series) { return [series](const history_sample& p) { return static_cast<double>(p.*series); }; };
  const auto mean = [&](float history_sample::*series) { return bucket_cells(axis, of(series)); };
  const auto windows_cpu = [](const history_sample& p) { return p.windows_cpu; };

  std::vector<line> left = windows_heads(s, static_cast<int>(s.cpu.core_pct.size()), col);
  std::vector<line> right = wsl_heads(s, w, right_col, collected);
  const auto append = [](std::vector<line>& to, std::vector<line> from) { to.insert(to.end(), from.begin(), from.end()); };

  const window_stats left_busy = bucket_stats(halves, windows_cpu), right_busy = bucket_stats(halves, of(&history_sample::wsl_busy));
  const int unwatched = unwatched_cells(h, halves);  // One axis, so the same for both busy plots
  append(left, plot_block(bucket_cells(halves, windows_cpu), plan.busy, "100", "  0", unwatched, left_busy));
  append(right, plot_block(bucket_cells(halves, of(&history_sample::wsl_busy)), plan.busy, "100", "  0", unwatched, right_busy));
  left.push_back(lane("kern", mean(&history_sample::own_kernel)));
  left.push_back(lane("VM", mean(&history_sample::vm_cpu)));
  right.push_back(lane("iowt", mean(&history_sample::wsl_iowait)));
  right.push_back(lost_lane(mean(&history_sample::lost_vcpus)));

  bool left_highest = false, right_highest = false;
  const std::vector<heat_row> left_rows = heat_rows(left_order, plan.heat, left_highest), right_rows = heat_rows(right_order, plan.heat, right_highest);
  const bool guest_live = w.ok && s.wsl_age_s < kStaleS;
  left.push_back(heat_caption("threads", hot_count(s.cpu.core_pct, true), left_highest, true, col));
  right.push_back(heat_caption("vCPUs, not Windows cores", hot_count(w.core_pct, guest_live), right_highest, false, right_col));
  append(left, heat_block(left_rows, axis, windows_core_at));
  append(right, heat_block(right_rows, axis, wsl_core_at));
  left.resize(std::max(left.size(), right.size()));
  right.resize(left.size());
  left.push_back(axis_row(axis, span_ms, left_busy));
  right.push_back(axis_row(axis, span_ms, right_busy));

  const selection_view sv = selection_of(s, v, 3, &h, now_ms);
  append(left, cpu_windows_list(s, col, list_for(sv, 0, plan.lists)));
  append(right, cpu_wsl_list(w, right_col, list_for(sv, 1, plan.lists)));

  panel_frame f;
  f.graph = true;  // The busy plots have their rows at the layout's floor
  f.hidden = plan.history == 0;
  f.selectable = selectable_drawn(sv);
  f.lines.push_back(title_row(s, now_ms));
  f.lines.push_back(rule("CPU", w.ok ? "? not yet checked · two systems' own views of overlapping work, never added" : "two systems' own views of overlapping work, never added", width));
  side_by_side(f.lines, left, right, col);
  if (plan.history > 0) {
    proc_id id;
    const bool any = selected_process(sv, id);
    append(f.lines, process_history(h, any ? &id : nullptr, width, plan.history, end, span_ms));
  }
  f.lines.resize(static_cast<size_t>(height));  // The footer stays at the bottom however short the lists are
  return f;
}

// ---- Panel 4, memory ------------------------------------------------------------------------------
//
// design/panel-grammar.md part 4. Each side's own figures, in GiB, side by side and never added: Windows'
// used includes the VM, and whether the guest's cache sits in vmmemWSL's working set is unresolved (ledger,
// "Memory and process collectors checked"), so the VM has a row of its own.

constexpr double kMachineFloorGiB = 64.0 / 1024;  // A whole machine's figures move by tens of MiB

// A zoomed graph's bottom and top: machtop's sample_series (src/render.cpp at 246c2cb), the spread of the
// window's samples or the floor if more, from a tenth of it under the minimum to a fifth over the maximum.
struct zoom {
  double bottom = 0, top = 1;
};

zoom zoom_of(const window_stats& st, double floor) {
  if (std::isnan(st.max)) return {0, floor};
  const double spread = std::max(st.max - st.min, floor);
  zoom z;
  z.bottom = std::max(0.0, st.min - spread * 0.1);
  z.top = z.bottom + spread * 1.2;
  return z;
}

// The unit and decimals a zoomed range is printed in: enough that the range spans at least 4 units of the last
// digit, so its two ends say how wide it is and a line sits where they put it. GiB from 1 GiB up with 1 to 3
// decimals, else MiB with 0 to 3.
struct zoom_unit {
  double scale = 1;
  const char* name = " GiB";
  int decimals = 1;
};

constexpr double kLeastUnits = 4;

zoom_unit unit_of(const zoom& z) {
  const double spread = z.top - z.bottom;
  for (int d = 1; d <= 3 && z.top >= 1; ++d) {
    if (spread * std::pow(10.0, d) >= kLeastUnits) return {1, " GiB", d};
  }
  for (int d = 0; d <= 3; ++d) {
    if (spread * 1024 * std::pow(10.0, d) >= kLeastUnits) return {1024, " MiB", d};
  }
  return {1024, " MiB", 3};
}


std::string in_unit(double gib_value, const zoom_unit& u) { return std::isfinite(gib_value) ? fmt("%.*f", u.decimals, gib_value * u.scale) : "n/a"; }

// A range's ends, the bottom rounded down and the top up, so every sample drawn is inside what is printed.
std::string range_end(double gib_value, const zoom_unit& u, bool up) {
  const double step = std::pow(10.0, -u.decimals), v = gib_value * u.scale / step;
  return fmt("%.*f", u.decimals, (up ? std::ceil(v - 1e-9) : std::floor(v + 1e-9)) * step);
}

// "avg 24.8 max 24.8 · range 24.75–24.82 GiB", or the name alone with nothing measured in the window.
std::string zoom_label(const window_stats& st, const zoom& z, const char* sep) {
  if (std::isnan(st.avg)) return "";
  const zoom_unit u = unit_of(z);
  const zoom_unit one = {u.scale, u.name, std::max(1, u.decimals - 1)};
  return "avg " + in_unit(st.avg, one) + sep + "max " + in_unit(st.max, one) + (std::string(sep) == "  " ? "  " : " · ") + "range " + range_end(z.bottom, u, false) + "–" + range_end(z.top, u, true) + u.name;
}

// A zoomed plot over an axis: the cells are the samples' place between bottom and top, as a percent.
template <class Value> std::vector<line> zoom_plot(const time_axis& a, const history& h, Value gib_value, double floor, int rows, window_stats& raw, zoom& z) {
  raw = bucket_stats(a, gib_value);
  z = zoom_of(raw, floor);
  const zoom zz = z;
  const auto place = [&](const history_sample& p) { return (gib_value(p) - zz.bottom) / (zz.top - zz.bottom) * 100; };
  const window_stats placed = bucket_stats(a, place);
  return plot_block(bucket_cells(a, place), rows, "", "", unwatched_cells(h, a), placed);
}

std::string gib_of(uint64_t b) { return fmt("%.1f", gib(b)); }

std::vector<line> windows_memory_heads(const snapshot& s) {
  static constexpr int kIndent = 9;  // "WINDOWS  "
  row used;
  used.add("WINDOWS", tone::title, true).add("  ");
  if (s.memory.total == 0) {
    used.add("used n/a", tone::dim);
  } else {
    const tone t = left_tone(100.0 * static_cast<double>(s.memory.available) / static_cast<double>(s.memory.total));
    used.add("used " + gib_of(s.memory.total - std::min(s.memory.total, s.memory.available)) + " of " + gib_of(s.memory.total) + " GiB", t, t != tone::text);
    used.add(" · cache? " + gib_of(s.memory.cache), tone::text);
  }
  row source;
  source.to(kIndent).add("whole machine, includes the WSL VM", tone::dim);
  row commit;
  commit.to(kIndent);
  if (s.memory.commit_limit > 0) commit.add("commit " + gib_of(s.memory.commit_used) + " of " + gib_of(s.memory.commit_limit) + " GiB");
  else commit.add("commit n/a", tone::dim);
  return {used, source, commit};
}

std::vector<line> wsl_memory_heads(const snapshot& s, const wsl_view& w, int col) {
  static constexpr int kIndent = 5;  // "WSL  "
  row used;
  used.add("WSL", tone::title, true).add("  ");
  row source, other;
  source.to(kIndent).add("guest view, inside the VM", tone::dim);
  other.to(kIndent);
  if (!w.ok) {
    used.add(wsl_unavailable(s, w), tone::watch);
    if (s.wsl_restarts > 0) other.add(fmt("collector restarted %d times", s.wsl_restarts), tone::watch);
    return {used, source, other};
  }
  if (w.mem_total == 0) {
    used.add("used n/a", tone::dim);
  } else {
    const tone t = left_tone(100.0 * static_cast<double>(w.mem_available) / static_cast<double>(w.mem_total));
    used.add("used " + gib_of(w.mem_total - std::min(w.mem_total, w.mem_available)) + " of " + gib_of(w.mem_total) + " GiB", t, t != tone::text);
    used.add(" · cache " + gib_of(w.mem_cached));
  }
  const bool stale = s.wsl_age_s >= kStaleS;
  const std::string age = stale ? " · " + number(s.wsl_age_s, "%.0f") + " s old" : "";
  const int room = col - source.cols() - display_width(age) - 3;
  if (room >= kLeastDistro && !w.distro.empty()) source.add(" · " + fit(w.distro, room), tone::dim);
  source.add(age, tone::watch, true);
  other.add("swap? " + gib_of(w.swap_used) + " GiB · ");
  if (w.psi_memory) {
    const bool high = *w.psi_memory >= kPressureWatch;
    other.add(fmt("PSI mem %.2f%% avg10", *w.psi_memory), high ? tone::watch : tone::text, high);
  } else {
    other.add("PSI mem n/a", tone::dim);
  }
  return {used, source, other};
}

line vm_memory_row(const snapshot& s) {
  row r;
  r.add("VM", tone::title, true).add("  ");
  if (s.vm.vmmem_pid == 0) return r.add("vmmemWSL not found on Windows", tone::watch);
  r.add("vmmemWSL working set " + gib_of(s.vm.vmmem_working_set) + " GiB");
  r.add(" · Windows' view of the VM, included in WINDOWS", tone::dim);
  return r;
}

// One side's used-and-cache block: for each, a title with avg, max and range, and a 2-row zoomed plot.
std::vector<line> memory_plots(const time_axis& a, const history& h, const char* used_name, float history_sample::*used, const char* cache_name, float history_sample::*cache) {
  std::vector<line> out;
  for (const auto& [name, series] : {std::pair{used_name, used}, std::pair{cache_name, cache}}) {
    const auto value = [series = series](const history_sample& p) { return static_cast<double>(p.*series); };
    window_stats st;
    zoom z;
    const std::vector<line> plot = zoom_plot(a, h, value, kMachineFloorGiB, 2, st, z);
    row title;
    title.add(name);
    const std::string label = zoom_label(st, z, " ");
    if (!label.empty()) title.add(" " + label, tone::dim);
    out.push_back(title);
    out.insert(out.end(), plot.begin(), plot.end());
  }
  return out;
}

std::vector<line> windows_memory_list(const snapshot& s, int col, const list_view& v) {
  const std::vector<const process_row*> list = windows_mem_order(s);
  row head;
  head.add("Windows", list_head_tone(v.focused), v.focused);
  if (list.empty()) return {head, message("", "unavailable", tone::watch, 0)};
  head.add("  by working set", tone::dim);
  const int name_w = std::clamp(col - 32, 4, 40);
  row cols;
  cols.add("  ").add(pad_left("WS GiB", 8), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("cores", 6), tone::dim);
  std::vector<line> out = {head, cols};
  const size_t first = first_drawn(v.selected, v.rows), end = std::min(list.size(), first + static_cast<size_t>(std::max(0, v.rows)));
  for (size_t i = first; i < end; ++i) {
    const process_row& p = *list[i];
    const bool is_vm = p.pid == s.vm.vmmem_pid || p.pid == s.vm.vmwp_pid, marked = v.focused && i == v.selected;
    row r;
    list_lead(r, marked);
    r.add(pad_left(fmt("%.2f", gib(p.working_set)), 8)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), is_vm ? tone::title : tone::text).add(pad_left(cores_text(windows_cores_known(s, p), p.cores), 6), tone::dim);
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(list_row(r, marked));
  }
  out.push_back({{fmt("%d of %d", static_cast<int>(end - first), static_cast<int>(list.size())), tone::dim}});
  return out;
}

std::vector<line> wsl_memory_list(const wsl_view& w, int col, const list_view& v) {
  row head;
  head.add("WSL", list_head_tone(v.focused), v.focused);
  if (!w.ok) return {head, message("", "unavailable", tone::watch, 0)};
  head.add("  by stat rss", tone::dim);
  const std::vector<const wsl_process*> list = wsl_mem_order(w);
  const int name_w = std::clamp(col - 28, 4, 40);
  row cols;
  cols.add("  ").add(pad_left("stat rss GiB", 13), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add("  st", tone::dim);
  std::vector<line> out = {head, cols};
  const size_t first = first_drawn(v.selected, v.rows), end = std::min(list.size(), first + static_cast<size_t>(std::max(0, v.rows)));
  for (size_t i = first; i < end; ++i) {
    const wsl_process& p = *list[i];
    const bool marked = v.focused && i == v.selected;
    row r;
    list_lead(r, marked);
    r.add(pad_left(fmt("%.2f", gib(p.rss)), 13)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text).add("  ").add(pad(p.state, 1), p.state == "D" ? tone::watch : tone::dim);
    out.push_back(list_row(r, marked));
  }
  out.push_back({{fmt("%d of %d · guest count? %lld", static_cast<int>(end - first), static_cast<int>(list.size()), static_cast<long long>(w.process_count)), tone::dim}});
  return out;
}

// The one history of panel 4: the focused list's selected process, its working set or stat rss, from 0 (design/
// panel-grammar-slice2.md section 6, Q3: a history does not zoom; panel 4's used and cache plots do).
std::vector<line> memory_history(const history& h, const proc_id* id, int width, int rows, int64_t end_ms, int64_t span_ms) {
  return process_history(h, id, width, rows, end_ms, span_ms, kMemoryHistory);
}

// Rows, height including the footer (part 4, "Rows"): 17 fixed; the history clamp((height - 16) / 3, 2, 12)
// and the lists the rest, but a list keeps 3 rows: the history gives way first, then the plots.
struct memory_rows {
  int history = 0, lists = 0;
  bool plots = true;
};

memory_rows plan_memory_rows(int height) {
  memory_rows p;
  p.history = std::clamp((height - 16) / 3, 2, 12);
  p.lists = height - 17 - p.history;
  if (p.lists < kLeastListRows) {
    p.history = std::max(kHistoryRows, p.history - (kLeastListRows - p.lists));
    p.lists = height - 17 - p.history;
  }
  if (p.lists < kLeastListRows) {
    p.history = 0;
    p.lists = height - 16;
  }
  if (p.lists < kLeastListRows) {
    p.plots = false;
    p.lists += 6;
  }
  p.lists = std::max(0, p.lists);
  return p;
}

constexpr int kMemoryWide = 80;  // Narrower than this the two sides stack

panel_frame memory_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms, int64_t span_ms, const view_state& v) {
  const memory_rows plan = plan_memory_rows(height + 1);
  const bool wide = width >= kMemoryWide;
  const int col = wide ? (width - kListGap) / 2 : width, right_col = wide ? width - col - kListGap : width;
  const int64_t end = axis_end(h, now_ms);
  const time_axis axis = make_axis(h, end, span_ms, std::max(1, col - kCpuGutter), 2);
  const selection_view sv = selection_of(s, v, 4, &h, now_ms);

  panel_frame f;
  f.graph = plan.plots || plan.history > 0;
  f.hidden = !plan.plots || plan.history == 0 || plan.lists < kLeastListRows;
  f.lines.push_back(title_row(s, now_ms));
  f.lines.push_back(rule("MEMORY", "? not yet checked · two systems' own views of overlapping memory, never added", width));
  const auto pair = [&](const std::vector<line>& left, const std::vector<line>& right) {
    if (wide) {
      side_by_side(f.lines, left, right, col);
    } else {
      f.lines.insert(f.lines.end(), left.begin(), left.end());
      f.lines.insert(f.lines.end(), right.begin(), right.end());
    }
  };
  pair(windows_memory_heads(s), wsl_memory_heads(s, w, right_col));
  f.lines.push_back(vm_memory_row(s));
  if (plan.plots) pair(memory_plots(axis, h, "used", &history_sample::windows_used, "cache?", &history_sample::windows_cache),
                       memory_plots(axis, h, "used", &history_sample::wsl_used, "cache", &history_sample::wsl_cache));
  if (plan.lists > 0) {
    // Stacked, the two lists share the rows the plan gave one.
    const int each = wide ? plan.lists : std::max(1, (plan.lists - 3) / 2);
    pair(windows_memory_list(s, col, list_for(sv, 0, each)), wsl_memory_list(w, right_col, list_for(sv, 1, each)));
    f.selectable = selectable_drawn(sv);
  }
  if (plan.history > 0) {
    proc_id id;
    const bool any = selected_process(sv, id);
    const std::vector<line> hist = memory_history(h, any ? &id : nullptr, width, plan.history, end, span_ms);
    f.lines.insert(f.lines.end(), hist.begin(), hist.end());
  }
  if (static_cast<int>(f.lines.size()) > height) f.hidden = true;
  f.lines.resize(static_cast<size_t>(std::max(0, height)));  // The footer stays at the bottom
  return f;
}

// ---- Panels not built, and the footer --------------------------------------------------------------

// machtop's short labels, and winwsltop's 8.
constexpr const char* kPanelLabel[kPanels + 1] = {"", "overview", "gpu", "cpu", "mem", "disk", "net", "procs", "vm cost"};

bool panel_built(int p) { return p >= 1 && p <= 4; }
bool panel_has_range(int p) { return p >= 2 && p <= 4; }  // Panels whose graphs ←→ change

// docs/panels.md's question for each panel not built, and where its figures are meanwhile.
std::vector<line> unbuilt_frame(const snapshot& s, int p, int width, int64_t now_ms) {
  static constexpr const char* kTitle[kPanels + 1] = {"", "", "", "", "", "DISK", "NETWORK", "PROCESSES", "VM COST"};
  static constexpr const char* kText[kPanels + 1] = {
      "", "", "", "",
      "",
      "Which disk is busy, and is it the WSL disk? The disk rows are on panel 1 until this panel is built.",
      "Which interface is busy? The network rows are on panel 1 until this panel is built.",
      "Every process, every measure. The process lists are on panels 1 and 3 until this panel is built.",
      "What has each side cost the other, and when? The GPU split is on panel 2 and the VM's CPU on panel 3 until this panel is built."};
  std::vector<line> out = {title_row(s, now_ms), rule(kTitle[p], "not built yet", width), {}};
  for (const std::string& l : wrap({kText[p]}, std::max(1, width))) out.push_back({{l, tone::text}});
  return out;
}

std::string range_text(int range) { return fmt("%d min", static_cast<int>(kSpansMs[std::clamp(range, 0, 2)] / 60000)); }

// machtop's footer: the panels on the left, current lit, built dim, not built faint; on the right the keys
// that act on this panel. Short of room every panel but the current one is its number, then the keys go,
// then the other panels. Rows a panel had to leave out are said after the panels, or by a leading … .
line panel_footer(const view_state& v, int lists, int width, bool hidden, bool graph) {
  std::string hints;
  if (lists > 0) hints += "↑↓ select  ";  // Lists drawn that a key moves in: none on a panel too short for them
  if (lists > 1) hints += "tab list  ";
  if (panel_has_range(v.panel) && graph) hints += "←→ " + range_text(v.range) + "  ";
  hints += "q quit";
  static constexpr std::string_view kNote = "… some rows hidden";
  for (int form = 0; form < 4; ++form) {  // Names and keys, numbers and keys, numbers, the current panel
    const bool names = form == 0, keys = form < 2;
    row r;
    const bool marker = hidden && form > 0;  // The note did not fit: a mark the right edge cannot cut
    if (marker) r.add("… ", tone::watch);
    for (int i = 1; i <= kPanels; ++i) {
      const bool lit = i == v.panel;
      if (form == 3 && !lit) continue;
      const std::string item = names || lit ? fmt("%d %s", i, kPanelLabel[i]) : fmt("%d", i);
      if (i > 1 && form < 3) r.add(names ? "  " : " ");
      r.add(item, lit ? tone::title : panel_built(i) ? tone::dim : tone::faint, lit);
    }
    const int need = r.cols() + (keys ? 3 + display_width(hints) : 0) + (hidden && names ? 3 + display_width(kNote) : 0);
    if (need > width && form < 3) continue;
    if (hidden && names) r.add("   ").add(kNote, tone::watch);
    if (keys) r.to(width - display_width(hints)).add(hints, tone::dim);
    return r;
  }
  return {};
}


}  // namespace

namespace {

float to_float(double v) {  // Not every finite double fits a float, and a value that does not is not a height
  return std::isfinite(v) && std::fabs(v) < 1e30 ? static_cast<float>(v) : std::numeric_limits<float>::quiet_NaN();
}

// Fills one side's values: the sample's own, and NaN for every thread it did not read.
uint16_t put_cores(const std::vector<double>& from, float (&to)[kHistoryThreads]) {
  const size_t n = std::min<size_t>(from.size(), kHistoryThreads);
  for (size_t i = 0; i < n; ++i) to[i] = to_float(from[i]);
  std::fill(to + n, to + kHistoryThreads, std::numeric_limits<float>::quiet_NaN());
  return static_cast<uint16_t>(n);
}

}  // namespace

int history::slot_of(const proc_id& id) const {
  const auto it = index_.find(slot_key{id.side, id.pid, id.start});
  return it == index_.end() || slots_[static_cast<size_t>(it->second)].id.name != id.name ? -1 : it->second;
}

void history::let_go(size_t i) {
  slot& sl = slots_[i];
  if (!sl.used) return;
  sl.used = false;
  index_.erase(slot_key{sl.id.side, sl.id.pid, sl.id.start});
  free_.push_back(static_cast<int>(i));
}

// A kept row with no start known (0) is matched by side, pid and name.
static bool kept_id(const std::vector<proc_id>& keep, const proc_id& p) {
  return std::any_of(keep.begin(), keep.end(), [&](const proc_id& q) { return q.side == p.side && q.pid == p.pid && (q.start == p.start || q.start == 0) && q.name == p.name; });
}

// The process's slot, made if it has none: a slot let go, then a new one under the cap, then the one least
// recently seen that is neither kept nor in this sample (write_processes marks the sample first). Past all of
// those a new one: nothing is refused.
int history::take_slot(const proc_id& id, int64_t t, size_t cap, const std::vector<proc_id>& keep) {
  const slot_key k{id.side, id.pid, id.start};
  if (const auto it = index_.find(k); it != index_.end()) {
    slot& sl = slots_[static_cast<size_t>(it->second)];
    if (sl.id.name == id.name) {
      sl.seen_ms = t;
      return it->second;
    }
    let_go(static_cast<size_t>(it->second));  // The same pid and start under another name is another process: this one has gone
  }
  int pick = -1;
  while (pick < 0 && !free_.empty()) {
    pick = free_.back();
    free_.pop_back();
    if (slots_[static_cast<size_t>(pick)].used) pick = -1;  // Taken back since it was let go
  }
  if (pick < 0 && slots_.size() >= cap) {
    if (!victims_ready_) {  // One pass a push, not one a new process
      victims_.clear();
      for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].used && slots_[i].seen_ms < t && !kept_id(keep, slots_[i].id)) victims_.push_back(static_cast<int>(i));
      }
      std::sort(victims_.begin(), victims_.end(), [&](int a, int b) {
        const int64_t sa = slots_[static_cast<size_t>(a)].seen_ms, sb = slots_[static_cast<size_t>(b)].seen_ms;
        return sa != sb ? sa > sb : a > b;  // Least recently seen, then lowest index, at the back
      });
      victims_ready_ = true;
    }
    while (pick < 0 && !victims_.empty()) {
      const int v = victims_.back();
      victims_.pop_back();
      if (slots_[static_cast<size_t>(v)].used && slots_[static_cast<size_t>(v)].seen_ms < t) {
        let_go(static_cast<size_t>(v));
        free_.pop_back();
        pick = v;
      }
    }
  }
  if (pick < 0) {
    if (slots_.size() == slots_.capacity()) slots_.reserve(slots_.size() + 64);  // In chunks, not one a process
    slots_.emplace_back();
    slots_.back().cores.resize(kCapacity);
    slots_.back().memory.resize(kCapacity);
    pick = static_cast<int>(slots_.size() - 1);
  }
  slot& sl = slots_[static_cast<size_t>(pick)];
  sl.id = id;
  sl.used = true;
  sl.seen_ms = t;
  std::fill(sl.cores.begin(), sl.cores.end(), std::numeric_limits<float>::quiet_NaN());  // Nothing of the last occupant
  std::fill(sl.memory.begin(), sl.memory.end(), std::numeric_limits<float>::quiet_NaN());
  sl.gpu.clear();  // Made again on its own first use: nothing of the last occupant's GPU
  sl.gpu_seen_ms = 0;
  index_[k] = pick;
  return pick;
}

// Every slot gets a value at this position, NaN unless its process reported one, so a slot never shows what
// it held a ring ago. A first reading's cores are NaN, as the lists print n/a. Windows: every row but pid 0,
// cores and working set. The guest: every row of a usable line, cores and stat rss, carried once on a repeat.
// The whole sample is marked seen before any of it is placed, so a new process never takes the slot of one
// that is in this same sample.
void history::write_processes(const snapshot& s, const wsl_view& w, size_t position, bool guest, const std::vector<proc_id>& keep) {
  const float gap = std::numeric_limits<float>::quiet_NaN();
  for (slot& sl : slots_) {
    if (!sl.used) continue;
    sl.cores[position] = sl.memory[position] = gap;
    if (!sl.gpu.empty()) sl.gpu[position] = gap;
  }
  const int64_t t = s.unix_ms;
  // Marked first, and each row's slot remembered, so placing looks nothing up twice.
  found_.clear();
  const auto mark = [&](uint8_t side, uint32_t pid, uint64_t start, const std::string& name) {
    const auto it = index_.find(slot_key{side, pid, start});
    const bool same = it != index_.end() && slots_[static_cast<size_t>(it->second)].id.name == name;
    if (same) slots_[static_cast<size_t>(it->second)].seen_ms = t;
    found_.push_back(same ? it->second : -1);
  };
  for (const process_row& p : s.processes) {
    if (p.pid != 0) mark(0, p.pid, p.create_time, p.name);
  }
  if (guest) {
    for (const wsl_process& p : w.processes) mark(1, p.pid, p.start.value_or(0), p.name);
  }
  victims_ready_ = false;
  const size_t rows = s.processes.size() + (guest ? w.processes.size() : 0);
  const size_t cap = std::max(kLeastSlots, 2 * rows);
  size_t n = 0;
  // Panel 2's GPU %, busiest engine, Windows only: 0 for a process with no engine row while the engines read.
  static const std::map<uint32_t, double> kNoEngines;
  const std::map<uint32_t, double> engines = s.gpu_engines_known ? busiest_engine(s) : kNoEngines;
  for (const process_row& p : s.processes) {
    if (p.pid == 0) continue;  // The idle process: its cores are idle time
    const int known = found_[n++];
    slot& sl = slots_[static_cast<size_t>(known >= 0 ? known : take_slot({0, p.pid, p.create_time, p.name}, t, cap, keep))];
    sl.cores[position] = windows_cores_known(s, p) ? to_float(p.cores) : gap;
    sl.memory[position] = to_float(gib(p.working_set));
    const auto e = engines.find(p.pid);
    const float g = !s.gpu_engines_known ? gap : e == engines.end() ? 0.0f : to_float(e->second);
    if (g > 0 && sl.gpu.empty()) {  // First use: what came before is 0 where the process was there and the engines read
      sl.gpu.assign(kCapacity, gap);
      for (size_t i = 0; i < count_; ++i) {
        const size_t at_i = (first_ + i) % kCapacity;
        if (!std::isnan(sl.memory[at_i]) && ring_[at_i].gpu_engines_known) sl.gpu[at_i] = 0;
      }
    }
    if (!sl.gpu.empty()) sl.gpu[position] = g;
    if (g > 0) sl.gpu_seen_ms = t;
  }
  if (guest) {
    for (const wsl_process& p : w.processes) {
      const int known = found_[n++];
      slot& sl = slots_[static_cast<size_t>(known >= 0 ? known : take_slot({1, p.pid, p.start.value_or(0), p.name}, t, cap, keep))];
      sl.cores[position] = p.rated ? to_float(p.cores) : gap;
      sl.memory[position] = to_float(gib(p.rss));
    }
  }
  for (size_t i = 0; i < slots_.size(); ++i) {  // Gone for all that is kept: let go
    const slot& sl = slots_[i];
    if (sl.used && t - sl.seen_ms >= kKeepMs && !kept_id(keep, sl.id)) let_go(i);
  }
}

void history::push(const snapshot& s, const std::vector<proc_id>& keep) {
  const int64_t t = s.unix_ms;
  const bool was_watching = watching_;
  watching_ = true;  // Even a sample that cannot be placed: the program is watching
  if (t <= 0 || t > kLatestMs) return;  // A sample that cannot be placed in time is a gap

  // Watching began here, or earlier at a time that could not be placed: then what is kept before t is a gap.
  if (count_ == 0) watched_from_ms_ = was_watching ? t - kKeepMs : t;

  // A stall as long as what is kept, whichever way the wall clock moved meanwhile: interval_s comes from
  // QPC, so the stored samples are off every graph, must not join the new one as if continuous, and every
  // span before t is watched time with nothing measured.
  // Otherwise, when the wall clock moved less than interval_s says, it stepped back, wholly or during a
  // stall: the old samples keep their spacing and move with the clock, so nothing is lost, nothing lands
  // ahead of "now", and a stall under what is kept still reads as one on a shorter span. When watching
  // began moves with them.
  if (count_ > 0 && s.interval_s >= static_cast<double>(kKeepMs) / 1000.0) {
    count_ = 0;
    watched_from_ms_ = std::min(watched_from_ms_, t - kKeepMs);
    // Whichever way the clock moved, every process was last seen, and last used the GPU, before what is kept: never
    // after t, or a slot's times would lie ahead and its process come back into a range later (audit finding 6).
    for (slot& sl : slots_) {
      sl.seen_ms = std::min(sl.seen_ms, t - kKeepMs);
      sl.gpu_seen_ms = 0;
    }
  } else if (count_ > 0) {
    const int64_t last = at(count_ - 1).unix_ms;
    const double gap_s = s.interval_s > 0 ? s.interval_s : 1.0;
    const int64_t shift = last + static_cast<int64_t>(gap_s * 1000.0) - t;
    if (t <= last || (s.interval_s > 0 && shift > kStepMs)) {
      for (size_t i = 0; i < count_; ++i) at(i).unix_ms -= shift;
      watched_from_ms_ -= shift;
      for (slot& sl : slots_) {  // When a process was last seen, and last used the GPU, move with its samples
        sl.seen_ms -= shift;
        if (sl.gpu_seen_ms != 0) sl.gpu_seen_ms -= shift;
      }
    }
  }

  if (ring_.empty()) ring_.resize(kCapacity);
  if (count_ == kCapacity) {
    first_ = (first_ + 1) % kCapacity;
    --count_;
  }

  const float gap = std::numeric_limits<float>::quiet_NaN();
  const double missing = std::numeric_limits<double>::quiet_NaN();
  const gpu_device_view& g = s.gpu_device;
  ++count_;
  history_sample& n = at(count_ - 1);  // Written in place: the slot holds an old sample's values until here
  n.unix_ms = t;
  n.gpu_util = g.present && g.has_util ? g.util_pct : missing;
  n.gpu_mem_pct = g.present && g.has_mem && g.mem_total > 0 ? 100.0 * static_cast<double>(g.mem_used) / static_cast<double>(g.mem_total) : missing;
  n.gpu_power_pct = g.present && g.has_power && g.power_limit_w > 0 ? 100.0 * g.power_w / g.power_limit_w : missing;
  n.gpu_mem_gib = std::isnan(n.gpu_mem_pct) ? missing : gib(g.mem_used);
  n.gpu_power_w = std::isnan(n.gpu_power_pct) ? missing : g.power_w;
  n.vm_pct = s.gpu_split.valid ? s.gpu_split.vm_pct : missing;
  n.windows_pct = s.gpu_split.valid ? s.gpu_split.windows_pct : missing;
  n.windows_cpu = s.cpu.core_pct.empty() ? missing : s.cpu.busy_pct;  // A failed read leaves no cores
  // The same line twice measured nothing new: a silent collector is a gap at once, not a flat line
  // until the line turns stale.
  const wsl_view w = read_wsl(s.wsl_json);
  const bool repeated = wsl_line_repeats(s.wsl_json, last_wsl_);
  repeats_ = repeated ? repeats_ + 1 : 0;
  n.wsl_cpu = w.ok && !repeated && s.wsl_age_s < kStaleS ? w.busy_pct : missing;

  // The large layout of panel 3. Reading a repeated line again is what carries its value.
  const bool guest = w.ok && s.wsl_age_s < kStaleS && repeats_ <= 1;
  n.own_kernel = s.vm.vmmem_cores_known ? to_float(s.cpu_derived.own_kernel_pct) : gap;
  n.vm_cpu = s.vm.vmmem_cores_known ? to_float(s.cpu_derived.vm_cpu_pct) : gap;
  n.wsl_busy = guest ? to_float(w.busy_pct) : gap;
  n.wsl_iowait = guest && w.cpu_iowait ? to_float(*w.cpu_iowait) : gap;
  n.lost_vcpus = s.lost.state == lost_state::known ? to_float(s.lost.vcpus) : gap;
  n.windows_used = s.memory.total > 0 ? to_float(gib(s.memory.total - std::min(s.memory.total, s.memory.available))) : gap;
  n.windows_cache = s.memory.total > 0 ? to_float(gib(s.memory.cache)) : gap;
  n.vm_working_set = s.vm.vmmem_pid != 0 ? to_float(gib(s.vm.vmmem_working_set)) : gap;
  n.wsl_used = guest && w.mem_total > 0 ? to_float(gib(w.mem_total - std::min(w.mem_total, w.mem_available))) : gap;
  n.wsl_cache = guest && w.mem_total > 0 ? to_float(gib(w.mem_cached)) : gap;
  n.windows_cores_all = s.process_totals.known ? to_float(s.process_totals.cores) : gap;
  n.windows_ws_all = s.process_totals.known ? to_float(gib(s.process_totals.working_set)) : gap;
  n.gpu_engines_known = s.gpu_engines_known;
  n.windows_threads = put_cores(s.cpu.core_pct, n.windows_core);
  static const std::vector<double> kNone;
  n.wsl_threads = put_cores(guest ? w.core_pct : kNone, n.wsl_core);
  write_processes(s, w, position(n), guest, keep);

  // One sample older than what is kept stays, so the left edge is drawn from where that sample ended.
  while (count_ > 1 && at(1).unix_ms < t - kKeepMs) {
    first_ = (first_ + 1) % kCapacity;
    --count_;
  }
}

int display_width(std::string_view s) {
  int w = 0;
  for (size_t i = 0; i < s.size();) w += cp_width(next_cp(s, i));
  return w;
}

namespace {

// One panel above the footer, body rows high.
panel_frame panel_body(const snapshot& s, const wsl_view& w, const history& h, int panel, int width, int body, int64_t now_ms, int64_t span_ms, const view_state& v) {
  panel_frame f;
  if (panel == 1) f.lines = overall_frame(s, w, width, body, now_ms);
  else if (panel == 2) f = gpu_frame(s, w, h, width, body, now_ms, span_ms, v);
  else if (panel == 3) f = cpu_frame(s, w, h, width, body, now_ms, span_ms, v);
  else if (panel == 4) f = memory_frame(s, w, h, width, body, now_ms, span_ms, v);
  else f.lines = unbuilt_frame(s, panel, width, now_ms);
  if (panel > 3) f.lines.resize(static_cast<size_t>(std::max(0, body)));  // The footer stays on the bottom row
  return f;
}

std::vector<line> with_footer(const panel_frame& f, int width, int body, const line& foot) {
  std::vector<line> out;
  for (size_t i = 0; i < f.lines.size() && i < static_cast<size_t>(body); ++i) out.push_back(clip(f.lines[i], width));
  out.push_back(clip(foot, width));
  return out;
}

}  // namespace

// The rows remembered, not the rows they resolve to this frame: a selected process that has left the list is
// what needs keeping. A list never moved in shows its first row, which is in the sample and so never taken.
std::vector<proc_id> selected_processes(const snapshot&, const view_state& v) {
  std::vector<proc_id> out;
  for (const int panel : {2, 3, 4}) {
    for (int list = 0; list < (panel == 2 ? 1 : 2); ++list) {
      const row_key& k = v.panels[panel].rows[list];
      if (k.set) out.push_back({static_cast<uint8_t>(list), k.pid, k.start, k.name});
    }
  }
  return out;
}

namespace {

bool apply_key_to(view_state& v, key_event e, const snapshot& s, const history* h, int64_t now_ms, int width) {
  if (e.esc_first) {
    apply_key_to(v, key_event{key::esc}, s, h, now_ms, width);
    e.esc_first = false;
  }
  v.panel = std::clamp(v.panel, 1, kPanels);
  v.range = std::clamp(v.range, 0, 2);
  view_state::panel_view& pv = v.panels[v.panel];
  switch (e.k) {
    case key::quit: return false;
    case key::panel:
      if (e.panel >= 1 && e.panel <= kPanels) v.panel = e.panel;
      break;
    case key::esc: v.panel = 1; break;  // Every panel keeps its focus and selections, and the range stays
    case key::left:
      if (panel_has_range(v.panel)) v.range = std::min(v.range + 1, 2);  // Further back
      break;
    case key::right:
      if (panel_has_range(v.panel)) v.range = std::max(v.range - 1, 0);
      break;
    case key::tab: {
      const std::vector<std::vector<row_key>> lists = selectable_lists(s, v.panel, h, now_ms, kSpansMs[v.range], width);
      pv.focus = focused_list(pv.focus, lists);  // An empty list never keeps focus
      if (selectable_count(lists) > 1) pv.focus = (pv.focus + 1) % static_cast<int>(lists.size());
      break;
    }
    case key::up:
    case key::down: {
      const std::vector<std::vector<row_key>> lists = selectable_lists(s, v.panel, h, now_ms, kSpansMs[v.range], width);
      const int focus = focused_list(pv.focus, lists);
      if (static_cast<size_t>(focus) >= lists.size() || lists[static_cast<size_t>(focus)].empty()) break;
      pv.focus = focus;
      const std::vector<row_key>& rows = lists[static_cast<size_t>(focus)];
      const size_t at = selected_row(rows, pv.rows[focus]);
      const size_t to = e.k == key::down ? std::min(at + 1, rows.size() - 1) : at == 0 ? 0 : at - 1;  // Stops at the ends
      pv.rows[focus] = rows[to];
      break;
    }
    case key::sort:  // Panel 7's, which is not built
    case key::none: break;
  }
  return true;
}

}  // namespace

bool apply_key(view_state& v, key_event e, const snapshot& s, const history& h, int64_t now_ms, int width) { return apply_key_to(v, e, s, &h, now_ms, width); }

std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms, const history* hist, const view_state& v) {
  if (width <= 0 || height <= 0) return {};
  const wsl_view w = read_wsl(s.wsl_json);
  if (!hist) return overall_frame(s, w, width, height, now_ms);
  const int panel = std::clamp(v.panel, 1, kPanels), body = height - 1;  // The footer is the last row, whatever else has to give
  const panel_frame f = panel_body(s, w, *hist, panel, width, body, now_ms, kSpansMs[std::clamp(v.range, 0, 2)], v);
  view_state shown = v;
  shown.panel = panel;
  return with_footer(f, width, body, panel_footer(shown, f.selectable, width, f.hidden, f.graph));
}

std::string as_text(const std::vector<line>& lines) {
  std::string out;
  for (const line& l : lines) {
    for (const span& p : l) out += p.text;
    out += '\n';
  }
  return out;
}

namespace {

constexpr const char* kToneColor[] = {
    "200;200;200", "236;236;236", "122;122;122", "60;60;60",  // text, title, dim, faint
    "166;196;218", "58;58;58",    "212;184;96",  "224;96;96"};  // bar, track, watch, act

void put_foreground(std::string& out, const span& p) {
  if (p.fg256 >= 0) {
    out += "38;5;" + std::to_string(p.fg256);
  } else {
    out += "38;2;";
    out += kToneColor[static_cast<size_t>(p.color)];
  }
}

void put_background(std::string& out, int bg256) {
  out += bg256 >= 0 ? "48;5;" + std::to_string(bg256) : bg256 == kSelectBg ? std::string("48;2;44;62;76") : std::string("49");
}

bool same_foreground(const span& a, const span& b) { return a.fg256 == b.fg256 && (a.fg256 >= 0 || a.color == b.color); }

// Takes the terminal from what pen says it is to what p needs: nothing if they look the same, only the
// difference if it can, and a reset where bold changes, which has no short way back.
void put_pen(std::string& out, span& pen, bool started, const span& p) {
  if (!started || pen.bold != p.bold) {
    out += p.bold ? "\x1b[0;1;" : "\x1b[0;";
    put_foreground(out, p);
    if (p.bg256 != -1) {
      out += ';';
      put_background(out, p.bg256);
    }
    out += 'm';
  } else if (const bool fg = !same_foreground(pen, p), bg = pen.bg256 != p.bg256; fg || bg) {
    out += "\x1b[";
    if (fg) put_foreground(out, p);
    if (fg && bg) out += ';';
    if (bg) put_background(out, p.bg256);
    out += 'm';
  }
  pen.color = p.color;
  pen.bold = p.bold;
  pen.fg256 = p.fg256;
  pen.bg256 = p.bg256;
}

}  // namespace

std::string vt_frame(const std::vector<line>& lines, int rows) {
  std::string out = "\x1b[0m";
  int y = 0;
  for (const line& l : lines) {
    if (++y > rows) break;
    out += fmt("\x1b[%d;1H\x1b[2K", y);
    span pen;  // What the terminal is set to, within this row. Its text is not used
    bool started = false;
    for (const span& p : l) {
      // Spaces show only a background: the foreground stays for whatever comes next.
      if (started && p.text.find_first_not_of(' ') == std::string::npos) {
        if (p.bg256 != pen.bg256) {
          out += "\x1b[";
          put_background(out, p.bg256);
          out += 'm';
          pen.bg256 = p.bg256;
        }
      } else {
        put_pen(out, pen, started, p);
        started = true;
      }
      out += p.text;
    }
    out += "\x1b[0m";
  }
  if (y < rows) out += fmt("\x1b[%d;1H\x1b[J", y + 1);
  return out;
}

}  // namespace wwt
