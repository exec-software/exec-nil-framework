// Windows real-editor test adapter, never an SDK or extension component.
// A copied executable runs its adjacent .cjs fixture using explicit Node.
// Pipe handles are inherited exactly; all child processes belong to one Job.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../../common/windows_environment.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
struct handle {
  HANDLE value = nullptr;
  explicit handle(HANDLE initial = nullptr) : value(initial) {}
  handle(const handle &) = delete;
  handle &operator=(const handle &) = delete;
  void reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
  ~handle() { reset(); }
};
[[noreturn]] void fail(const char *operation) {
  throw std::runtime_error(std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")");
}
std::wstring quote(const std::wstring &text) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (auto character : text) {
    if (character == L'\\') { ++slashes; continue; }
    result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result.push_back(character);
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}
void duplicate_standard(DWORD kind, handle &result) {
  const auto original = GetStdHandle(kind);
  if (!original || original == INVALID_HANDLE_VALUE ||
      !DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(),
                       &result.value, 0, TRUE, DUPLICATE_SAME_ACCESS))
    fail("duplicate fixture standard handle");
}
void diagnostic_snapshot(HANDLE job, const char *stage) {
  const auto deadline = clock_type::now() + std::chrono::seconds(2);
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  struct process_ids { DWORD assigned, count; ULONG_PTR ids[4096]; } ids{};
  if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) ||
      !QueryInformationJobObject(job, JobObjectBasicProcessIdList, &ids, sizeof(ids), nullptr) ||
      ids.count > 4096 || ids.count != ids.assigned) {
    std::cerr << "Fixture diagnostic " << stage << ": incomplete Job snapshot\n";
    return;
  }
  std::cerr << "Fixture diagnostic " << stage << ": TotalProcesses=" << accounting.TotalProcesses
            << " ActiveProcesses=" << accounting.ActiveProcesses << " listed=" << ids.count << '\n';
  for (DWORD index = 0; index < ids.count; ++index) {
    if (clock_type::now() >= deadline) {
      std::cerr << "Fixture diagnostic " << stage << ": inspection deadline reached\n";
      break;
    }
    if (ids.ids[index] > MAXDWORD) continue;
    const auto pid = static_cast<DWORD>(ids.ids[index]);
    handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    BOOL belongs = FALSE;
    if (!process.value || !IsProcessInJob(process.value, job, &belongs) || !belongs) {
      std::cerr << "Fixture diagnostic pid=" << pid << ": exact Job membership unavailable\n";
      continue;
    }
    std::vector<wchar_t> image(32768);
    DWORD length = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(process.value, 0, image.data(), &length)) {
      const auto name = std::filesystem::path(std::wstring(image.data(), length)).filename().u8string();
      std::cerr << "Fixture diagnostic owned pid=" << pid << " image=" << name << '\n';
    } else std::cerr << "Fixture diagnostic owned pid=" << pid << ": image unavailable\n";
  }
}
class job_cleanup {
  static constexpr std::size_t maximum_members = 4096;
  using process_identity = std::pair<DWORD, std::uint64_t>;
  HANDLE job_, leader_;
  std::optional<clock_type::time_point> deadline_;
  std::optional<DWORD> population_;
  std::map<process_identity, std::unique_ptr<handle>> members_;
  std::string failure_;

  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting() const {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    if (!QueryInformationJobObject(job_, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr))
      fail("query fixture Job");
    return info;
  }
  void check_deadline() const {
    if (deadline_ && clock_type::now() >= *deadline_)
      throw std::runtime_error("fixture cleanup exceeded its common five-second deadline");
  }
  void remember(const std::string &message) {
    if (failure_.empty()) failure_ = message;
  }
  void verify_population(DWORD total) const {
    if (population_ && total != *population_)
      throw std::runtime_error("fixture Job population changed during cleanup snapshot");
  }
  static bool signaled(HANDLE process) {
    const auto result = WaitForSingleObject(process, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT) fail("wait for retained fixture process");
    return result == WAIT_OBJECT_0;
  }
  static process_identity identity(HANDLE process, DWORD pid) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
      fail("verify fixture process creation time");
    return std::make_pair(pid, (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) |
                                  created.dwLowDateTime);
  }
public:
  job_cleanup(HANDLE job, HANDLE leader) : job_(job), leader_(leader) {}

  void prepare() {
    if (!deadline_) deadline_ = clock_type::now() + std::chrono::seconds(5);
    check_deadline();
    const auto before = accounting().TotalProcesses;
    if (!population_) population_ = before;
    verify_population(before);
    const auto leader_pid = GetProcessId(leader_);
    if (!leader_pid) fail("identify retained fixture leader");
    const auto leader_identity = identity(leader_, leader_pid);
    // Bounded Job-local enumeration, never a system-wide PID scan. A partial
    // or changing snapshot is a verification failure, not permission to guess.
    struct process_ids {
      DWORD assigned, count;
      ULONG_PTR ids[maximum_members];
    } ids{};
    if (!QueryInformationJobObject(job_, JobObjectBasicProcessIdList, &ids, sizeof(ids), nullptr))
      fail("snapshot fixture Job members");
    if (ids.count > maximum_members || ids.assigned != ids.count)
      throw std::runtime_error("fixture Job process snapshot is incomplete");
    for (DWORD index = 0; index < ids.count; ++index) {
      check_deadline();
      if (ids.ids[index] > MAXDWORD) throw std::runtime_error("invalid fixture Job process identity");
      const auto pid = static_cast<DWORD>(ids.ids[index]);
      auto member = std::make_unique<handle>(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
      if (!member->value) fail("retain fixture Job member");
      BOOL belongs = FALSE;
      if (!IsProcessInJob(member->value, job_, &belongs)) fail("verify fixture Job membership");
      if (!belongs) throw std::runtime_error("fixture Job snapshot no longer identifies an owned process");
      const auto member_identity = identity(member->value, pid);
      if (members_.find(member_identity) == members_.end()) {
        if (members_.size() >= maximum_members) throw std::runtime_error("fixture retained-process budget exceeded");
        members_.emplace(member_identity, std::move(member));
      }
    }
    const auto after = accounting();
    verify_population(after.TotalProcesses);
    // These compiler fixtures do not legitimately launch subprocesses. A
    // descendant that already vanished from Job accounting could still hold
    // resources during rundown, and cannot be recovered safely by PID. Fail
    // closed unless every lifetime is represented by a retained exact handle.
    // This is deliberately narrower than a general-purpose process runner.
    const auto covered = members_.size() + (members_.count(leader_identity) ? 0 : 1);
    if (covered != *population_)
      throw std::runtime_error("fixture Job has an unretained process lifetime (TotalProcesses=" +
                               std::to_string(after.TotalProcesses) + ", ActiveProcesses=" +
                               std::to_string(after.ActiveProcesses) + ", retained=" +
                               std::to_string(members_.size()) + ", covered=" +
                               std::to_string(covered) + ", leader_pid=" +
                               std::to_string(leader_pid) + ")");
  }

  void terminate(UINT exit_code) {
    try { prepare(); }
    catch (const std::exception &error) { remember(error.what()); }
    // Even a failed snapshot must stop this exact owned Job. Newly opened
    // member handles have query/wait rights only; no reusable PID is signaled.
    if (!TerminateJobObject(job_, exit_code))
      remember("terminate fixture Job failed (Win32 " + std::to_string(GetLastError()) + ")");
    try { verify_population(accounting().TotalProcesses); }
    catch (const std::exception &error) { remember(error.what()); }
  }

  bool wait_for(DWORD milliseconds) {
    if (!deadline_) throw std::runtime_error("fixture cleanup snapshot was not initialized");
    const auto requested = clock_type::now() + std::chrono::milliseconds(milliseconds);
    const auto end = requested < *deadline_ ? requested : *deadline_;
    do {
      check_deadline();
      const auto state = accounting();
      try { verify_population(state.TotalProcesses); }
      catch (const std::exception &error) { remember(error.what()); }
      bool all_signaled = signaled(leader_);
      for (const auto &member : members_) {
        check_deadline();
        all_signaled = signaled(member.second->value) && all_signaled;
      }
      // ActiveProcesses==0 can precede process-resource rundown. Retained
      // process handles must also signal before the caller can remove CWD/files.
      if (state.ActiveProcesses == 0 && all_signaled) {
        if (!failure_.empty()) throw std::runtime_error(failure_);
        return true;
      }
      if (clock_type::now() >= end) return false;
      Sleep(10); // Bounded polling of the owned handles, never a grace sleep.
    } while (true);
  }
};
int run(int argc, wchar_t **argv) {
  const bool diagnostic = nil::detail::windows_environment_get("NIL_VSCODE_FIXTURE_DIAGNOSTIC").value_or("") == "1";
  const auto node_text = nil::detail::windows_environment_get("NIL_VSCODE_FIXTURE_NODE");
  if (!node_text || node_text->empty()) throw std::runtime_error("NIL_VSCODE_FIXTURE_NODE is required");
  const auto node_wide = nil::detail::environment_utf16(*node_text);
  if (!node_wide) throw std::runtime_error("Node path is not valid UTF-8");
  const std::filesystem::path node(*node_wide);
  if (!node.is_absolute() || !std::filesystem::is_regular_file(node))
    throw std::runtime_error("NIL_VSCODE_FIXTURE_NODE must be an existing absolute executable");
  std::vector<wchar_t> filename(32768);
  const auto length = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
  if (!length || length >= filename.size()) fail("resolve fixture executable");
  auto script = std::filesystem::path(std::wstring(filename.data(), length));
  script.replace_extension(L".cjs");
  if (!std::filesystem::is_regular_file(script)) throw std::runtime_error("Adjacent .cjs fixture is missing");
  DWORD timeout = 15000;
  if (const auto value = nil::detail::windows_environment_get("NIL_VSCODE_FIXTURE_TIMEOUT_MS")) {
    if (value->empty() || value->find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("Fixture timeout must be an integer from 50 to 15000 milliseconds");
    const auto parsed = std::stoull(*value);
    if (parsed < 50 || parsed > 15000) throw std::runtime_error("Fixture timeout outside 50..15000 milliseconds");
    timeout = static_cast<DWORD>(parsed);
  }
  std::wstring command = quote(node.native()) + L" " + quote(script.native());
  for (int index = 1; index < argc; ++index) command += L" " + quote(argv[index]);
  if (command.size() >= 32767) throw std::runtime_error("Fixture command exceeds Windows command-line limit");
  std::vector<wchar_t> arguments(command.begin(), command.end());
  arguments.push_back(L'\0');
  handle job(CreateJobObjectW(nullptr, nullptr));
  if (!job.value) fail("create fixture Job");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    fail("set fixture Job lifetime");
  handle input, output, errors;
  duplicate_standard(STD_INPUT_HANDLE, input);
  duplicate_standard(STD_OUTPUT_HANDLE, output);
  duplicate_standard(STD_ERROR_HANDLE, errors);
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
  std::vector<unsigned char> storage(bytes);
  auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!InitializeProcThreadAttributeList(attributes, 2, 0, &bytes)) fail("initialize fixture attributes");
  struct attribute_guard {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~attribute_guard() { DeleteProcThreadAttributeList(value); }
  } guard{attributes};
  HANDLE inherited[] = {input.value, output.value, errors.value};
  if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                inherited, sizeof(inherited), nullptr, nullptr))
    fail("restrict fixture inherited handles");
  // Windows 10+ assigns this Job during creation, including the interval
  // before CreateProcess returns. Killing the adapter cannot orphan a child.
  // https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute
  if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                &job.value, sizeof(job.value), nullptr, nullptr))
    fail("set fixture creation-time Job");
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.value;
  startup.StartupInfo.hStdOutput = output.value;
  startup.StartupInfo.hStdError = errors.value;
  startup.lpAttributeList = attributes;
  PROCESS_INFORMATION child{};
  // These fake compilers use only the explicit inherited pipes. Do not attach
  // or implicitly create a console session for them. Keep the strict lifetime
  // check rather than exempting an unidentified extra process from ownership.
  // https://learn.microsoft.com/windows/console/creation-of-a-console
  if (!CreateProcessW(node.c_str(), arguments.data(), nullptr, nullptr, TRUE,
      CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
      nullptr, nullptr, &startup.StartupInfo, &child)) fail("create fixture Node process");
  handle process(child.hProcess), thread(child.hThread);
  job_cleanup cleanup(job.value, process.value);
  try {
    BOOL belongs = FALSE;
    if (!IsProcessInJob(process.value, job.value, &belongs)) fail("verify suspended fixture Job membership");
    if (!belongs) throw std::runtime_error("suspended fixture process was not assigned to its exact Job");
    if (diagnostic) diagnostic_snapshot(job.value, "before-resume");
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) fail("resume fixture Node process");
    thread.reset();
    const auto status = WaitForSingleObject(process.value, timeout);
    if (status == WAIT_FAILED) fail("wait for fixture Node process");
    if (diagnostic) diagnostic_snapshot(job.value, "after-wait");
    DWORD exit_code = 125;
    if (status == WAIT_OBJECT_0) {
      if (!GetExitCodeProcess(process.value, &exit_code)) fail("read fixture exit status");
      cleanup.prepare();
      if (cleanup.wait_for(250)) return static_cast<int>(exit_code);
    }
    cleanup.terminate(status == WAIT_TIMEOUT ? 124 : 125);
    if (!cleanup.wait_for(5000)) throw std::runtime_error("Fixture Job cleanup could not be confirmed");
    std::cerr << (status == WAIT_TIMEOUT ? "Fixture timed out\n" : "Fixture left running descendants\n");
    return status == WAIT_TIMEOUT ? 124 : 125;
  } catch (const std::exception &error) {
    // Every exceptional post-creation path retains the exact process/Job
    // handles through bounded cleanup confirmation, including resume errors.
    const std::string original = error.what();
    std::string cleanup_result;
    cleanup.terminate(125);
    try {
      cleanup_result = cleanup.wait_for(5000) ? "confirmed" : "could not be confirmed";
    } catch (const std::exception &cleanup_error) { cleanup_result = cleanup_error.what(); }
    throw std::runtime_error(original + "; fixture cleanup " + cleanup_result);
  }
}
} // namespace

int wmain(int argc, wchar_t **argv) {
  try { return run(argc, argv); }
  catch (const std::exception &error) {
    std::cerr << "Compiler fixture adapter: " << error.what() << '\n';
    return 125;
  }
}
