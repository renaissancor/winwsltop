#include "core/wsl_view.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace wwt {
namespace {

// A strict walker over one JSON text. The first error sets bad_ and everything after it is a no-op.
class reader {
 public:
  explicit reader(std::string_view s) : s_(s) {}

  bool bad() const { return bad_; }
  bool at_end() { space(); return i_ == s_.size(); }

  // Calls member(key) with the cursor on each value; member must consume it.
  template <class F> void object(F member) {
    if (!enter('{')) return;
    if (!take('}')) {
      do {
        std::string key;
        text(key);
        expect(':');
        if (bad_) break;
        member(key);
      } while (!bad_ && take(','));
      expect('}');
    }
    --depth_;
  }

  template <class F> void array(F element) {
    if (!enter('[')) return;
    if (!take(']')) {
      do { element(); } while (!bad_ && take(','));
      expect(']');
    }
    --depth_;
  }

  void number(double& out) {
    space();
    if (bad_ || i_ == s_.size() || !(s_[i_] == '-' || (s_[i_] >= '0' && s_[i_] <= '9'))) return fail();
    double v = 0;
    const auto r = std::from_chars(s_.data() + i_, s_.data() + s_.size(), v);
    if (r.ec != std::errc() || !std::isfinite(v)) return fail();  // from_chars takes "-inf"
    i_ = static_cast<size_t>(r.ptr - s_.data());
    out = v;
  }

  template <class T> void whole(T& out) {
    double v = 0;
    number(v);
    const double top = static_cast<double>(std::numeric_limits<T>::max());
    if (!bad_) out = v > 0 && v < top ? static_cast<T>(v) : T{};
  }

  void number_or_null(std::optional<double>& out) {
    if (word("null")) { out.reset(); return; }
    double v = 0;
    number(v);
    if (!bad_) out = v;
  }

  template <class T> void whole_or_null(std::optional<T>& out) {
    if (word("null")) { out.reset(); return; }
    T v{};
    whole(v);
    if (!bad_) out = v;
  }

  // For a key whose whole value can be null: consumes it and says so.
  bool null() { return word("null"); }

  void boolean(bool& out) {
    if (word("true")) out = true;
    else if (word("false")) out = false;
    else fail();
  }

  void text(std::string& out) {
    space();
    if (bad_ || i_ == s_.size() || s_[i_] != '"') return fail();
    ++i_;
    out.clear();
    while (!bad_) {
      if (i_ == s_.size()) return fail();
      const char c = s_[i_++];
      if (c == '"') return;
      if (c != '\\') { out += c; continue; }
      if (i_ == s_.size()) return fail();
      const char e = s_[i_++];
      switch (e) {
        case '"': case '\\': case '/': out += e; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': escape(out); break;
        default: fail();
      }
    }
  }

  void skip() {
    space();
    if (bad_ || i_ == s_.size()) return fail();
    const char c = s_[i_];
    std::string ignored;
    double n = 0;
    if (c == '{') object([&](const std::string&) { skip(); });
    else if (c == '[') array([&] { skip(); });
    else if (c == '"') text(ignored);
    else if (word("null") || word("true") || word("false")) return;
    else number(n);
  }

 private:
  void fail() { bad_ = true; }
  void space() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }
  bool take(char c) {
    space();
    if (i_ == s_.size() || s_[i_] != c) return false;
    ++i_;
    return true;
  }
  void expect(char c) { if (!bad_ && !take(c)) fail(); }
  bool enter(char c) {
    if (bad_ || depth_ >= 16 || !take(c)) { fail(); return false; }
    ++depth_;
    return true;
  }
  bool word(std::string_view w) {
    space();
    if (s_.substr(i_, w.size()) != w) return false;
    i_ += w.size();
    return true;
  }
  bool hex4(uint32_t& out) {
    if (s_.size() - i_ < 4) return false;
    out = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      out = out * 16 + static_cast<uint32_t>(d);
    }
    return true;
  }
  void escape(std::string& out) {
    uint32_t c = 0;
    if (!hex4(c)) return fail();
    if (c >= 0xD800 && c <= 0xDBFF && s_.substr(i_, 2) == "\\u") {  // A surrogate pair
      const size_t back = i_;
      uint32_t low = 0;
      i_ += 2;
      if (!hex4(low)) return fail();
      if (low >= 0xDC00 && low <= 0xDFFF) c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
      else i_ = back;
    }
    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;  // python escapes a bad byte as a lone surrogate
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }

  std::string_view s_;
  size_t i_ = 0;
  int depth_ = 0;
  bool bad_ = false;
};

}  // namespace

wsl_view read_wsl(std::string_view json) {
  wsl_view v;
  if (json.empty()) return v;
  reader r(json);
  bool has_cpu = false, has_memory = false;
  r.object([&](const std::string& key) {
    if (key == "schema") r.whole(v.schema);
    else if (key == "distro") r.text(v.distro);
    else if (key == "busy_pct") { r.number(v.busy_pct); has_cpu = true; }
    else if (key == "steal_pct") r.number(v.steal_pct);
    else if (key == "core_pct") r.array([&] { r.number(v.core_pct.emplace_back()); });
    else if (key == "cpu_split") {
      if (!r.null()) r.object([&](const std::string& k) {
        if (k == "user") r.number_or_null(v.cpu_user);
        else if (k == "sys") r.number_or_null(v.cpu_sys);
        else if (k == "iowait") r.number_or_null(v.cpu_iowait);
        else r.skip();
      });
    } else if (key == "load") {
      size_t n = 0;
      if (!r.null()) r.array([&] {
        std::optional<double> minutes;
        r.number_or_null(minutes);
        if (n < v.load.size()) v.load[n++] = minutes;
      });
    }
    else if (key == "process_count") r.whole(v.process_count);
    else if (key == "dxg_denied") r.whole(v.dxg_denied);
    else if (key == "dxg_pids") r.array([&] { r.whole(v.dxg_pids.emplace_back()); });
    else if (key == "memory") {
      has_memory = true;
      r.object([&](const std::string& k) {
        if (k == "total") r.whole(v.mem_total);
        else if (k == "available") r.whole(v.mem_available);
        else if (k == "cached") r.whole(v.mem_cached);
        else if (k == "swap_used") r.whole(v.swap_used);
        else r.skip();
      });
    } else if (key == "pressure") {
      r.object([&](const std::string& k) {
        if (k == "cpu") r.number_or_null(v.psi_cpu);
        else if (k == "memory") r.number_or_null(v.psi_memory);
        else if (k == "io") r.number_or_null(v.psi_io);
        else r.skip();
      });
    } else if (key == "processes") {
      r.array([&] {
        wsl_process p;
        r.object([&](const std::string& k) {
          if (k == "pid") r.whole(p.pid);
          else if (k == "name") r.text(p.name);
          else if (k == "state") r.text(p.state);
          else if (k == "cores") r.number(p.cores);
          else if (k == "rss") r.whole(p.rss);
          else if (k == "on_gpu") r.boolean(p.on_gpu);
          else if (k == "threads") r.whole_or_null(p.threads);
          else if (k == "rated") { if (!r.null()) r.boolean(p.rated); }
          else if (k == "start") r.whole_or_null(p.start);
          else r.skip();
        });
        v.processes.push_back(std::move(p));
      });
    } else {
      r.skip();
    }
  });
  v.ok = !r.bad() && r.at_end() && v.schema == 1 && has_cpu && has_memory;
  return v;
}

}  // namespace wwt
