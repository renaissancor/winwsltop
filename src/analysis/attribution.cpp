#include "analysis/attribution.h"

namespace wwt {
namespace {

// WSL CUDA appears on the 3D engine, so Compute alone would miss it. Copy and video engines run
// in parallel with these and are left out. Their sum is not an exact share of the GPU.
bool counts(const std::string& engine) {
  return engine == "3D" || engine.rfind("Compute", 0) == 0;
}

}  // namespace

gpu_split_view split_gpu(const snapshot& s) {
  gpu_split_view out;
  if (s.vm.vmwp_pid == 0 || !s.gpu_engines_known) return out;
  out.valid = true;
  const auto is_vm = [&](uint32_t pid) { return pid == s.vm.vmwp_pid || pid == s.vm.vmmem_pid; };
  for (const gpu_engine_row& r : s.gpu_engines) {
    if (counts(r.engine)) (is_vm(r.pid) ? out.vm_pct : out.windows_pct) += r.pct;
  }
  for (const gpu_memory_row& r : s.gpu_memory) (is_vm(r.pid) ? out.vm_local : out.windows_local) += r.local;
  return out;
}

}  // namespace wwt
