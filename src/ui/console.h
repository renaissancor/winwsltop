// Puts rendered frames on the Windows console. With stdout redirected it writes plain text instead.
#pragma once
#include "ui/render.h"
#include <vector>

namespace wwt {

class console {
 public:
  console();   // On a console: alternate screen, cursor hidden, VT on, Ctrl+C caught, no input echo
  ~console();  // Puts all of that back
  console(const console&) = delete;
  console& operator=(const console&) = delete;

  bool is_terminal() const { return terminal_; }
  bool size(int& width, int& height) const;  // false when stdout is not a console
  void draw(const std::vector<line>& lines);
  // Ctrl+C or a closing window asked to end. Poll it, so that the destructor runs.
  static bool stop_requested();
  // The next key as a char, or 0: no key waiting, no console, or a key with no ASCII character.
  // Never blocks. Reads CONIN$, so it works when stdin is not the console.
  char poll_key();

 private:
  void* out_ = nullptr;
  void* in_ = nullptr;  // CONIN$, null without a console
  unsigned long saved_mode_ = 0, saved_in_mode_ = 0;
  bool terminal_ = false;
};

}  // namespace wwt
