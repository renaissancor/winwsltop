#pragma once
#include "core/platform.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <vector>

namespace wwt {

// Reads a wildcard counter. The returned items point into buf.
inline bool pdh_array(PDH_HCOUNTER c, std::vector<unsigned char>& buf, PDH_FMT_COUNTERVALUE_ITEM_W*& items, DWORD& count) {
  const DWORD fmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
  DWORD size = 0;
  count = 0;
  if (PdhGetFormattedCounterArrayW(c, fmt, &size, &count, nullptr) != PDH_MORE_DATA) return false;
  buf.resize(size);
  items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
  return PdhGetFormattedCounterArrayW(c, fmt, &size, &count, items) == ERROR_SUCCESS;
}

}  // namespace wwt
