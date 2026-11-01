#include "analysis/lost.h"
#include "core/wsl_view.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace wwt {
namespace {

// The gate: where the evidence ends, and not a tuning choice. docs/measurable.md, "Guest busy times threads
// less `Hv VP` `% Total Run Time` tracks lost work", has every contended case at a full guest, so a partly
// busy one is not checked. A guest load that has just stopped leaves Windows idle while the means still
// hold it: docs/design/cpu-panel.md section 4 has the replay, and scripts/lost_check.cpp repeats it.
constexpr double kGuestBusyMin = 99;
constexpr double kWindowsBusyMin = 95;
// Wider than the graph's kGapMs: a loop short of CPU runs late in exactly the case this figure exists for,
// as both-win-20 in experiment 05 shows. Past a skipped second the window is stale.
constexpr double kLostGapMs = 2500;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

void lost_tracker::window::push(double v) {
  v_[next_] = v;
  next_ = (next_ + 1) % kSamples;
  count_ = std::min(count_ + 1, kSamples);
}

double lost_tracker::window::mean() const {
  if (!full()) return kNaN;
  double sum = 0;
  for (const double v : v_) sum += v;
  return sum / static_cast<double>(kSamples);
}

double lost_tracker::window::lowest() const {
  return *std::min_element(v_.begin(), v_.end());
}

void lost_tracker::clear_lost() {
  guest_busy_.clear();
  guest_cores_.clear();
  vm_cores_.clear();
}

void lost_tracker::update(snapshot& s) {
  const bool gap = !(s.interval_s * 1000.0 <= kLostGapMs);
  const bool new_vm = s.vm.vmmem_pid != vm_pid_;
  vm_pid_ = s.vm.vmmem_pid;
  if (gap) ready_.clear();
  if (gap || new_vm) clear_lost();
  if (gap || new_vm || !s.vm.vmmem_cores_known) vm_ready_.clear();  // A reused pid is a new VM

  // Derived. std::max would turn a NaN into 0, so each is guarded rather than clamped blind.
  cpu_derived_view& d = s.cpu_derived;
  const size_t logical = s.cpu.core_pct.size();
  d.vm_cpu_pct = s.vm.vmmem_cores_known && logical > 0 ? 100.0 * s.vm.vmmem_cores / static_cast<double>(logical) : kNaN;
  d.own_kernel_pct = std::isfinite(d.vm_cpu_pct) && std::isfinite(s.cpu.kernel_pct) ? std::max(0.0, s.cpu.kernel_pct - d.vm_cpu_pct) : kNaN;
  const auto average = [](window& w, const std::optional<uint32_t>& count) {
    if (count) w.push(static_cast<double>(*count));
    else w.clear();
    return w.mean();
  };
  d.ready_threads_mean = average(ready_, s.ready_threads);
  d.vm_ready_threads_mean = average(vm_ready_, s.vm_ready_threads);

  // Inferred. A repeat re-reads the line it repeats, which is how it carries the guest's last value.
  s.lost = lost_view{};
  const bool repeated = wsl_line_repeats(s.wsl_json, last_line_);
  repeats_ = repeated ? repeats_ + 1 : 0;
  const wsl_view w = read_wsl(s.wsl_json);
  const bool line_ok = w.ok && !w.core_pct.empty() && std::isfinite(w.busy_pct) && s.wsl_age_s >= 0 && s.wsl_age_s < kStaleS;
  const bool windows_ok = logical > 0 && std::isfinite(s.cpu.busy_pct);
  const bool vm_ok = s.vm.vmmem_cores_known && std::isfinite(s.vm.vmmem_cores);
  if (!line_ok || !windows_ok || !vm_ok || repeats_ > 1) {
    clear_lost();
    s.lost.state = lost_state::no_data;
    return;
  }
  if (w.core_pct.size() != logical) {  // Every contended case had as many vCPUs as logical CPUs
    clear_lost();
    s.lost.state = lost_state::vcpus_differ;
    return;
  }
  if (repeated && guest_busy_.empty()) return;  // A value from before the reset: nothing to carry

  guest_busy_.push(w.busy_pct);
  guest_cores_.push(w.busy_pct * static_cast<double>(w.core_pct.size()) / 100.0);
  vm_cores_.push(s.vm.vmmem_cores);
  if (!guest_busy_.full()) return;
  if (guest_busy_.lowest() < kGuestBusyMin) s.lost.state = lost_state::guest_under_99;
  else if (s.cpu.busy_pct < kWindowsBusyMin) s.lost.state = lost_state::windows_under_95;
  else {
    s.lost.state = lost_state::known;
    s.lost.vcpus = std::max(0.0, guest_cores_.mean() - vm_cores_.mean());
  }
}

}  // namespace wwt
