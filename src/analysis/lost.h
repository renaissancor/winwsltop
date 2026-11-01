// The cost WSL pays when Windows wants the CPU too, which the guest cannot see, and the figures the CPU
// panel needs from more than one sample. Every reader gets these from the snapshot.
#pragma once
#include "core/snapshot.h"
#include <array>
#include <cstddef>
#include <string>

namespace wwt {

class lost_tracker {
 public:
  // Once a sample, after wsl_json is set. Writes s.lost and s.cpu_derived. The window under lost holds
  // consecutive usable samples only: anything that cannot count empties it, so it never spans a hole.
  void update(snapshot& s);

 private:
  static constexpr size_t kSamples = 5;

  class window {
   public:
    void clear() { count_ = 0; }
    void push(double v);
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == kSamples; }
    double mean() const;    // NaN until full
    double lowest() const;  // Only read when full
   private:
    std::array<double, kSamples> v_{};
    size_t count_ = 0, next_ = 0;
  };

  void clear_lost();

  window guest_busy_, guest_cores_, vm_cores_, ready_, vm_ready_;
  std::string last_line_;
  uint32_t vm_pid_ = 0;
  int repeats_ = 0;  // Repeated lines in a row
};

}  // namespace wwt
