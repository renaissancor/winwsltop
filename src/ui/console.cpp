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

}  // namespace

console::console() {
  out_ = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(out_, &mode)) return;
  if (!SetConsoleMode(out_, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return;
  saved_mode_ = mode;
  terminal_ = true;
  SetConsoleCtrlHandler(on_ctrl, TRUE);
  write_utf8(out_, "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");  // alternate screen, no cursor, no wrap
}

console::~console() {
  if (!terminal_) return;
  write_utf8(out_, "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
  SetConsoleMode(out_, saved_mode_);
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

}  // namespace wwt
