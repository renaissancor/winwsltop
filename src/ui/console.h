// Puts rendered frames on the Windows console. With stdout redirected it writes plain text instead.
#pragma once
#include "ui/render.h"
#include <vector>

namespace wwt {

class console {
 public:
  console();   // Ctrl+C caught; on a console: alternate screen, cursor hidden, VT on, no input echo
  ~console();  // Puts all of that back
  console(const console&) = delete;
  console& operator=(const console&) = delete;

  bool is_terminal() const { return terminal_; }
  bool size(int& width, int& height) const;  // false when stdout is not a console
  void draw(const std::vector<line>& lines);
  // True for good once writing plain text to redirected stdout has failed: a pipe with no reader, a
  // full disk, no stdout at all. Nothing can reach the reader after that. A console never sets it.
  bool write_failed() const { return write_failed_; }
  // Ctrl+C or a closing window asked to end. Poll it, so that the destructor runs.
  static bool stop_requested();
  // The next key this program uses: '1', '2', '3' or 'q'. 0 when none is waiting or there is no
  // console; every other key is passed over. Keys are told by virtual key code, so q should work in
  // Hangul input mode; not tried with a real IME. Never blocks. Reads CONIN$, so it works when stdin
  // is not the console.
  char poll_key();

 private:
  void* out_ = nullptr;
  void* in_ = nullptr;  // CONIN$, null without a console
  unsigned long saved_mode_ = 0, saved_in_mode_ = 0;
  bool terminal_ = false;
  bool write_failed_ = false;
};

}  // namespace wwt
