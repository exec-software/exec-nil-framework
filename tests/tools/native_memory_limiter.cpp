// Windows-only test infrastructure, never installed in the framework SDK.
// PROCESS_MEMORY limits committed virtual memory, not reserved address space
// or resident working set. Linux lifetime tests continue to use RLIMIT_AS.
// https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr SIZE_T lifetime_limit = 96 * 1024 * 1024;
constexpr SIZE_T allocation_probe_bytes = 128 * 1024 * 1024;
constexpr std::size_t output_limit = 64 * 1024;

class handle {
public:
  explicit handle(HANDLE value = nullptr) : value_(value) {}
  ~handle() { reset(); }
  handle(const handle &) = delete;
  handle &operator=(const handle &) = delete;
  handle(handle &&other) noexcept : value_(other.release()) {}
  handle &operator=(handle &&other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  HANDLE get() const { return value_; }
  HANDLE release() { return std::exchange(value_, nullptr); }
  void reset(HANDLE value = nullptr) {
    if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    value_ = value;
  }
private:
  HANDLE value_;
};

std::string api_error(const char *operation) {
  return std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")";
}

std::wstring quote(const std::wstring &argument) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (wchar_t ch : argument) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result.push_back(ch);
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

std::wstring command_line(const std::vector<std::wstring> &arguments) {
  std::wstring result;
  for (const auto &argument : arguments) {
    if (!result.empty()) result += L' ';
    result += quote(argument);
  }
  if (result.size() >= 32767) throw std::runtime_error("command line exceeds Win32 limit");
  return result;
}

bool job_empty(HANDLE job, bool &empty, std::string &error) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                 &info, sizeof(info), nullptr)) {
    error = api_error("Job accounting");
    return false;
  }
  empty = info.ActiveProcesses == 0;
  return true;
}

bool cleanup_job(HANDLE job, HANDLE leader, std::string &error) {
  // Snapshot and retain identities before termination. Numeric PIDs are never
  // used to signal a process, and only exact Job members are inspected.
  constexpr std::size_t capacity = 4096;
  std::vector<ULONG_PTR> storage(capacity + 2);
  auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(storage.data());
  bool complete = QueryInformationJobObject(job, JobObjectBasicProcessIdList, list,
      static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR)), nullptr) != FALSE;
  complete = complete && list->NumberOfProcessIdsInList <= capacity &&
             list->NumberOfProcessIdsInList == list->NumberOfAssignedProcesses;
  std::vector<handle> members;
  if (complete) {
    for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index) {
      handle member(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                FALSE, static_cast<DWORD>(list->ProcessIdList[index])));
      BOOL belongs = FALSE;
      if (!member.get() || !IsProcessInJob(member.get(), job, &belongs) || !belongs) {
        complete = false;
        break;
      }
      members.push_back(std::move(member));
    }
  }
  const BOOL terminated = TerminateJobObject(job, 125);
  const auto deadline = clock_type::now() + 5s;
  do {
    bool signaled = WaitForSingleObject(leader, 0) == WAIT_OBJECT_0;
    for (const auto &member : members)
      signaled = (WaitForSingleObject(member.get(), 0) == WAIT_OBJECT_0) && signaled;
    bool empty = false;
    if (!job_empty(job, empty, error)) return false;
    if (signaled && empty) {
      if (!complete || !terminated) {
        error = "owned cleanup could not be completely verified";
        return false;
      }
      return true;
    }
    Sleep(10);
  } while (clock_type::now() < deadline);
  error = "owned process/descendant cleanup did not complete within 5 seconds";
  return false;
}

struct pipe_pair {
  handle read, write;
};

pipe_pair capture_pipe() {
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, &security, 0))
    throw std::runtime_error(api_error("CreatePipe"));
  pipe_pair pipe{handle(read), handle(write)};
  if (!SetHandleInformation(pipe.read.get(), HANDLE_FLAG_INHERIT, 0))
    throw std::runtime_error(api_error("SetHandleInformation"));
  return pipe;
}

bool drain(HANDLE pipe, std::string &output, std::size_t &total, bool &closed,
           std::string &error) {
  while (!closed) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) { closed = true; return true; }
      error = api_error("PeekNamedPipe");
      return false;
    }
    if (available == 0) return true;
    char buffer[4096];
    DWORD read = 0;
    if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)) {
      error = api_error("ReadFile");
      return false;
    }
    if (read > output_limit - total) {
      error = "consumer exceeded 64 KiB output cap";
      return false;
    }
    output.append(buffer, read);
    total += read;
  }
  return true;
}

struct run_result {
  DWORD code = 125;
  std::string output, stderr_output, error;
  bool timed_out = false;
  bool cleanup_confirmed = true;
};

run_result run_limited(const std::vector<std::wstring> &arguments, SIZE_T bytes,
                       std::chrono::milliseconds timeout) {
  run_result result;
  auto command = command_line(arguments);
  auto output = capture_pipe();
  auto errors = capture_pipe();
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (input.get() == INVALID_HANDLE_VALUE) throw std::runtime_error(api_error("open NUL"));
  handle job(CreateJobObjectW(nullptr, nullptr));
  if (!job.get()) throw std::runtime_error(api_error("CreateJobObjectW"));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY |
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  limits.ProcessMemoryLimit = bytes;
  if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits)))
    throw std::runtime_error(api_error("set process committed-memory cap"));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION verified{};
  if (!QueryInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                 &verified, sizeof(verified), nullptr) ||
      verified.ProcessMemoryLimit != bytes ||
      verified.BasicLimitInformation.LimitFlags != limits.BasicLimitInformation.LimitFlags)
    throw std::runtime_error("Job memory cap did not round-trip exactly");

  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  std::vector<unsigned char> attributes(attribute_bytes);
  auto *attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_bytes))
    throw std::runtime_error(api_error("InitializeProcThreadAttributeList"));
  struct attribute_guard {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~attribute_guard() { DeleteProcThreadAttributeList(value); }
  } guard{attribute_list};
  HANDLE inherited[] = {input.get(), output.write.get(), errors.write.get()};
  if (!UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr))
    throw std::runtime_error(api_error("restrict inherited handles"));
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.get();
  startup.StartupInfo.hStdOutput = output.write.get();
  startup.StartupInfo.hStdError = errors.write.get();
  startup.lpAttributeList = attribute_list;
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(arguments.front().c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW |
                           EXTENDED_STARTUPINFO_PRESENT,
                       nullptr, nullptr, &startup.StartupInfo, &child))
    throw std::runtime_error(api_error("CreateProcessW"));
  handle process(child.hProcess), thread(child.hThread);
  if (!AssignProcessToJobObject(job.get(), process.get())) {
    result.error = api_error("assign suspended consumer to limited Job");
    TerminateProcess(process.get(), 125);
    result.cleanup_confirmed = WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0;
    return result;
  }
  output.write.reset();
  errors.write.reset();
  input.reset();
  if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
    result.error = api_error("ResumeThread");
  } else {
    thread.reset();
    const auto deadline = clock_type::now() + timeout;
    auto completion_deadline = deadline;
    bool exited = false, output_closed = false, error_closed = false;
    std::size_t total = 0;
    while (true) {
      if (!drain(output.read.get(), result.output, total, output_closed, result.error) ||
          !drain(errors.read.get(), result.stderr_output, total, error_closed, result.error)) break;
      const DWORD state = WaitForSingleObject(process.get(), 0);
      if (state == WAIT_FAILED) { result.error = api_error("wait consumer"); break; }
      if (state == WAIT_OBJECT_0 && !exited) {
        exited = true;
        completion_deadline = std::min(deadline, clock_type::now() + 500ms);
      }
      bool empty = false;
      if (!job_empty(job.get(), empty, result.error)) break;
      if (exited && empty && output_closed && error_closed) {
        if (!GetExitCodeProcess(process.get(), &result.code))
          result.error = api_error("GetExitCodeProcess");
        return result;
      }
      if (clock_type::now() >= completion_deadline) {
        result.timed_out = !exited;
        result.error = exited ? "consumer left active descendants or open output pipes"
                              : "consumer execution timed out";
        break;
      }
      Sleep(5);
    }
  }
  std::string cleanup_error;
  result.cleanup_confirmed = cleanup_job(job.get(), process.get(), cleanup_error);
  if (!result.cleanup_confirmed) result.error += "; " + cleanup_error;
  return result;
}

bool allocation_succeeds() {
  void *memory = VirtualAlloc(nullptr, allocation_probe_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!memory) return false;
  if (!VirtualFree(memory, 0, MEM_RELEASE)) throw std::runtime_error(api_error("VirtualFree"));
  return true;
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size >= path.size()) throw std::runtime_error(api_error("GetModuleFileNameW"));
  path.resize(size);
  return path;
}

int self_test() {
  if (!allocation_succeeds()) throw std::runtime_error("uncapped 128 MiB positive allocation control failed");
  const auto self = self_path();
  const auto denied = run_limited({self, L"--allocation-denied"}, lifetime_limit, 5s);
  if (!denied.error.empty() || denied.code != 0 || denied.output != "allocation denied\n" ||
      !denied.stderr_output.empty() || !allocation_succeeds())
    throw std::runtime_error("96 MiB negative allocation control failed: " + denied.error);
  const auto arguments = run_limited(
      {self, L"--argument-control", L"", L"spaces café 中文", L"a\"b", L"trailing\\"}, lifetime_limit, 5s);
  if (!arguments.error.empty() || arguments.code != 0 || !arguments.output.empty() ||
      !arguments.stderr_output.empty()) throw std::runtime_error("wide argument control failed");
  const auto timeout = run_limited({self, L"--hang"}, lifetime_limit, 100ms);
  if (!timeout.timed_out || !timeout.cleanup_confirmed)
    throw std::runtime_error("timeout/owned cleanup control failed");
  const auto flood = run_limited({self, L"--flood"}, lifetime_limit, 5s);
  if (flood.error.find("output cap") == std::string::npos || !flood.cleanup_confirmed)
    throw std::runtime_error("output-cap/owned cleanup control failed");
  const auto orphan = run_limited({self, L"--orphan"}, lifetime_limit, 5s);
  if (orphan.error.find("descendants") == std::string::npos || !orphan.cleanup_confirmed)
    throw std::runtime_error("descendant/owned cleanup control failed");
  std::cout << "Windows committed-memory cap and launcher controls verified\n";
  return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  try {
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"--self-test" && argc == 2) return self_test();
    if (mode == L"--allocation-denied" && argc == 2) {
      if (allocation_succeeds()) return 1;
      std::cout << "allocation denied\n";
      return 0;
    }
    if (mode == L"--argument-control") {
      return argc == 6 && std::wstring(argv[2]).empty() && argv[3] == std::wstring(L"spaces café 中文") &&
             argv[4] == std::wstring(L"a\"b") && argv[5] == std::wstring(L"trailing\\") ? 0 : 1;
    }
    if (mode == L"--hang") { Sleep(60000); return 0; }
    if (mode == L"--flood") { std::cout << std::string(128 * 1024, 'x'); return 0; }
    if (mode == L"--orphan") {
      const auto self = self_path();
      auto command = command_line({self, L"--hang"});
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION child{};
      if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 1;
      CloseHandle(child.hThread);
      CloseHandle(child.hProcess);
      return 0;
    }
    if (argc < 7 || mode != L"--limit-bytes" || std::wstring(argv[3]) != L"--timeout-ms" ||
        std::wstring(argv[5]) != L"--") throw std::runtime_error("invalid memory launcher arguments");
    std::size_t consumed = 0;
    const auto bytes = std::stoull(argv[2], &consumed);
    if (consumed != std::wstring(argv[2]).size() || bytes != lifetime_limit)
      throw std::runtime_error("lifetime tests require exactly 96 MiB");
    const auto milliseconds = std::stoull(argv[4], &consumed);
    if (consumed != std::wstring(argv[4]).size() || !milliseconds || milliseconds > 60000)
      throw std::runtime_error("invalid execution deadline");
    const auto result = run_limited(std::vector<std::wstring>(argv + 6, argv + argc),
                                    static_cast<SIZE_T>(bytes), std::chrono::milliseconds(milliseconds));
    std::cout << result.output;
    std::cerr << result.stderr_output;
    if (!result.error.empty()) { std::cerr << "memory limiter: " << result.error << '\n'; return 125; }
    return static_cast<int>(result.code);
  } catch (const std::exception &error) {
    std::cerr << "memory limiter: " << error.what() << '\n';
    return 125;
  }
}
