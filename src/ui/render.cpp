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

constexpr int kListGap = 3;  // " │ " between two lists
constexpr const char* kTwoLists = "two systems, two lists: never compare or add across them";

std::map<uint32_t, double> busiest_engine(const snapshot& s) {  // The busiest engine: engines are not additive
  std::map<uint32_t, double> gpu_pct;
  for (const gpu_engine_row& e : s.gpu_engines) gpu_pct[e.pid] = std::max(gpu_pct[e.pid], e.pct);
  return gpu_pct;
}

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
    r.add(pad_left(fmt("%.2f", p.cores), 6)).add(" ");
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
  const std::string count = fmt("  top %d of %lld", static_cast<int>(shown), static_cast<long long>(w.process_count));
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
  const int name_w = std::clamp(col - (pids ? 34 : 26), 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("RSS GiB", 8), tone::dim).add("  st", tone::dim);
  cols.add("  dxg", tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const wsl_process& p = *list[i];
    row r;
    r.add(pad_left(fmt("%.2f", p.cores), 6)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text).add(pad_left(fmt("%.1f", gib(p.rss)), 8));
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
  frame.push_back(title_row(s, now_ms));
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
// Every graph is one figure on a fixed 0-100 scale, width mapped to a span of time. Time places a
// sample, never its index. Graphs are not stacked, summed or overlaid: each names its own source.

constexpr int kGutter = 4;       // "100│"
constexpr int kMinGraph = 2;     // Rows of a graph when the window is short: room for "100" and "0"
constexpr int kMinPlot = kGutter + 9;  // Narrower than this "-60s" and "now" do not fit: no graph
constexpr int kMinPanel = 36;    // Fewer panels to a row rather than narrower than this
constexpr int kPanelGap = 2;
constexpr int kTitleLines = 3;
constexpr int kMaxGpuList = 14;  // Rows of the GPU panel's lists; height beyond this goes to the graphs
constexpr int kSideBySideGpu = 64;  // Those lists are short and stay side by side down to here

// One value per dot column, oldest on the left, 0-100; NaN where nothing was measured. Columns from the
// end of the previous sample up to this one take its value, if the previous one is close enough to
// join and measured this figure. Several samples in one column show the highest.
std::vector<double> plot_columns(const history& h, double history_sample::*series, int64_t end_ms, int dots, int64_t span_ms = history::kSpanMs) {
  std::vector<double> cols(static_cast<size_t>(std::max(0, dots)), std::numeric_limits<double>::quiet_NaN());
  if (dots <= 0) return cols;
  const double end = static_cast<double>(end_ms), span = static_cast<double>(span_ms);
  const auto x_of = [&](double t) { return std::lround((dots - 1) * (1.0 - (end - t) / span)); };
  bool joined = false;
  double before = 0;
  for (const history_sample& p : h.samples()) {
    const double t = static_cast<double>(p.unix_ms), value = p.*series;
    const bool known = std::isfinite(value);
    if (known && t <= end && end - t <= span) {
      const long x = x_of(t);
      const long from = joined && t - before <= kGapMs ? std::min(x, std::max(0L, x_of(before) + 1)) : x;
      const double v = std::clamp(value, 0.0, 100.0);
      for (long k = from; k <= x; ++k) {
        double& c = cols[static_cast<size_t>(k)];
        c = std::isnan(c) ? v : std::max(c, v);
      }
    }
    joined = known;
    before = t;
  }
  return cols;
}

// Filled braille, one string per row from the top. A measured value always shows at least its bottom
// dot, so a measured 0 is a line and a gap is empty.
std::vector<std::string> plot_rows(const std::vector<double>& cols, int rows) {
  std::vector<std::string> out(static_cast<size_t>(rows));
  const int cells = static_cast<int>(cols.size()) / 2;
  for (int y = 0; y < rows; ++y) {
    for (int c = 0; c < cells; ++c) {
      unsigned bits = 0;
      for (int side = 0; side < 2; ++side) {
        const double v = cols[static_cast<size_t>(2 * c + side)];
        if (std::isnan(v)) continue;
        const int high = std::max(1, static_cast<int>(std::lround(v / 100.0 * 4 * rows)));
        const int dots = std::clamp(high - (rows - 1 - y) * 4, 0, 4);
        for (int k = 0; k < dots; ++k) bits |= (side == 0 ? kDotLeft : kDotRight)[k];
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
  int cells = 0;
  int64_t bucket_ms = 1;
  std::vector<const history_sample*> samples;  // Those on the axis and the one before, oldest first
  std::vector<int> cell;                       // The cell of each; below 0 for that one before
};

int64_t floor_div(int64_t a, int64_t b) { return a / b - (a % b != 0 && a < 0 ? 1 : 0); }

// The right edge is the bucket end_ms falls in. Whole milliseconds, rounded up, so cells cover the span.
time_axis make_axis(const history& h, int64_t end_ms, int64_t span_ms, int cells) {
  time_axis a;
  a.cells = std::max(0, cells);
  if (a.cells == 0) return a;
  a.bucket_ms = std::max<int64_t>(1, (span_ms + a.cells - 1) / a.cells);
  const int64_t last = floor_div(end_ms, a.bucket_ms);
  const sample_range all = h.samples();
  for (size_t i = 0; i < all.size(); ++i) {
    const int64_t back = last - floor_div(all[i].unix_ms, a.bucket_ms);  // Cells left of the right edge
    if (back < 0) break;                                                 // Ahead of the axis, and so is the rest
    const bool on_axis = back < a.cells;
    if (!on_axis && !a.samples.empty()) {  // Only the newest sample before the left edge is kept
      a.samples.back() = &all[i];
      continue;
    }
    a.samples.push_back(&all[i]);
    a.cell.push_back(on_axis ? a.cells - 1 - static_cast<int>(back) : -1);
  }
  return a;
}

// One figure on an axis, 0-100, NaN where nothing was measured. A bucket holds its highest sample, or the
// mean of those it has when peak is false, and NaN never counts. A bucket no sample fell in, between two
// samples close enough to join, takes the earlier one: a cell narrower than a second would stripe.
template <class Value> std::vector<double> bucket_cells(const time_axis& a, bool peak, Value value) {
  std::vector<double> out(static_cast<size_t>(a.cells), std::numeric_limits<double>::quiet_NaN());
  std::vector<int> count(static_cast<size_t>(a.cells), 0);
  bool joined = false;
  int from = 0;
  int64_t before = 0;
  double held = 0;
  for (size_t i = 0; i < a.samples.size(); ++i) {
    const history_sample& p = *a.samples[i];
    const double raw = value(p);
    const bool known = std::isfinite(raw);
    if (known) {
      const double v = std::clamp(raw, 0.0, 100.0);
      const int c = a.cell[i];
      if (joined && p.unix_ms - before <= kGapMs) {
        for (int k = std::max(0, from + 1); k < c; ++k) out[static_cast<size_t>(k)] = held;
      }
      if (c >= 0) {
        double& cell = out[static_cast<size_t>(c)];
        int& n = count[static_cast<size_t>(c)];
        cell = n == 0 ? v : peak ? std::max(cell, v) : cell + v;
        ++n;
      }
      from = c;
      held = v;
    }
    joined = known;
    before = p.unix_ms;
  }
  if (!peak) {
    for (size_t c = 0; c < out.size(); ++c) {
      if (count[c] > 1) out[c] /= count[c];
    }
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
  double history_sample::*series = nullptr;
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

std::vector<line> panel_graph(const panel& p, const history& h, int64_t end_ms, int col, int rows) {
  const int cells = col - kGutter;
  const std::vector<std::string> plot = plot_rows(plot_columns(h, p.series, end_ms, 2 * cells), rows);
  std::vector<line> out;
  for (int y = 0; y < rows; ++y) {
    row r;
    r.add(y == 0 ? "100" : y == rows - 1 ? "  0" : "   ", tone::dim).add("│", tone::faint);
    r.add(plot[static_cast<size_t>(y)], tone::bar);
    out.push_back(r);
  }
  row axis;
  axis.to(kGutter).add("-60s ", tone::dim).add(repeat("─", cells - 9), tone::faint).add(" now", tone::dim);
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

void draw_rows(std::vector<line>& out, const std::vector<grid_row>& rows, const history& h, int64_t end_ms) {
  for (const grid_row& r : rows) {
    std::vector<std::vector<line>> columns;
    for (const panel* p : r.cells) {
      std::vector<line> c = panel_title(*p, r.col);
      c.resize(static_cast<size_t>(r.title_h));
      if (r.plot) {
        const std::vector<line> graph = panel_graph(*p, h, end_ms, r.col, r.graph_h);
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

// ---- The panels ----------------------------------------------------------------------------------

struct panel_frame {
  std::vector<line> lines;
  bool hidden = false;        // A block was dropped for want of rows
  bool span_applies = false;  // Drawn on a span the caller can change
};

std::vector<line> gpu_users_list(const snapshot& s, int col, int rows, bool& cut) {
  std::vector<line> out;
  std::vector<std::pair<uint32_t, double>> list;
  for (const auto& e : busiest_engine(s)) {
    if (e.second > 0) list.push_back(e);
  }
  std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  row head;
  head.add("Windows", tone::title, true);
  if (list.empty()) {
    out.push_back(head);
    out.push_back(message("", s.gpu_engines_known ? "no process with GPU use" : "unavailable: no GPU engine data", tone::watch, 0));
    return out;
  }
  std::map<uint32_t, const process_row*> by_pid;
  for (const process_row& p : s.processes) by_pid[p.pid] = &p;
  std::map<uint32_t, uint64_t> vram;
  for (const gpu_memory_row& m : s.gpu_memory) vram[m.pid] = m.local;

  const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(0, rows)));
  cut = cut || shown < list.size();
  head.add(fmt("  top %d of %d by GPU%%, busiest engine", static_cast<int>(shown), static_cast<int>(list.size())), tone::dim);
  out.push_back(head);

  const bool pids = col >= 36;
  const int name_w = std::clamp(col - (pids ? 27 : 19), 4, 40);
  row cols;
  cols.add(pad_left("GPU%", 5), tone::dim).add(" ");
  if (pids) cols.add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("LOCAL", 6), tone::dim);
  out.push_back(cols);

  for (size_t i = 0; i < shown; ++i) {
    const uint32_t pid = list[i].first;
    const auto p = by_pid.find(pid);
    const auto v = vram.find(pid);
    const bool is_vm = pid == s.vm.vmmem_pid || pid == s.vm.vmwp_pid;
    row r;
    r.add(pad_left(number(list[i].second, "%.0f"), 5)).add(" ");
    if (pids) r.add(pad_left(fmt("%u", pid), 7), tone::dim).add(" ");
    r.add(pad(p == by_pid.end() ? "-" : std::string_view(p->second->name), name_w), is_vm ? tone::title : tone::text);
    r.add(pad_left(v == vram.end() ? "-" : fmt("%.1f", gib(v->second)), 6));
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(r);
  }
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

// avail covers everything drawn here. cut comes back true if either list left a row out.
void gpu_lists(std::vector<line>& out, const snapshot& s, const wsl_view& w, int width, int avail, bool& cut) {
  out.push_back(rule("PROCESSES", kTwoLists, width));
  if (width >= kSideBySideGpu) {
    const int col = (width - kListGap) / 2;
    side_by_side(out, gpu_users_list(s, col, avail - 3, cut), dxg_holder_list(w, width - col - kListGap, avail - 3 - denied_rows(w), cut), col);
    return;
  }
  // Stacked: rule, two heads, two column rows, the warning. The rest is shared, and what the first
  // list leaves over goes to the second.
  const int total = std::max(0, avail - 5 - denied_rows(w));
  const int holders = w.ok ? static_cast<int>(std::min<size_t>(w.dxg_pids.size(), static_cast<size_t>(total))) : 0;
  const std::vector<line> top = gpu_users_list(s, width, total - std::min(total / 2, holders), cut);
  const std::vector<line> bottom = dxg_holder_list(w, width, avail - 3 - denied_rows(w) - static_cast<int>(top.size()), cut);
  out.insert(out.end(), top.begin(), top.end());
  out.insert(out.end(), bottom.begin(), bottom.end());
}

panel_frame gpu_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms) {
  const gpu_device_view& g = s.gpu_device;
  const gpu_split_view& split = s.gpu_split;
  const std::vector<panel> top = {
      pct_panel({"device · NVML"}, g.present && g.has_util, g.util_pct, &history_sample::gpu_util),
      pct_panel({"WSL VM ·", "engine counters,", "VM level, not exact shares"}, split.valid, split.vm_pct, &history_sample::vm_pct),
      pct_panel({"Windows · engine counters"}, split.valid, split.windows_pct, &history_sample::windows_pct)};

  panel memory{{"GPU memory · NVML,", "used of total"}, "n/a", tone::dim, &history_sample::gpu_mem_pct};
  if (g.present && g.has_mem && g.mem_total > 0) {
    memory.value = fmt("%.1f/%.1f GiB", gib(g.mem_used), gib(g.mem_total));
    memory.color = tone::text;
  }
  panel power{{"power · NVML,", "of limit"}, "n/a", tone::dim, &history_sample::gpu_power_pct};
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
  std::vector<grid_row> rows;
  add_panels(rows, top, width);
  add_panels(rows, second, width);
  f.hidden = fit_rows(rows, room);
  room -= rows_height(rows);

  // Lower blocks: as much of the lists as they can use, then the device line goes last of the two.
  const bool device = room >= 1;
  if (device) --room;
  std::vector<line> lists;
  // At least one row of each list, and the warning.
  if (const int least = (width >= kSideBySideGpu ? 4 : 7) + denied_rows(w); room >= least) {
    bool cut = false;
    gpu_lists(lists, s, w, width, std::min(room, kMaxGpuList), cut);
    room -= static_cast<int>(lists.size());
    f.hidden = f.hidden || cut;
  } else {
    f.hidden = true;
  }
  if (!device) f.hidden = true;

  const bool head = room >= 1;
  if (head) {
    // docs/measurable.md: close under load, not at idle, and the cause is unresolved.
    f.lines.push_back(rule("GPU", width >= 100 ? "device and engine counters can differ, the difference is unexplained" : "device vs engine gap unexplained", width));
    --room;
  } else {
    f.hidden = true;
  }
  if (room > 0) grow_rows(rows, room);
  draw_rows(f.lines, rows, h, graph_end(h, now_ms));
  if (device) f.lines.push_back(device_row(g, width));
  f.lines.insert(f.lines.end(), lists.begin(), lists.end());
  return f;
}

panel_frame cpu_small_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms) {
  // Stacked lists have two heads and share their rows; below 8 rows the Windows list gets none.
  const bool wide = width >= 100;
  const int least = wide ? 5 : 8;
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
  int list_rows = 0;
  if (show_cores && room >= least) {  // A share goes to the graphs, and whatever short lists leave over
    std::vector<line> want;
    process_section(want, s, w, width, room);
    list_rows = std::min(static_cast<int>(want.size()), std::max(least, room - room * (wide ? 2 : 1) / 5));
    room -= list_rows;
  } else {
    f.hidden = true;
  }
  if (head) f.lines.push_back(rule("CPU", "two systems' own views of overlapping work, never added", width));
  else f.hidden = true;
  if (room > 0) grow_rows(rows, room);
  draw_rows(f.lines, rows, h, graph_end(h, now_ms));
  if (show_cores) f.lines.insert(f.lines.end(), cores.begin(), cores.end());
  if (list_rows > 0) process_section(f.lines, s, w, width, list_rows);
  return f;
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
constexpr int kLaneRows = 2, kRamRows = 2;
constexpr int kFloorHeatRows = 10, kMostHeatRows = 20;  // Below the first the heat block has what it asks for
constexpr int kCpuFixedRows = 7 + kListHeads + kLeastListRows + 2 + kLeastBusyRows;  // 17: title, rule, 4 heads, footer; lists; caption, axis; busy
constexpr int kHeadValue = 4;        // Columns of the value that starts a head
constexpr int kLeastDistro = 6;      // A name cut shorter than this says nothing

// Rows of each block. height includes the footer. docs/design/cpu-panel.md, "Allocation", in its order;
// false when the window is below this layout's floor.
struct cpu_rows {
  int heat = 0, busy = kLeastBusyRows, lanes = 0, ram = 0, lists = kLeastListRows;
};

bool plan_cpu_rows(int natural, int height, cpu_rows& p) {
  p = {};
  p.heat = std::min(natural, kFloorHeatRows);
  int left = height - kCpuFixedRows - p.heat;
  if (left < 0) return false;
  if (left >= kLaneRows) {
    p.lanes = kLaneRows;
    left -= kLaneRows;
    p.ram = std::min(left, kRamRows);  // One row left is the RAM lane
    left -= p.ram;
  }
  const auto take = [&](int& rows, int most) {
    const int extra = std::clamp(most - rows, 0, left);
    rows += extra;
    left -= extra;
  };
  take(p.lists, kGoodListRows);  // The lists are guaranteed before anything graphs more
  take(p.heat, std::min(natural, kMostHeatRows));
  take(p.busy, kMostBusyRows);
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
    const std::vector<double> one = bucket_cells(a, false, [&](const history_sample& p) { return static_cast<double>(value(p, t)); });
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

// A plot's cells are a whole bucket wide: both dots of a braille cell show the same value.
std::vector<line> plot_block(const std::vector<double>& cells, int rows, std::string_view top, std::string_view bottom) {
  std::vector<double> dots;
  dots.reserve(cells.size() * 2);
  for (const double v : cells) dots.insert(dots.end(), 2, v);
  const std::vector<std::string> plot = plot_rows(dots, rows);
  std::vector<line> out;
  for (int y = 0; y < rows; ++y) {
    row r;
    gutter(r, y == 0 ? top : y == rows - 1 ? bottom : std::string_view());
    r.add(plot[static_cast<size_t>(y)], tone::bar);
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

std::string span_text(int64_t span_ms) {
  return span_ms % 60000 == 0 && span_ms > 60000 ? fmt("%dm", static_cast<int>(span_ms / 60000)) : fmt("%ds", static_cast<int>(span_ms / 1000));
}

line axis_row(const time_axis& a, int64_t span_ms, int64_t collected_ms) {
  const double bucket_s = static_cast<double>(a.bucket_ms) / 1000.0;
  // A sample's worth of slack: the ring can hold slightly under the longest span, and must still end.
  const bool collecting = collected_ms + static_cast<int64_t>(kGapMs) < span_ms;
  const std::string left = "-" + span_text(span_ms);
  const std::string mid = collecting ? fmt("collecting: %d s of %d s", static_cast<int>(collected_ms / 1000), static_cast<int>(span_ms / 1000))
                          : span_ms > history::kSpanMs ? fmt("peak over mean · %.0f s", bucket_s)
                                                       : fmt("each cell %.1f s", bucket_s);
  const int rules = a.cells - display_width(left) - display_width(mid) - 7;  // "-60s ", " mid ", " now"
  row r;
  r.to(kCpuGutter).add(left + " ", tone::dim).add(repeat("─", rules / 2), tone::faint);
  r.add(" " + mid + " ", collecting ? tone::text : tone::dim).add(repeat("─", rules - rules / 2), tone::faint).add(" now", tone::dim);
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

std::string used_of(uint64_t total, uint64_t available) {
  return fmt("%.1f/%.1f GiB", gib(total - std::min(total, available)), gib(total));
}

void ram_head(row& r, uint64_t total, uint64_t available, bool show_free) {
  r.add("RAM ", tone::dim);
  if (total == 0) {
    r.add("n/a", tone::dim);
    return;
  }
  const tone t = left_tone(100.0 * static_cast<double>(available) / static_cast<double>(total));
  r.add(used_of(total, available), t, t != tone::text);
  if (show_free) r.add(fmt("  free %.1f GiB", gib(std::min(total, available))), tone::dim);
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

  row ram;
  ram_head(ram, s.memory.total, s.memory.available, true);
  return {busy, source, ran, ram};
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
  const std::string load = "load " + number(or_nan(w.load[0]), "%.1f");
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

void pressure_head(row& r, const wsl_view& w, int col) {
  if (!w.psi_cpu && !w.psi_memory && !w.psi_io) {
    r.add_if_fits(" · pressure n/a", col);
    return;
  }
  const auto high = [](const std::optional<double>& v) { return v && *v >= kPressureWatch; };
  const std::string cpu = number(or_nan(w.psi_cpu), "%.0f"), mem = number(or_nan(w.psi_memory), "%.0f"), io = number(or_nan(w.psi_io), "%.0f");
  const std::string long_text = " · pressure cpu " + cpu + " mem " + mem + " io " + io;
  if (r.cols() + display_width(long_text) <= col) {
    r.add(" · pressure", tone::dim);
    r.add(" cpu " + cpu, high(w.psi_cpu) ? tone::watch : tone::text, high(w.psi_cpu));
    r.add(" mem " + mem, high(w.psi_memory) ? tone::watch : tone::text, high(w.psi_memory));
    r.add(" io " + io, high(w.psi_io) ? tone::watch : tone::text, high(w.psi_io));
    return;
  }
  const bool any = high(w.psi_cpu) || high(w.psi_memory) || high(w.psi_io);
  r.add_if_fits(" · psi " + cpu + "/" + mem + "/" + io, col, any ? tone::watch : tone::text);
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
  if (w.ok && w.steal_pct > 0) steal = " · steal " + number(w.steal_pct, "%.1f") + "%";
  row source;
  source.to(kHeadValue + 2).add(kSource, tone::dim);
  const int room = col - source.cols() - display_width(age) - display_width(steal) - 3;
  if (room >= kLeastDistro && !w.distro.empty()) source.add(" · " + fit(w.distro, room), tone::dim);
  source.add(age, tone::watch, true).add_if_fits(steal, col);

  row third, ram;
  if (w.ok) {
    short_head(third, s, w, col);
    ram_head(ram, w.mem_total, w.mem_available, false);
    pressure_head(ram, w, col);
  } else if (s.wsl_json.empty() && s.wsl_restarts == 0) {
    third.add(fmt("WSL starting, no data yet: %d s", static_cast<int>(waited_ms / 1000)), tone::watch);
    ram_head(ram, 0, 0, false);
  } else {
    third.add(s.wsl_json.empty() ? "WSL unavailable: the collector sent nothing" : parsed.schema > 1 ? "WSL unavailable: newer schema than this screen" : "WSL unavailable: unreadable collector line", tone::watch);
    ram_head(ram, 0, 0, false);
  }
  if (!w.ok && s.wsl_restarts > 0) ram.add(fmt(" · collector restarted %d times", s.wsl_restarts), tone::watch);
  return {busy, source, third, ram};
}

// ---- The lists: each sorted on its own, in its own units

// Rounded to 0.1 and then PID, so that rows of similar cores do not swap each second.
int64_t cores_key(double cores) { return std::isfinite(cores) ? std::llround(std::clamp(cores, 0.0, 1e9) * 10) : -1; }

std::vector<line> cpu_windows_list(const snapshot& s, int col, int rows) {
  std::vector<const process_row*> list;
  for (const process_row& p : s.processes) {
    if (p.pid != 0) list.push_back(&p);  // PID 0 is the idle process: its cores are idle time
  }
  row head;
  head.add("Windows", tone::title, true);
  if (list.empty()) return {head, message("", "unavailable", tone::watch, 0)};
  std::stable_sort(list.begin(), list.end(), [](const process_row* a, const process_row* b) {
    const int64_t ka = cores_key(a->cores), kb = cores_key(b->cores);
    return ka != kb ? ka > kb : a->pid < b->pid;
  });
  const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(0, rows)));
  head.add(fmt("  top %d of %d by cores", static_cast<int>(shown), static_cast<int>(list.size())), tone::dim);

  const int name_w = std::clamp(col - 33, 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("thr", 4), tone::dim).add(pad_left("WS GiB", 7), tone::dim);
  std::vector<line> out = {head, cols};

  for (size_t i = 0; i < shown; ++i) {
    const process_row& p = *list[i];
    const bool is_vm = p.pid == s.vm.vmmem_pid || p.pid == s.vm.vmwp_pid;
    const bool unmeasured = p.pid == s.vm.vmmem_pid && !s.vm.vmmem_cores_known;  // An unmeasured 0
    row r;
    r.add(pad_left(unmeasured ? "n/a" : number(p.cores, "%.2f"), 6)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), is_vm ? tone::title : tone::text).add(pad_left(fmt("%u", p.threads), 4), tone::dim);
    r.add(pad_left(fmt("%.1f", gib(p.working_set)), 7));
    if (is_vm) r.add(" WSL VM", tone::dim);
    out.push_back(r);
  }
  return out;
}

std::vector<line> cpu_wsl_list(const wsl_view& w, int col, int rows) {
  row head;
  head.add("WSL", tone::title, true);
  if (!w.ok) return {head, message("", "unavailable", tone::watch, 0)};
  std::vector<const wsl_process*> list;
  for (const wsl_process& p : w.processes) list.push_back(&p);
  std::stable_sort(list.begin(), list.end(), [](const wsl_process* a, const wsl_process* b) {
    const int64_t ka = cores_key(a->cores), kb = cores_key(b->cores);
    return ka != kb ? ka > kb : a->pid < b->pid;
  });
  const size_t shown = std::min(list.size(), static_cast<size_t>(std::max(0, rows)));
  // The "holder" column is /dev/dxg open, not GPU use, and this head is where the list says so.
  const std::string count = fmt("  top %d of %lld", static_cast<int>(shown), static_cast<long long>(w.process_count));
  static constexpr std::string_view kBy = " by cores", kNoUsage = ", dxg: no usage measured";
  head.add(count, tone::dim);
  if (head.cols() + static_cast<int>(kBy.size() + kNoUsage.size()) <= col) head.add(kBy, tone::dim);
  head.add(kNoUsage, tone::dim);

  const int name_w = std::clamp(col - 38, 4, 40);
  row cols;
  cols.add(pad_left("cores", 6), tone::dim).add(" ").add(pad_left("pid", 7), tone::dim).add(" ");
  cols.add(pad("name", name_w), tone::dim).add(pad_left("thr", 4), tone::dim).add(pad_left("RSS GiB", 8), tone::dim).add(" st dxg", tone::dim);
  std::vector<line> out = {head, cols};

  for (size_t i = 0; i < shown; ++i) {
    const wsl_process& p = *list[i];
    row r;
    r.add(pad_left(number(p.cores, "%.2f"), 6)).add(" ").add(pad_left(fmt("%u", p.pid), 7), tone::dim).add(" ");
    r.add(pad(p.name, name_w), p.on_gpu ? tone::title : tone::text);
    r.add(pad_left(p.threads ? fmt("%u", *p.threads) : "n/a", 4), tone::dim).add(pad_left(fmt("%.1f", gib(p.rss)), 8));
    r.add("  ").add(pad(p.state, 1), p.state == "D" ? tone::watch : tone::dim);
    if (p.on_gpu) r.add(" holder");
    out.push_back(r);
  }
  return out;
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

// height here leaves out the footer, as in the other panels.
panel_frame cpu_frame(const snapshot& s, const wsl_view& w, const history& h, int width, int height, int64_t now_ms, int64_t span_ms) {
  cpu_rows plan;
  thread_order left_order, right_order;
  if (!plan_large_cpu(s, w, h, width, height + 1, plan, left_order, right_order)) return cpu_small_frame(s, w, h, width, height, now_ms);

  const int col = (width - kListGap) / 2, right_col = width - col - kListGap;
  // The newest sample is the right edge unless it is late: a redraw between samples must not open an empty
  // bucket at now, because blank means not measured.
  const int64_t last_sample = h.samples().empty() ? 0 : h.samples().back().unix_ms;
  const int64_t end = !h.samples().empty() && static_cast<double>(now_ms - last_sample) <= kGapMs ? last_sample : graph_end(h, now_ms);
  const time_axis axis = make_axis(h, end, span_ms, col - kCpuGutter);  // The smaller column: both plots are this wide
  const int64_t collected = h.samples().empty() ? 0 : end - h.samples().front().unix_ms;
  const auto peak = [&](float history_sample::*series) { return bucket_cells(axis, true, [series](const history_sample& p) { return static_cast<double>(p.*series); }); };
  const auto mean = [&](float history_sample::*series) { return bucket_cells(axis, false, [series](const history_sample& p) { return static_cast<double>(p.*series); }); };

  std::vector<line> left = windows_heads(s, static_cast<int>(s.cpu.core_pct.size()), col);
  std::vector<line> right = wsl_heads(s, w, right_col, collected);
  const auto append = [](std::vector<line>& to, std::vector<line> from) { to.insert(to.end(), from.begin(), from.end()); };

  const std::vector<double> windows_busy = bucket_cells(axis, true, [](const history_sample& p) { return p.windows_cpu; });
  append(left, plot_block(windows_busy, plan.busy, "100", "  0"));
  append(right, plot_block(peak(&history_sample::wsl_busy), plan.busy, "100", "  0"));
  if (plan.ram == kRamRows) {
    append(left, plot_block(peak(&history_sample::windows_ram), kRamRows, "RAM", ""));
    append(right, plot_block(peak(&history_sample::wsl_ram), kRamRows, "RAM", ""));
  } else if (plan.ram == 1) {
    left.push_back(lane("RAM", mean(&history_sample::windows_ram)));
    right.push_back(lane("RAM", mean(&history_sample::wsl_ram)));
  }
  if (plan.lanes > 0) {
    left.push_back(lane("kern", mean(&history_sample::own_kernel)));
    left.push_back(lane("VM", mean(&history_sample::vm_cpu)));
    right.push_back(lane("iowt", mean(&history_sample::wsl_iowait)));
    right.push_back(lost_lane(mean(&history_sample::lost_vcpus)));
  }

  bool left_highest = false, right_highest = false;
  const std::vector<heat_row> left_rows = heat_rows(left_order, plan.heat, left_highest), right_rows = heat_rows(right_order, plan.heat, right_highest);
  const bool guest_live = w.ok && s.wsl_age_s < kStaleS;
  left.push_back(heat_caption("threads", hot_count(s.cpu.core_pct, true), left_highest, true, col));
  right.push_back(heat_caption("vCPUs, not Windows cores", hot_count(w.core_pct, guest_live), right_highest, false, right_col));
  append(left, heat_block(left_rows, axis, windows_core_at));
  append(right, heat_block(right_rows, axis, wsl_core_at));
  left.resize(std::max(left.size(), right.size()));
  right.resize(left.size());
  left.push_back(axis_row(axis, span_ms, collected));
  right.push_back(axis_row(axis, span_ms, collected));

  append(left, cpu_windows_list(s, col, plan.lists));
  append(right, cpu_wsl_list(w, right_col, plan.lists));

  panel_frame f;
  f.span_applies = true;
  f.hidden = plan.lanes == 0 || plan.ram == 0;
  f.lines.push_back(title_row(s, now_ms));
  f.lines.push_back(rule("CPU", "two systems' own views of overlapping work, never added", width));
  side_by_side(f.lines, left, right, col);
  f.lines.resize(static_cast<size_t>(height));  // The footer stays at the bottom however short the lists are
  return f;
}

constexpr int kHiddenNote = 21;  // Columns of the note below, after the 39 of the keys

line footer(view v, int width, bool hidden, const std::string& span) {
  row r;
  const auto key = [&](const char* text, view which) {
    const bool on = v == which;
    r.add(text, on ? tone::title : tone::dim, on).add("  ");
  };
  // Too narrow for the note, a marker first: the cut at the right edge cannot take it.
  if (hidden && width < 39 + kHiddenNote) r.add("… ", tone::watch);
  key("[1] overall", view::overall);
  key("[2] gpu", view::gpu);
  key("[3] cpu", view::cpu);
  if (!span.empty()) r.add("[t] span " + span + "  ", tone::dim);
  r.add("[q] quit", tone::dim);
  if (hidden) r.add_if_fits("   … some rows hidden", width, tone::watch);
  return r;
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

void history::push(const snapshot& s) {
  const int64_t t = s.unix_ms;
  if (t <= 0 || t > kLatestMs) return;  // A sample that cannot be placed in time is a gap

  // The wall clock stepped back. interval_s comes from QPC, so the old samples keep their spacing and
  // move with the clock: nothing is lost and nothing lands ahead of "now". After a stall as long as
  // what is kept they are off every graph anyway, and must not join the new sample as if continuous.
  if (count_ > 0 && t <= at(count_ - 1).unix_ms) {
    if (s.interval_s >= static_cast<double>(kKeepMs) / 1000.0) {
      count_ = 0;
    } else {
      const double gap_s = s.interval_s > 0 ? s.interval_s : 1.0;
      const int64_t shift = at(count_ - 1).unix_ms + static_cast<int64_t>(gap_s * 1000.0) - t;
      for (size_t i = 0; i < count_; ++i) at(i).unix_ms -= shift;
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
  n.windows_ram = s.memory.total > 0 ? to_float(100.0 * static_cast<double>(s.memory.total - std::min(s.memory.total, s.memory.available)) / static_cast<double>(s.memory.total)) : gap;
  n.wsl_busy = guest ? to_float(w.busy_pct) : gap;
  n.wsl_iowait = guest && w.cpu_iowait ? to_float(*w.cpu_iowait) : gap;
  n.wsl_ram = guest && w.mem_total > 0 ? to_float(100.0 * static_cast<double>(w.mem_total - std::min(w.mem_total, w.mem_available)) / static_cast<double>(w.mem_total)) : gap;
  n.lost_vcpus = s.lost.state == lost_state::known ? to_float(s.lost.vcpus) : gap;
  n.windows_threads = put_cores(s.cpu.core_pct, n.windows_core);
  static const std::vector<double> kNone;
  n.wsl_threads = put_cores(guest ? w.core_pct : kNone, n.wsl_core);

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

int64_t next_span(int64_t span_ms) {
  for (size_t i = 0; i + 1 < std::size(kSpansMs); ++i) {
    if (kSpansMs[i] == span_ms) return kSpansMs[i + 1];
  }
  return kSpansMs[0];
}

bool span_key_acts(const snapshot& s, const history& hist, view v, int width, int height) {
  cpu_rows plan;
  thread_order left, right;
  return v == view::cpu && plan_large_cpu(s, read_wsl(s.wsl_json), hist, width, height, plan, left, right);
}

std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms, const history* hist, view v, const render_options& options) {
  if (width <= 0 || height <= 0) return {};
  const wsl_view w = read_wsl(s.wsl_json);
  if (!hist) return overall_frame(s, w, width, height, now_ms);

  panel_frame f;
  const int body = height - 1;  // The footer is the last row, whatever else has to give
  if (v == view::overall) f.lines = overall_frame(s, w, width, body, now_ms);
  else if (v == view::gpu) f = gpu_frame(s, w, *hist, width, body, now_ms);
  else f = cpu_frame(s, w, *hist, width, body, now_ms, options.span_ms);

  std::vector<line> out;
  for (size_t i = 0; i < f.lines.size() && i < static_cast<size_t>(body); ++i) out.push_back(clip(f.lines[i], width));
  out.push_back(clip(footer(v, width, f.hidden, options.span_key && f.span_applies ? span_text(options.span_ms) : std::string()), width));
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
  out += bg256 >= 0 ? "48;5;" + std::to_string(bg256) : "49";
}

bool same_foreground(const span& a, const span& b) { return a.fg256 == b.fg256 && (a.fg256 >= 0 || a.color == b.color); }

// Takes the terminal from what pen says it is to what p needs: nothing if they look the same, only the
// difference if it can, and a reset where bold changes, which has no short way back.
void put_pen(std::string& out, span& pen, bool started, const span& p) {
  if (!started || pen.bold != p.bold) {
    out += p.bold ? "\x1b[0;1;" : "\x1b[0;";
    put_foreground(out, p);
    if (p.bg256 >= 0) {
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
