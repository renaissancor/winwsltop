// The terminal screen as data. render() does no I/O and reads the snapshot and, for the graphs, the
// history kept in memory from earlier snapshots, so it can be checked anywhere; console.h puts its
// result on a Windows console.
#pragma once
#include "core/snapshot.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wwt {

// Grey text, one steel hue for bars, red to act, amber to watch. Colours live in vt_frame(), except the
// heat ramp, which is 256-colour indexes in the two fields below.
enum class tone : uint8_t { text, title, dim, faint, bar, track, watch, act };

struct span {
  std::string text;  // UTF-8
  tone color = tone::text;
  bool bold = false;
  int fg256 = -1, bg256 = -1;  // A 256-colour index, or -1: the tone's colour, the terminal's background
};
using line = std::vector<span>;

class history;

// The panels of the live screen. render() knows nothing of keys: its caller maps them to a view.
enum class view : uint8_t { overall, gpu, cpu };

// At most `height` lines of at most `width` columns, wide characters counted as 2. Names from either
// system are made safe here: control characters and malformed UTF-8 never reach the terminal.
// now_ms is the wall clock when drawing; 0 means unknown, and the title then gives no data age.
// Without hist there are no panels: the one overall screen, v ignored and no key footer. With hist,
// the panel v and a key footer as the last of the `height` rows.
std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms = 0, const history* hist = nullptr,
                         view v = view::overall);

std::string as_text(const std::vector<line>& lines);

// One frame of VT sequences for a screen `rows` high: every row is rewritten, the rest erased. Within
// a row equal neighbours are one colour change, and a change says only what changed.
std::string vt_frame(const std::vector<line>& lines, int rows);

// Terminal columns of a UTF-8 string.
int display_width(std::string_view utf8);

}  // namespace wwt
