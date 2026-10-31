#include "record/jsonl.h"
#include <cstdio>

namespace wwt {
namespace {

void str(std::string& o, const std::string& v) {
  o += '"';
  for (const char ch : v) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') { o += '\\'; o += ch; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += ch;
  }
  o += '"';
}

void num(std::string& o, double v) {
  char b[40];
  snprintf(b, sizeof b, "%.3f", v);
  o += b;
}

template <class T> void key(std::string& o, const char* k, const T& v) {
  o += '"'; o += k; o += "\":";
  if constexpr (std::is_same_v<T, double>) num(o, v);
  else if constexpr (std::is_same_v<T, std::string>) str(o, v);
  else if constexpr (std::is_same_v<T, bool>) o += v ? "true" : "false";
  else o += std::to_string(v);
  o += ',';
}

void close(std::string& o, char c) {
  if (o.back() == ',') o.pop_back();
  o += c;
}

template <class R, class F> void list(std::string& o, const char* k, const R& rows, F each) {
  o += '"'; o += k; o += "\":[";
  for (const auto& r : rows) { o += '{'; each(r); close(o, '}'); o += ','; }
  close(o, ']');
  o += ',';
}

}  // namespace

std::string to_json_line(const snapshot& s) {
  std::string o = "{";
  key(o, "schema", 1);
  key(o, "unix_ms", s.unix_ms);
  key(o, "interval_s", s.interval_s);

  o += "\"cpu\":{"; key(o, "busy_pct", s.cpu.busy_pct); o += "\"core_pct\":[";
  for (const double v : s.cpu.core_pct) { num(o, v); o += ','; }
  close(o, ']'); o += "},";

  o += "\"memory\":{";
  key(o, "total", s.memory.total); key(o, "available", s.memory.available); key(o, "cache", s.memory.cache);
  key(o, "commit_used", s.memory.commit_used); key(o, "commit_limit", s.memory.commit_limit);
  close(o, '}'); o += ',';

  o += "\"vm\":{";
  key(o, "vmmem_pid", s.vm.vmmem_pid); key(o, "vmwp_pid", s.vm.vmwp_pid);
  key(o, "vmmem_working_set", s.vm.vmmem_working_set); key(o, "vmmem_cores", s.vm.vmmem_cores);
  close(o, '}'); o += ',';

  list(o, "processes", s.processes, [&](const process_row& r) {
    key(o, "pid", r.pid); key(o, "ppid", r.ppid); key(o, "name", r.name); key(o, "cores", r.cores);
    key(o, "working_set", r.working_set); key(o, "private", r.private_bytes);
    key(o, "threads", r.threads); key(o, "handles", r.handles);
  });
  list(o, "disks", s.disks, [&](const disk_row& r) {
    key(o, "name", r.name); key(o, "read_bytes_s", r.read_bytes_s);
    key(o, "write_bytes_s", r.write_bytes_s); key(o, "busy_pct", r.busy_pct);
  });
  list(o, "nets", s.nets, [&](const net_row& r) {
    key(o, "name", r.name); key(o, "in_bytes_s", r.in_bytes_s); key(o, "out_bytes_s", r.out_bytes_s);
    key(o, "link_bits_s", r.link_bits_s); key(o, "hardware", r.hardware);
  });
  list(o, "gpu_engines", s.gpu_engines, [&](const gpu_engine_row& r) {
    key(o, "pid", r.pid); key(o, "engine", r.engine); key(o, "pct", r.pct);
    key(o, "source", std::string("pdh"));
  });

  const gpu_device_view& g = s.gpu_device;
  o += "\"gpu_device\":{";
  key(o, "present", g.present); key(o, "name", g.name);
  if (g.has_mem) { key(o, "mem_used", g.mem_used); key(o, "mem_total", g.mem_total); }
  if (g.has_util) key(o, "util_pct", g.util_pct);
  if (g.has_power) { key(o, "power_w", g.power_w); key(o, "power_limit_w", g.power_limit_w); }
  if (g.has_temp) key(o, "temp_c", g.temp_c);
  if (g.has_clock) key(o, "sm_mhz", g.sm_mhz);
  close(o, '}'); o += ',';

  list(o, "gpu_memory", s.gpu_memory, [&](const gpu_memory_row& r) {
    key(o, "pid", r.pid); key(o, "dedicated", r.dedicated); key(o, "shared", r.shared); key(o, "local", r.local);
  });

  o += "\"gpu_split\":{";
  key(o, "valid", s.gpu_split.valid); key(o, "vm_pct", s.gpu_split.vm_pct); key(o, "windows_pct", s.gpu_split.windows_pct);
  key(o, "vm_local", s.gpu_split.vm_local); key(o, "windows_local", s.gpu_split.windows_local);
  key(o, "source", std::string("derived"));
  close(o, '}'); o += ',';

  o += "\"wsl\":{";
  key(o, "age_s", s.wsl_age_s); key(o, "restarts", s.wsl_restarts);
  key(o, "clock_known", s.wsl_clock_known); key(o, "clock_offset_s", s.wsl_clock_offset_s);
  o += "\"data\":"; o += s.wsl_json.empty() ? "null" : s.wsl_json;
  o += "},";

  o += "\"cost_ms\":{";
  key(o, "cpu", s.cost.cpu_ms); key(o, "memory", s.cost.memory_ms); key(o, "processes", s.cost.processes_ms);
  key(o, "disk", s.cost.disk_ms); key(o, "net", s.cost.net_ms); key(o, "gpu", s.cost.gpu_ms);
  key(o, "gpu_device", s.cost.gpu_device_ms); key(o, "gpu_memory", s.cost.gpu_memory_ms);
  close(o, '}');
  o += '}';
  return o;
}

}  // namespace wwt
