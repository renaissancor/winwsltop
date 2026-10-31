// Puts rendered frames on the Windows console. With stdout redirected it writes plain text instead.
#pragma once
#include "ui/render.h"
#include <vector>

namespace wwt {

class console {
 public:
  console();   // On a console: alternate screen, cursor hidden, VT on, Ctrl+C caught
  ~console();  // Puts all of that back
  console(const console&) = delete;
  console& operator=(const console&) = delete;

  bool is_terminal() const { return terminal_; }
  bool size(int& width, int& height) const;  // false when stdout is not a console
  void draw(const std::vector<line>& lines);
  // Ctrl+C or a closing window asked to end. Poll it, so that the destructor runs.
  static bool stop_requested();

 private:
  void* out_ = nullptr;
  unsigned long saved_mode_ = 0;
  bool terminal_ = false;
};

}  // namespace wwt
