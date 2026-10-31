#pragma once
#include <cstddef>

namespace wwt {

// The bytes of wsl-collector/collector.py as of the last run of wsl-collector/embed.py.
const char* embedded_collector(size_t& size);

}  // namespace wwt
