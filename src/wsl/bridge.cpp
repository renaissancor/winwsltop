#include "wsl/bridge.h"
#include <cstdio>
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

}  // namespace

wsl_bridge::wsl_bridge(std::wstring script_path) : script_path_(std::move(script_path)) {
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

void wsl_bridge::run() {
  const std::string script = read_file(script_path_);
  if (script.empty()) return;
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
  wchar_t cmd[] = L"wsl.exe -e python3 -u -";
  const BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                 nullptr, nullptr, &si, &pi);
  CloseHandle(in_r);
  CloseHandle(out_w);
  CloseHandle(nul);
  if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return false; }
  AssignProcessToJobObject(job_, pi.hProcess);
  ResumeThread(pi.hThread);
  { const std::lock_guard<std::mutex> lock(mu_); ++started_; }

  DWORD wrote = 0;
  WriteFile(in_w, script.data(), static_cast<DWORD>(script.size()), &wrote, nullptr);
  CloseHandle(in_w);

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
      got_data = true;
      const std::lock_guard<std::mutex> lock(mu_);
      line_ = std::move(line);
      line_t_ = seconds_now();
    }
  }
  CloseHandle(out_r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return got_data;
}

}  // namespace wwt
