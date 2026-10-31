// The terminal screen as data. render() does no I/O and reads nothing but the snapshot, so it can be
// checked anywhere; console.h puts its result on a Windows console.
#pragma once
#include "core/snapshot.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wwt {

// Grey text, one steel hue for bars, red to act, amber to watch. Colours live in vt_frame().
enum class tone : uint8_t { text, title, dim, faint, bar, track, watch, act };

struct span {
  std::string text;  // UTF-8
  tone color = tone::text;
  bool bold = false;
};
using line = std::vector<span>;

// At most `height` lines of at most `width` columns, wide characters counted as 2. Names from either
// system are made safe here: control characters and malformed UTF-8 never reach the terminal.
// now_ms is the wall clock when drawing; 0 means unknown, and the title then gives no data age.
std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms = 0);

std::string as_text(const std::vector<line>& lines);

// One frame of VT sequences for a screen `rows` high: every row is rewritten, the rest erased.
std::string vt_frame(const std::vector<line>& lines, int rows);

// Terminal columns of a UTF-8 string.
int display_width(std::string_view utf8);

}  // namespace wwt
