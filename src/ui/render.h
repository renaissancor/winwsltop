// The terminal screen as data. render() does no I/O and reads the snapshot and, for the graphs, the
// history kept in memory from earlier snapshots, so it can be checked anywhere; console.h puts its
// result on a Windows console.
#pragma once
#include "core/snapshot.h"
#include "ui/history.h"
#include "ui/keys.h"
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

// bg256 for a selected row: machtop's selection background, 44;62;76, which no 256-colour index matches.
constexpr int kSelectBg = -2;
using line = std::vector<span>;

class history;

// The ranges a graph is drawn over, 1, 5 and 10 minutes, which ← and → step through on panels 2, 3 and 4.
constexpr int64_t kSpansMs[] = {60000, 300000, 600000};

// The eight panels of the shared grammar (docs/panels.md), by number; 0 is not one.
constexpr int kPanels = 8;

// A row of a list, remembered by pid, start and name, so that it survives a reorder (design/panel-grammar.md
// part 3, design/panel-grammar-slice2.md section 1).
struct row_key {
  bool set = false;  // Not yet moved: the first row
  uint32_t pid = 0;
  std::string name;
  // Its create time or starttime when last seen, 0 when not known: what the per-process ring keys it by, and,
  // where the row's start is known too, part of what selects it, so a reused pid is not the row selected.
  uint64_t start = 0;
};

// What the keys have chosen, and nothing else: no offset, no size, nothing per second.
struct view_state {
  int panel = 1;  // 1-8
  int range = 0;  // Index into kSpansMs: one range for the whole screen
  struct panel_view {
    int focus = 0;     // The focused list: 0 Windows, 1 WSL
    row_key rows[2];   // Each list's own selection
  } panels[kPanels + 1];
};

// Applies one key: false when it asks to quit. esc_first is esc, then the key. Pure: it reads the snapshot,
// and the history for panel 2's list, only for the lists a key moves through, in the order the panel draws them
// for the same now_ms. Panel 1 and those not built have no list; panel 2 has one, so tab does nothing there; tab
// needs two lists to move between, which a missing WSL line leaves one of.
// width is the screen's, as render gets it: panel 2's list counts a use from the first column its history draws.
// 0 counts from the start of the range, which can list a row for up to a cell whose history draws nothing.
bool apply_key(view_state& v, key_event e, const snapshot& s, const history& h, int64_t now_ms, int width = 0);

// The rows panels 2, 3 and 4 have selected, every list, as the per-process ring knows them, from the keys
// remembered, so a selected process that has left the list is still named: history::push keeps their slots,
// and it keeps its history while it is away. A list never moved in names nothing; its first row is in the
// sample, which push never takes from.
std::vector<proc_id> selected_processes(const snapshot& s, const view_state& v);

// At most `height` lines of at most `width` columns, wide characters counted as 2. Names from either
// system are made safe here: control characters and malformed UTF-8 never reach the terminal.
// now_ms is the wall clock when drawing; 0 means unknown, and the title then gives no data age.
// Without hist there are no panels: the one overall screen, v ignored and no key footer. With hist,
// the panel v.panel and a key footer as the last of the `height` rows, offering only keys that act there.
std::vector<line> render(const snapshot& s, int width, int height, int64_t now_ms = 0, const history* hist = nullptr, const view_state& v = {});

std::string as_text(const std::vector<line>& lines);

// One frame of VT sequences for a screen `rows` high: every row is rewritten, the rest erased. Within
// a row equal neighbours are one colour change, and a change says only what changed.
std::string vt_frame(const std::vector<line>& lines, int rows);

// Terminal columns of a UTF-8 string.
int display_width(std::string_view utf8);

}  // namespace wwt
