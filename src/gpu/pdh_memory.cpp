#include "core/collectors.h"
#include "core/pdh_array.h"
#include <cwchar>
#include <map>
#pragma comment(lib, "pdh.lib")

namespace wwt {

gpu_memory_collector::gpu_memory_collector() {
  if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) { query_ = nullptr; return; }
  if (PdhAddEnglishCounterW(query_, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &dedicated_) != ERROR_SUCCESS) dedicated_ = nullptr;
  if (PdhAddEnglishCounterW(query_, L"\\GPU Process Memory(*)\\Shared Usage", 0, &shared_) != ERROR_SUCCESS) shared_ = nullptr;
  if (PdhAddEnglishCounterW(query_, L"\\GPU Process Memory(*)\\Local Usage", 0, &local_) != ERROR_SUCCESS) local_ = nullptr;
}

gpu_memory_collector::~gpu_memory_collector() {
  if (query_) PdhCloseQuery(query_);
}

namespace {

// Instance: pid_1234_luid_0x..._phys_0. One process can have one instance per adapter.
template <class Field>
void add_rows(const PDH_FMT_COUNTERVALUE_ITEM_W* items, DWORD count, std::map<uint32_t, gpu_memory_row>& rows, Field field) {
  for (DWORD i = 0; i < count; ++i) {
    if (items[i].FmtValue.CStatus != ERROR_SUCCESS || items[i].FmtValue.doubleValue <= 0.0) continue;
    const wchar_t* pid_at = wcsstr(items[i].szName, L"pid_");
    if (!pid_at) continue;
    const uint32_t pid = static_cast<uint32_t>(_wtoi(pid_at + 4));
    gpu_memory_row& row = rows[pid];
    row.pid = pid;
    row.*field += static_cast<uint64_t>(items[i].FmtValue.doubleValue);
  }
}

}  // namespace

// These are gauges, so unlike GPU Engine the first collection already gives values.
// Processes with nothing on either counter are left out. Dedicated is not VRAM in use: summed over
// processes it reached three times NVML's used figure, see docs/measurable.md.
// out is cleared first, so a failed read leaves it empty, not the previous sample (docs/TODO.md item 1).
bool gpu_memory_collector::sample(std::vector<gpu_memory_row>& out) {
  out.clear();
  if (!query_ || !dedicated_ || !shared_ || !local_) return false;
  if (PdhCollectQueryData(query_) != ERROR_SUCCESS) return false;

  std::vector<unsigned char> buf;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
  DWORD count = 0;
  std::map<uint32_t, gpu_memory_row> rows;
  if (!pdh_array(dedicated_, buf, items, count)) return false;
  add_rows(items, count, rows, &gpu_memory_row::dedicated);
  if (!pdh_array(shared_, buf, items, count)) return false;
  add_rows(items, count, rows, &gpu_memory_row::shared);
  if (!pdh_array(local_, buf, items, count)) return false;
  add_rows(items, count, rows, &gpu_memory_row::local);

  for (const auto& [pid, row] : rows) out.push_back(row);
  return true;
}

}  // namespace wwt
