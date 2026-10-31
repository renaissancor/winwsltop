#include "core/collectors.h"
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#pragma comment(lib, "iphlpapi.lib")

namespace wwt {

// out is cleared first, so a failed query or the priming call leaves it empty (docs/TODO.md item 1).
bool net_collector::sample(std::vector<net_row>& out) {
  out.clear();
  MIB_IF_TABLE2* table = nullptr;
  if (GetIfTable2(&table) != NO_ERROR) return false;
  const double now_t = seconds_now();
  const double elapsed = now_t - prev_t_;
  const bool have_prev = prev_t_ != 0 && elapsed > 0;
  std::map<uint64_t, counters> now;
  for (ULONG i = 0; i < table->NumEntries; ++i) {
    const MIB_IF_ROW2& row = table->Table[i];
    if (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    if (row.InterfaceAndOperStatusFlags.FilterInterface) continue;
    const uint64_t id = row.InterfaceLuid.Value;
    now[id] = {row.InOctets, row.OutOctets};
    net_row r;
    r.name = utf8(row.Alias);
    r.link_bits_s = row.TransmitLinkSpeed;
    r.hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != 0;
    if (have_prev) {
      if (const auto it = prev_.find(id); it != prev_.end()) {
        r.in_bytes_s = static_cast<double>(row.InOctets - it->second.in) / elapsed;
        r.out_bytes_s = static_cast<double>(row.OutOctets - it->second.out) / elapsed;
      }
    }
    out.push_back(std::move(r));
  }
  FreeMibTable(table);
  prev_ = std::move(now);
  prev_t_ = now_t;
  if (!have_prev) out.clear();  // The priming call has no rates: it reports no rows.
  return have_prev;
}

}  // namespace wwt
