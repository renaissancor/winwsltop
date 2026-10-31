#include "core/collectors.h"
#include "core/pdh_array.h"
#include <cwchar>
#include <cwctype>
#include <map>
#pragma comment(lib, "pdh.lib")

namespace wwt {

gpu_engine_collector::gpu_engine_collector() {
  if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) { query_ = nullptr; return; }
  if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &util_) != ERROR_SUCCESS) util_ = nullptr;
}

gpu_engine_collector::~gpu_engine_collector() {
  if (query_) PdhCloseQuery(query_);
}

// Instance: pid_1234_luid_0x..._phys_0_eng_0_engtype_3D. Non-zero rows, summed per pid and engine
// type. Engine type does not separate Windows from WSL: WSL CUDA appears as 3D.
bool gpu_engine_collector::sample(std::vector<gpu_engine_row>& out) {
  if (!query_ || !util_) return false;
  if (PdhCollectQueryData(query_) != ERROR_SUCCESS) return false;
  if (!primed_) { primed_ = true; return false; }

  std::vector<unsigned char> buf;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
  DWORD count = 0;
  if (!pdh_array(util_, buf, items, count)) return false;

  std::map<std::pair<uint32_t, std::wstring>, double> sums;
  for (DWORD i = 0; i < count; ++i) {
    if (items[i].FmtValue.CStatus != ERROR_SUCCESS || items[i].FmtValue.doubleValue <= 0.0) continue;
    const wchar_t* pid_at = wcsstr(items[i].szName, L"pid_");
    const wchar_t* eng_at = wcsstr(items[i].szName, L"engtype_");
    if (!pid_at || !eng_at) continue;
    sums[{static_cast<uint32_t>(_wtoi(pid_at + 4)), eng_at + 8}] += items[i].FmtValue.doubleValue;
  }
  out.clear();
  for (const auto& [k, v] : sums) out.push_back({k.first, utf8(k.second), v});
  return true;
}

}  // namespace wwt
