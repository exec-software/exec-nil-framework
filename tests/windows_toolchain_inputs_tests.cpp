#include "../nilbuild/windows_toolchain_inputs.hpp"
#include "../common/content_digest.hpp"
#include "../common/private_directory.hpp"

#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
namespace detail = nilbuild::windows_toolchain_detail;
namespace {
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream << text;
  if (!stream) throw std::runtime_error("cannot write test fixture");
}
std::string dump(const std::string &directives = {}) {
  return "Dump of file fixture.lib\n\nFile Type: LIBRARY\n\n"
         "   Linker Directives\n   -----------------\n" + directives +
         "\n  Summary\n    100 .text\n";
}
std::map<std::string, std::string> identities(const std::vector<fs::path> &files) {
  std::map<std::string, std::string> result;
  for (const auto &file : files) result[file.string()] = nil::detail::file_digest(file);
  return result;
}

void parsing() {
  const auto supported = detail::directive_libraries(dump(
      " /DEFAULTLIB:\"runtime library.lib\" /DEFAULTLIB:MSVCRT\n"
      " /FAILIFMISMATCH:RuntimeLibrary=MD_DynamicRelease\n"
      " /disallowlib:libcmt.lib\n /ThrowingNew\n"
      " /alternatename:_one=_two\n /include:__dyn_tls_init\n"));
  check(supported && *supported == std::vector<std::string>{"runtime library.lib", "MSVCRT"},
        "default libraries preserve full quoted names and ordering");
  check(detail::directive_libraries(dump()).has_value(), "empty directive blocks are supported");
  check(detail::directive_libraries("File Type: LIBRARY\n\n Summary\n 100 .text\n").has_value(),
        "import archives without directives are supported");
  for (const auto *invalid : {" /LIBPATH:other\n", " @options.rsp\n", " extra.obj\n",
                              " /DEFAULTLIB:\"unterminated\n", " /DEFAULTLIB:\n",
                              " /DEFAULTLIB:*.lib\n", " /ORDER:@order.txt\n",
                              " /NODEFAULTLIB\n", " /ThrowingNew:extra\n", " /UNKNOWN:value\n"})
    check(!detail::directive_libraries(dump(invalid)), "unknown or malformed directives disable reuse");
  check(!detail::directive_libraries("/DEFAULTLIB:missed.lib\n"), "missing dump format is not an empty closure");
  check(!detail::directive_libraries("File Type: LIBRARY\n 10 .drectve\n"),
        "unrecognized nonempty directive section disables reuse");
  check(!detail::directive_libraries("arbitrary tool output\n"), "unknown dumper output disables reuse");
}

void closure(const fs::path &root) {
  const auto bin = root / "bin";
  const auto project = root / "project";
  const auto vc = root / "vc lib";
  const auto sdk = root / "sdk lib";
  const auto ambient = root / "ambient lib";
  fs::create_directories(project);
  fs::create_directories(ambient);
  const auto driver = bin / "link.exe";
  const auto runtime = root / "runtime" / "nil.lib";
  write(driver, "linker version 1");
  write(bin / "cvtres.exe", "resource converter 1");
  write(bin / "c2.dll", "linker plugin 1");
  write(bin / "1033" / "linkui.dll", "localized linker resource 1");
  write(runtime, "NIL runtime archive");
  for (const auto *name : {"msvcrt.lib", "vcruntime.lib", "legacy_stdio_definitions.lib",
                          "msvcprt.lib", "oldnames.lib"}) write(vc / name, name);
  for (const auto *name : {"kernel32.lib", "ucrt.lib", "uuid.lib"}) write(sdk / name, name);
  write(ambient / "ambient.lib", "ambient library");
  std::map<fs::path, std::string> directives;
  directives[runtime] = " /DEFAULTLIB:msvcprt /DEFAULTLIB:oldnames\n";
  directives[vc / "msvcprt.lib"] = " /DEFAULTLIB:uuid.lib /DEFAULTLIB:msvcrt\n";
  directives[vc / "msvcrt.lib"] = " /DEFAULTLIB:msvcprt /DEFAULTLIB:ambient.lib\n";
  std::map<fs::path, int> probes;
  const auto inspect = [&](const fs::path &path) -> std::optional<std::string> {
    ++probes[path];
    return dump(directives[path]);
  };
  const auto collect = [&]() {
    probes.clear();
    return detail::collector(driver, project, runtime, {vc, sdk}, ambient.string(), inspect).files();
  };
  auto files = collect();
  check(files.has_value(), "supported MSVC closure resolves");
  auto before = identities(*files);
  for (const auto &file : {runtime, driver, bin / "cvtres.exe", bin / "c2.dll",
                          bin / "1033" / "linkui.dll", vc / "msvcprt.lib", vc / "oldnames.lib",
                          sdk / "uuid.lib", ambient / "ambient.lib"})
    check(before.count(file.string()) == 1, "closure includes runtime, transitive libs and helper tools");
  check(probes[vc / "msvcprt.lib"] == 1, "default library cycles do not repeat subprocesses");

  const auto previous_time = fs::last_write_time(sdk / "uuid.lib");
  write(sdk / "uuid.lib", "UUID library changed");
  fs::last_write_time(sdk / "uuid.lib", previous_time);
  files = collect();
  check(files && identities(*files) != before, "transitive archive changes invalidate despite preserved timestamp");
  before = identities(*files);
  write(bin / "c2.dll", "linker plugin changed");
  files = collect();
  check(files && identities(*files) != before, "linker helper updates invalidate reuse");
  before = identities(*files);
  write(bin / "new-plugin.dll", "new linker helper");
  files = collect();
  check(files && identities(*files) != before, "new adjacent helper changes closure");

  // Current-directory libraries shadow /LIBPATH roots, and an earlier
  // /LIBPATH root shadows a later one even when both files still exist.
  write(project / "uuid.lib", "project UUID library");
  files = collect();
  check(files && identities(*files).count((project / "uuid.lib").string()) &&
            !identities(*files).count((sdk / "uuid.lib").string()),
        "new cwd shadow is selected on fresh analysis");
  fs::remove(project / "uuid.lib");
  write(vc / "uuid.lib", "earlier UUID library");
  files = collect();
  check(files && identities(*files).count((vc / "uuid.lib").string()) &&
            !identities(*files).count((sdk / "uuid.lib").string()),
        "explicit library search order is honored");
  fs::remove(vc / "uuid.lib");

  directives[runtime] = " /DEFAULTLIB:absent.lib\n";
  check(!collect(), "missing transitive libraries disable reuse");
  directives[runtime] = " /LIBPATH:unmodeled\n";
  check(!collect(), "custom archive search directives disable reuse");
  directives[runtime].clear();
  fs::remove(sdk / "kernel32.lib");
  check(!collect(), "missing explicit link libraries disable reuse");
  write(sdk / "kernel32.lib", "kernel32.lib");
  const auto fail = [](const fs::path &) -> std::optional<std::string> { return {}; };
  check(!detail::collector(driver, project, runtime, {vc, sdk}, "", fail).files(),
        "failed inspection disables reuse");
  check(!detail::collector(bin / "lld-link.exe", project, runtime, {vc, sdk}, "", inspect).files(),
        "unmodeled fallback driver disables reuse");
  fs::remove(bin / "cvtres.exe");
  check(!collect(), "missing adjacent resource converter cannot silently use an untracked PATH helper");

  const auto paths = detail::library_search_paths(project, {vc, sdk},
      '"' + ambient.string() + "\";relative libraries");
  check(paths == std::vector<fs::path>{project, vc, sdk, ambient, project / "relative libraries"},
        "LIB uses semicolons and follows ordered explicit paths");
}
} // namespace

int main(int argc, char **argv) {
  const auto root = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-msvc-closure-");
  try {
    parsing();
    closure(root);
    // Optional real MSVC dumps make the parser testable on the Linux host too.
    for (int index = 1; index < argc; ++index) {
      std::ifstream input(argv[index], std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(input)), {});
      check(input.good() || input.eof(), "can read supplied MSVC dump");
      check(detail::directive_libraries(text).has_value(), "actual MSVC archive directives are supported");
    }
    fs::remove_all(root);
    std::cout << "MSVC toolchain closure: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
