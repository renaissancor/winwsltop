// Which key the screen was given, from one Windows key record. Header-only and pure, so scripts/msvc.cmd
// builds nothing new and render_check runs it on Linux too. docs/design/panel-grammar.md part 2; the keys
// follow machtop's (src/keys.cpp on grammar-tab), which reads bytes where this reads key records.
#pragma once
#include <cstdint>

namespace wwt {

enum class key : uint8_t { none, panel, up, down, left, right, tab, esc, quit, sort };

struct key_event {
  key k = key::none;
  uint8_t panel = 0;       // 1-8, for key::panel
  bool esc_first = false;  // Alt was held: esc, then this key, as machtop reads a terminal's ESC then the key
};

// The virtual key codes and dwControlKeyState bits read here, with windows.h's values; console.cpp checks
// that they agree. Kept here so that this header needs no windows.h.
namespace vk {
constexpr uint16_t tab = 0x09, shift = 0x10, control = 0x11, menu = 0x12, capital = 0x14, escape = 0x1B;
constexpr uint16_t left = 0x25, up = 0x26, right = 0x27, down = 0x28;
constexpr uint16_t lwin = 0x5B, rwin = 0x5C, numpad0 = 0x60, numlock = 0x90, scroll = 0x91;
constexpr uint16_t lshift = 0xA0, rmenu = 0xA5, processkey = 0xE5;
}  // namespace vk
constexpr uint32_t kRightAlt = 0x0001, kLeftAlt = 0x0002, kRightCtrl = 0x0004, kLeftCtrl = 0x0008, kShift = 0x0010;

// The 2-set Korean layout, machtop's table: the jamo each Latin key types while Korean input is on, so that
// q quits and s sorts either way. Index is the letter.
inline constexpr char32_t kJamo[26] = {
    0x3141, 0x3160, 0x314A, 0x3147, 0x3137, 0x3139, 0x314E, 0x3157, 0x3151, 0x3153, 0x314F, 0x3163, 0x3161,  // a-m
    0x315C, 0x3150, 0x3154, 0x3142, 0x3131, 0x3134, 0x3145, 0x3155, 0x314D, 0x3148, 0x314C, 0x315B, 0x314B,  // n-z
};

// The Latin letter a jamo is typed with, or 0.
inline char latin_for(char32_t c) {
  for (int i = 0; i < 26; ++i) {
    if (kJamo[i] == c) return static_cast<char>('a' + i);
  }
  return 0;
}

// What a lower-case letter or a digit does: 1-8 a panel, q quit, s sort. Nothing else, t included.
inline key_event key_of_char(char c) {
  if (c >= '1' && c <= '8') return {key::panel, static_cast<uint8_t>(c - '0')};
  if (c == 'q') return {key::quit};
  if (c == 's') return {key::sort};
  return {};
}

// The virtual key decides first. Only when it gives nothing, as under an IME that reports VK_PROCESSKEY,
// does the character, through the jamo table. Ctrl held is no key: Ctrl+C is the console's, and AltGr
// arrives as Ctrl+Alt. Shift is allowed, since some layouts need it for the digits, except with Tab:
// Shift+Tab is machtop's ESC [ Z, which its parser drops whole. Alt with a key that types something is esc
// then that key, as a terminal sends it; with an arrow it is the arrow, which a terminal sends as one
// sequence; with numpad digits it is Windows' Alt code entry, and nothing.
inline key_event key_for(uint16_t code, char32_t ch, uint32_t modifiers) {
  if (modifiers & (kLeftCtrl | kRightCtrl)) return {};
  const bool alt = (modifiers & (kLeftAlt | kRightAlt)) != 0, shift = (modifiers & kShift) != 0;
  const bool numpad = code > vk::numpad0 && code <= vk::numpad0 + 9;
  key_event e;
  switch (code) {
    case vk::up: return {key::up};
    case vk::down: return {key::down};
    case vk::left: return {key::left};
    case vk::right: return {key::right};
    case vk::tab:
      if (shift) return {};
      e.k = key::tab;
      break;
    case vk::escape: e.k = key::esc; break;
    case vk::shift: case vk::control: case vk::menu: case vk::capital: case vk::lwin: case vk::rwin: case vk::numlock: case vk::scroll:
      return {};  // A modifier on its own, or a lock: no key yet
    default:
      if (code >= vk::lshift && code <= vk::rmenu) return {};
      if (numpad && alt) return {};
      if (numpad) e = key_of_char(static_cast<char>('0' + (code - vk::numpad0)));
      else if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) e = key_of_char(static_cast<char>(code >= 'A' ? code - 'A' + 'a' : code));
      if (e.k == key::none) e = key_of_char(latin_for(ch));
      if (e.k == key::none) {
        // Alt with a key that types a character is esc, then a key that does nothing
        const bool types = ch != 0 || (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z');
        return alt && types ? key_event{key::esc} : key_event{};
      }
  }
  e.esc_first = alt;
  return e;
}

}  // namespace wwt
