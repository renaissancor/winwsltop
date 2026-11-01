#include "wsl/bridge.h"
#include "wsl/collector_embed.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace wwt {
namespace {

std::string read_file(const std::wstring& path) {
  std::string out;
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return out;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

double windows_unix_s() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<double>(u.QuadPart - 116444736000000000ULL) / 1e7;
}

// Reads a length line, then that many bytes of script, then runs it. stdin stays open for pings.
// Single quotes only: wsl.exe hands the argument to python3 without a shell.
const wchar_t kBootstrap[] =
    L"import sys;n=int(sys.stdin.buffer.readline());exec(compile(sys.stdin.buffer.read(n),'collector','exec'))";

constexpr size_t kWindow = 30;
constexpr double kStepS = 0.05;
// A collector line is a few kilobytes. Unterminated output beyond this is dropped, so a child that
// never writes a newline cannot grow the buffer without bound (docs/TODO.md item 17).
constexpr size_t kMaxPending = 1 << 20;

// wsl.exe by full path only: a bare name would also search the executable's folder and the current
// directory. Empty when System32 cannot be resolved; there is no fallback to a search.
std::wstring system_wsl() {
  wchar_t dir[MAX_PATH];
  const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return std::wstring(dir, n) + L"\\wsl.exe";
}

}  // namespace

wsl_bridge::wsl_bridge(std::wstring script_path, std::wstring distro)
    : script_path_(std::move(script_path)), distro_(std::move(distro)) {
  job_ = CreateJobObjectW(nullptr, nullptr);
  if (job_) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limit, sizeof limit);
  }
  thread_ = std::thread([this] { run(); });
}

// The read loop ends only when the child's stdout closes, so the child must die for join() to
// return. The job object does that when it exists and the child was assigned to it; the process
// handle kept in child_ covers a null job or a failed assignment (docs/TODO.md item 3).
wsl_bridge::~wsl_bridge() {
  stop_ = true;
  if (job_) TerminateJobObject(job_, 0);
  { const std::lock_guard<std::mutex> lock(mu_); if (child_) TerminateProcess(child_, 0); }
  thread_.join();
  if (job_) CloseHandle(job_);
}

bool wsl_bridge::wait_first_line(double seconds) {
  const double until = seconds_now() + seconds;
  while (seconds_now() < until) {
    { const std::lock_guard<std::mutex> lock(mu_); if (!line_.empty()) return true; }
    Sleep(50);
  }
  return false;
}

std::string wsl_bridge::latest(double& age_s, int& restarts) const {
  const std::lock_guard<std::mutex> lock(mu_);
  age_s = line_.empty() ? -1.0 : seconds_now() - line_t_;
  restarts = started_ > 0 ? started_ - 1 : 0;
  return line_;
}

bool wsl_bridge::drift_locked(double& s_per_s) const {
  if (window_.size() < 5 || window_.back().t - window_.front().t < 3.0) return false;
  double mt = 0, mo = 0;
  for (const sample& x : window_) { mt += x.t; mo += x.offset; }
  mt /= window_.size();
  mo /= window_.size();
  double num = 0, den = 0;
  for (const sample& x : window_) { num += (x.t - mt) * (x.offset - mo); den += (x.t - mt) * (x.t - mt); }
  s_per_s = num / den;
  return true;
}

bool wsl_bridge::clock_drift(double& s_per_s) const {
  const std::lock_guard<std::mutex> lock(mu_);
  return drift_locked(s_per_s);
}

bool wsl_bridge::clock_offset(double& offset_s, double* rtt_s) const {
  const std::lock_guard<std::mutex> lock(mu_);
  if (window_.empty()) return false;
  const sample* best = &window_.front();
  for (const sample& x : window_) if (x.rtt < best->rtt) best = &x;
  double drift = 0;
  if (!drift_locked(drift)) drift = 0;
  offset_s = best->offset + drift * (windows_unix_s() - best->t);
  if (rtt_s) *rtt_s = best->rtt;
  return true;
}

int wsl_bridge::clock_steps() const {
  const std::lock_guard<std::mutex> lock(mu_);
  return steps_;
}

// {"pong":seq,"u2":wsl unix at receipt,"d":hold seconds on a monotonic clock,"wm":wall minus monotonic}
void wsl_bridge::on_pong(const std::string& line, double t4_unix, double q4) {
  auto field = [&](const char* key, double& v) {
    const size_t at = line.find(key);
    if (at == std::string::npos) return false;
    v = atof(line.c_str() + at + strlen(key));
    return true;
  };
  double seq, u2, d, wm;
  if (!field("\"pong\":", seq) || !field("\"u2\":", u2) || !field("\"d\":", d) || !field("\"wm\":", wm)) return;
  const std::lock_guard<std::mutex> lock(mu_);
  while (!sent_.empty() && sent_.front().seq < static_cast<long long>(seq)) sent_.pop_front();
  if (sent_.empty() || sent_.front().seq != static_cast<long long>(seq)) return;
  const ping_sent p = sent_.front();
  sent_.pop_front();
  if (have_wm_ && std::abs(wm - last_wm_) > kStepS) { window_.clear(); ++steps_; }
  have_wm_ = true;
  last_wm_ = wm;
  const double rtt = (q4 - p.q) - d;
  // Midpoints: Windows (send + receive) / 2 against WSL receipt + hold / 2.
  const double offset = (p.unix_s + t4_unix) / 2 - (u2 + d / 2);
  window_.push_back({(p.unix_s + t4_unix) / 2, offset, rtt});
  if (window_.size() > kWindow) window_.pop_front();
}

void wsl_bridge::run() {
  std::string script = read_file(script_path_);
  if (script.empty()) {
    size_t n = 0;
    const char* p = embedded_collector(n);
    script.assign(p, n);
  }
  double backoff = 2.0;
  while (!stop_) {
    const bool got_data = run_once(script);
    if (stop_) break;
    backoff = got_data ? 2.0 : (backoff * 2 > 30.0 ? 30.0 : backoff * 2);
    for (double t = 0; t < backoff && !stop_; t += 0.1) Sleep(100);
  }
}

bool wsl_bridge::run_once(const std::string& script) {
  const std::wstring app = system_wsl();
  if (app.empty()) return false;  // Handled exactly as a failed CreateProcessW.
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE in_r, in_w, out_r, out_w;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
  if (!CreatePipe(&out_r, &out_w, &sa, 1 << 20)) { CloseHandle(in_r); CloseHandle(in_w); return false; }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
  // Without NUL the child gets no stderr handle at all, never INVALID_HANDLE_VALUE (docs/TODO.md item 18).
  if (nul == INVALID_HANDLE_VALUE) nul = nullptr;

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = nul;
  PROCESS_INFORMATION pi{};
  std::wstring cmdline = L"wsl.exe ";
  if (!distro_.empty()) {
    if (distro_.find_first_of(L" \t\"\r\n") != std::wstring::npos) { CloseHandle(in_r); CloseHandle(in_w); CloseHandle(out_r); CloseHandle(out_w); if (nul) CloseHandle(nul); return false; }
    cmdline += L"-d " + distro_ + L" ";  // Distro names have no spaces; a quoted name was rejected by wsl.exe.
  }
  // -I: `-c` would put the inherited current directory first on sys.path, so a json.py there would
  // run in the guest in place of the standard library.
  cmdline += L"-e python3 -I -u -c \"" + std::wstring(kBootstrap) + L"\"";
  std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
  cmd.push_back(0);
  const BOOL ok = CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                 nullptr, nullptr, &si, &pi);
  CloseHandle(in_r);
  CloseHandle(out_w);
  if (nul) CloseHandle(nul);
  if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return false; }
  { const std::lock_guard<std::mutex> lock(mu_); child_ = pi.hProcess; }
  // A failed assignment (or a null job_) is not fatal: the child then lives outside the job and
  // child_ is what the destructor kills (docs/TODO.md item 3).
  if (!AssignProcessToJobObject(job_, pi.hProcess)) { /* covered by child_ */ }
  ResumeThread(pi.hThread);
  // The destructor may have run between CreateProcessW and here, before child_ was set and before
  // the child was in the job: then neither kill reached it, so finish the stop here.
  if (stop_) TerminateProcess(pi.hProcess, 0);
  { const std::lock_guard<std::mutex> lock(mu_); ++started_; }

  { const std::lock_guard<std::mutex> lock(mu_); sent_.clear(); window_.clear(); have_wm_ = false; }
  const std::string header = std::to_string(script.size()) + "\n";
  DWORD wrote = 0;
  WriteFile(in_w, header.data(), static_cast<DWORD>(header.size()), &wrote, nullptr);
  WriteFile(in_w, script.data(), static_cast<DWORD>(script.size()), &wrote, nullptr);

  // Pings: a quick burst for a first estimate, then one a second. The collector exits when stdin closes.
  std::atomic<bool> done{false};
  std::thread pinger([&] {
    for (int i = 0; !done && !stop_; ++i) {
      std::string msg;
      {
        const std::lock_guard<std::mutex> lock(mu_);
        msg = "P " + std::to_string(++seq_) + "\n";
        sent_.push_back({seq_, windows_unix_s(), seconds_now()});
        while (sent_.size() > 8) sent_.pop_front();
      }
      DWORD w = 0;
      if (!WriteFile(in_w, msg.data(), static_cast<DWORD>(msg.size()), &w, nullptr)) break;
      const int wait_ms = i < 5 ? 200 : 1000;
      for (int t = 0; t < wait_ms && !done && !stop_; t += 20) Sleep(20);
    }
  });

  bool got_data = false;
  bool discarding = false;  // inside an oversized line: skip up to its newline (docs/TODO.md item 17)
  std::string pending;
  std::vector<char> buf(1 << 16);
  DWORD n = 0;
  while (ReadFile(out_r, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0) {
    pending.append(buf.data(), n);
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1)) {
      if (discarding) { discarding = false; continue; }  // the remainder of a dropped oversized line
      std::string line = pending.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line.front() != '{') continue;
      if (line.compare(0, 8, "{\"pong\":") == 0) { on_pong(line, windows_unix_s(), seconds_now()); continue; }
      got_data = true;
      const std::lock_guard<std::mutex> lock(mu_);
      line_ = std::move(line);
      line_t_ = seconds_now();
    }
    // No newline in 1 MiB: not a protocol line. Drop it, and skip the rest of that line as well, so
    // its tail is never read as a fresh line (docs/TODO.md item 17).
    if (pending.size() > kMaxPending) { pending.clear(); discarding = true; }
  }
  done = true;
  pinger.join();
  // The collector is gone: forget its clock samples now, not when the next one starts, so
  // clock_offset() is false during the backoff instead of extrapolating from a dead collector
  // (docs/TODO.md item 7).
  { const std::lock_guard<std::mutex> lock(mu_); window_.clear(); sent_.clear(); have_wm_ = false; }
  CloseHandle(in_w);
  CloseHandle(out_r);
  { const std::lock_guard<std::mutex> lock(mu_); child_ = nullptr; }  // Cleared under mu_ before the close, so the destructor never terminates a closed handle.
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return got_data;
}

}  // namespace wwt
