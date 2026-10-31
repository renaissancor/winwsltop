#include "core/collectors.h"
#include "core/pdh_array.h"
#include <cwchar>
#include <map>
#pragma comment(lib, "pdh.lib")

namespace wwt {

disk_collector::disk_collector() {
  if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) { query_ = nullptr; return; }
  PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &read_);
  PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &write_);
  PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(*)\\% Idle Time", 0, &idle_);
}

disk_collector::~disk_collector() {
  if (query_) PdhCloseQuery(query_);
}

bool disk_collector::sample(std::vector<disk_row>& out) {
  if (!query_ || !read_ || !write_ || !idle_) return false;
  if (PdhCollectQueryData(query_) != ERROR_SUCCESS) return false;
  if (!primed_) { primed_ = true; return false; }

  std::map<std::wstring, disk_row> rows;
  std::vector<unsigned char> buf;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
  DWORD count = 0;
  const auto read_into = [&](PDH_HCOUNTER c, auto set) {
    if (!pdh_array(c, buf, items, count)) return;
    for (DWORD i = 0; i < count; ++i) {
      if (items[i].FmtValue.CStatus != ERROR_SUCCESS || wcscmp(items[i].szName, L"_Total") == 0) continue;
      set(rows[items[i].szName], items[i].FmtValue.doubleValue);
    }
  };
  read_into(read_, [](disk_row& r, double v) { r.read_bytes_s = v; });
  read_into(write_, [](disk_row& r, double v) { r.write_bytes_s = v; });
  read_into(idle_, [](disk_row& r, double v) { r.busy_pct = v > 100.0 ? 0.0 : 100.0 - v; });

  out.clear();
  for (auto& [name, row] : rows) {
    row.name = utf8(name);
    out.push_back(std::move(row));
  }
  return true;
}

}  // namespace wwt
