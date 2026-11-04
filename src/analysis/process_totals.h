// The Windows whole that a process row is part of, for the part-of-whole histories:
// docs/design/panel-grammar-slice2.md, section 3. Header-only, so render_check and lost_check, each built
// alone, can fill hand-built snapshots with it.
#pragma once
#include "core/snapshot.h"
#include <limits>

namespace wwt {

// DERIVED from snapshot.processes alone: one side's own figures, never a guest figure, and never PDH busy,
// which process cores do not account for (docs/measurable.md, "Process cores account for Windows busy": No).
// Summed over exactly the rows the lists show, pid 0 left out as they leave it out, in the same reading.
// Each term rests on the rows' own ledger rows: "Windows collector checked against Windows", "Windows
// per-process working set and private bytes are `Get-Process` ...", "Windows loses work to the VM".
inline process_totals_view process_totals_of(const snapshot& s) {
  process_totals_view out;
  if (s.processes.empty()) return out;  // A failed read clears the list: the whole is unknown, not 0
  out.known = true;
  double cores = 0;
  bool any_cores = false;
  for (const process_row& r : s.processes) {
    if (r.pid == 0) continue;
    out.working_set += r.working_set;
    if (!r.cores_known) continue;  // A first reading's 0 is unmeasured, so it is not a term
    cores += r.cores;
    any_cores = true;
  }
  out.cores = any_cores ? cores : std::numeric_limits<double>::quiet_NaN();
  return out;
}

}  // namespace wwt
