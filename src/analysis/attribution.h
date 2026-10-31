#pragma once
#include "core/snapshot.h"

namespace wwt {
// The Windows-versus-WSL split at VM level. Not per Linux process, and not exact shares.
gpu_split_view split_gpu(const snapshot& s);
}
