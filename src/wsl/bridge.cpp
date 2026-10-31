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

}  // namespace

wsl_bridge::wsl_bridge(std::wstring script_path, std::wstring distro)
    : script_path_(std::move(script_path)), distro_(std::move(distro)) {
  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
  limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limit, sizeof limit);
  thread_ = std::thread([this] { run(); });
}

wsl_bridge::~wsl_bridge() {
  stop_ = true;
  TerminateJobObject(job_, 0);
  thread_.join();
  CloseHandle(job_);
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
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE in_r, in_w, out_r, out_w;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
  if (!CreatePipe(&out_r, &out_w, &sa, 1 << 20)) { CloseHandle(in_r); CloseHandle(in_w); return false; }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = nul;
  PROCESS_INFORMATION pi{};
  std::wstring cmdline = L"wsl.exe ";
  if (!distro_.empty()) {
    if (distro_.find_first_of(L"\"\r\n") != std::wstring::npos) { CloseHandle(in_w); CloseHandle(out_r); return false; }
    cmdline += L"-d \"" + distro_ + L"\" ";
  }
  cmdline += L"-e python3 -u -c \"" + std::wstring(kBootstrap) + L"\"";
  std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
  cmd.push_back(0);
  const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                 nullptr, nullptr, &si, &pi);
  CloseHandle(in_r);
  CloseHandle(out_w);
  CloseHandle(nul);
  if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return false; }
  AssignProcessToJobObject(job_, pi.hProcess);
  ResumeThread(pi.hThread);
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
  std::string pending;
  std::vector<char> buf(1 << 16);
  DWORD n = 0;
  while (ReadFile(out_r, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0) {
    pending.append(buf.data(), n);
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1)) {
      std::string line = pending.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line.front() != '{') continue;
      if (line.compare(0, 8, "{\"pong\":") == 0) { on_pong(line, windows_unix_s(), seconds_now()); continue; }
      got_data = true;
      const std::lock_guard<std::mutex> lock(mu_);
      line_ = std::move(line);
      line_t_ = seconds_now();
    }
  }
  done = true;
  pinger.join();
  CloseHandle(in_w);
  CloseHandle(out_r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return got_data;
}

}  // namespace wwt
