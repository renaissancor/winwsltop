#include "ui/console.h"
#include "core/platform.h"
#include <algorithm>
#include <atomic>
#include <string>

namespace wwt {
namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI on_ctrl(DWORD) {
  g_stop = true;
  return TRUE;
}

void write_wide(HANDLE out, std::wstring_view w) {
  constexpr size_t kChunk = 16384;  // WriteConsoleW fails on very long writes
  while (!w.empty()) {
    size_t n = std::min(w.size(), kChunk);
    if (n < w.size() && IS_HIGH_SURROGATE(w[n - 1])) --n;
    DWORD done = 0;
    if (!WriteConsoleW(out, w.data(), static_cast<DWORD>(n), &done, nullptr) || done == 0) return;
    w.remove_prefix(done);
  }
}

void write_utf8(HANDLE out, std::string_view s) {
  if (s.empty()) return;
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  write_wide(out, w);
}

// The key pressed, not the character it made. Under Hangul input mode q should arrive as U+3142 with
// its usual key code, so it should work; not tried with a real IME. Delete has no character worth
// reading. Shift stays allowed, because some layouts need it for the digits.
char key_of(const KEY_EVENT_RECORD& k) {
  if (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED | LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) return 0;
  switch (k.wVirtualKeyCode) {
    case '1': case VK_NUMPAD1: return '1';
    case '2': case VK_NUMPAD2: return '2';
    case '3': case VK_NUMPAD3: return '3';
    case 'Q': return 'q';
    default: return 0;
  }
}

}  // namespace

console::console() {
  // Before any mode changes, whatever stdout is: Ctrl+C has to end in the destructor, which puts them back.
  SetConsoleCtrlHandler(on_ctrl, TRUE);

  // Input on its own: a redirected stdout does not take the keyboard away. Echo needs line input, so
  // both go; ENABLE_PROCESSED_INPUT stays so that Ctrl+C still reaches on_ctrl. VT input would turn a
  // key like Delete into text, and poll_key wants the key.
  HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  DWORD in_mode = 0;
  if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &in_mode) &&
      SetConsoleMode(in, (in_mode | ENABLE_PROCESSED_INPUT) & ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT))) {
    in_ = in;
    saved_in_mode_ = in_mode;
  } else if (in != INVALID_HANDLE_VALUE) {
    CloseHandle(in);
  }

  out_ = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(out_, &mode)) return;
  if (!SetConsoleMode(out_, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return;
  saved_mode_ = mode;
  terminal_ = true;
  write_utf8(out_, "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");  // alternate screen, no cursor, no wrap
}

console::~console() {
  if (in_) {
    SetConsoleMode(in_, saved_in_mode_);
    CloseHandle(in_);
  }
  if (terminal_) {
    write_utf8(out_, "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
    SetConsoleMode(out_, saved_mode_);
  }
  SetConsoleCtrlHandler(on_ctrl, FALSE);
}

bool console::size(int& width, int& height) const {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!terminal_ || !GetConsoleScreenBufferInfo(out_, &info)) return false;
  width = info.srWindow.Right - info.srWindow.Left + 1;
  height = info.srWindow.Bottom - info.srWindow.Top + 1;
  return true;
}

void console::draw(const std::vector<line>& lines) {
  if (terminal_) {
    int width = 0, height = static_cast<int>(lines.size());
    size(width, height);
    write_utf8(out_, vt_frame(lines, height));
    return;
  }
  const std::string text = as_text(lines);
  DWORD done = 0;
  WriteFile(out_, text.data(), static_cast<DWORD>(text.size()), &done, nullptr);
}

bool console::stop_requested() { return g_stop; }

char console::poll_key() {
  if (!in_) return 0;
  DWORD waiting = 0;
  while (GetNumberOfConsoleInputEvents(in_, &waiting) && waiting > 0) {  // Only then can a read not block
    INPUT_RECORD e;
    DWORD got = 0;
    if (!ReadConsoleInputW(in_, &e, 1, &got) || got == 0) return 0;
    if (e.EventType != KEY_EVENT || !e.Event.KeyEvent.bKeyDown) continue;  // Releases, resizes, the mouse
    if (const char key = key_of(e.Event.KeyEvent)) return key;
  }
  return 0;
}

}  // namespace wwt
