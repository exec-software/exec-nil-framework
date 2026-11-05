#include "../nilcompiler/src/compiler_module_fingerprint.hpp"
#include "../common/content_digest.hpp"
#include "../common/private_directory.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
namespace modules = nil::detail::compiler_modules;
namespace {
int checks = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << bytes;
  if (!output) throw std::runtime_error("cannot write module fixture");
}
auto identities(const modules::files &files) {
  std::map<std::string, std::string> result;
  for (const auto &file : files) result[file] = nil::detail::file_digest(fs::u8path(file));
  return result;
}

void portable(const fs::path &root) {
  const auto anchor = root / "system" / "apphelp.dll";
  const auto compiler = root / "compiler.exe";
  const auto other = root / "application" / "apphelp.dll";
  const auto alias = root / "alias.dll";
  write(compiler, "compiler bytes");
  write(anchor, "module version 1");
  write(other, "module version 1"); // Same content and basename are not identity.
  fs::create_hard_link(anchor, alias);
  const modules::files base{compiler.u8string(), other.u8string()};
  auto absent_from_snapshot = base, present_in_snapshot = base;
  present_in_snapshot.insert(anchor.u8string());
  check(modules::stabilize_optional_anchor(absent_from_snapshot, anchor), "unloaded anchor is tracked");
  check(modules::stabilize_optional_anchor(present_in_snapshot, anchor), "loaded anchor is tracked");
  check(absent_from_snapshot == present_in_snapshot, "load presence cannot change the fingerprint set");
  check(absent_from_snapshot.count(anchor.u8string()) == 1, "system anchor occurs once");
  check(absent_from_snapshot.count(other.u8string()) == 1, "different same-basename DLL stays tracked");
  auto aliases = base;
  aliases.insert(alias.u8string());
  aliases.insert((anchor.parent_path() / "." / anchor.filename()).u8string());
  check(modules::stabilize_optional_anchor(aliases, anchor), "real file aliases resolve");
  check(aliases == present_in_snapshot, "verified hardlink and spelling aliases collapse only to anchor");

  const auto before = identities(present_in_snapshot);
  const auto timestamp = fs::last_write_time(anchor);
  write(anchor, "module version 2");
  fs::last_write_time(anchor, timestamp);
  check(identities(present_in_snapshot) != before, "same-size preserved-timestamp content changes remain fingerprinted");

  const auto missing = root / "system" / "missing.dll";
  auto files = base;
  check(modules::stabilize_optional_anchor(files, missing) && files == base, "genuine optional absence preserves observed set");
  write(missing, "new component");
  check(modules::stabilize_optional_anchor(files, missing) && files.count(missing.u8string()), "new component presence enters set");
  fs::remove(missing);
  auto fresh = base;
  check(modules::stabilize_optional_anchor(fresh, missing) && fresh != files, "component deletion changes fresh set");
  check(!modules::stabilize_optional_anchor(fresh, root / "missing-parent" / "apphelp.dll"), "missing parent is not optional leaf absence");
  check(!modules::stabilize_optional_anchor(fresh, compiler / "apphelp.dll"), "regular-file parent is rejected");
  check(!modules::stabilize_optional_anchor(fresh, root / "system"), "nonregular anchor is rejected");
  check(!modules::stabilize_optional_anchor(fresh, "relative.dll"), "relative anchor is rejected");

  const auto status = [](const fs::path &path, std::error_code &error) { return fs::status(path, error); };
  const auto equivalent = [](const fs::path &left, const fs::path &right, std::error_code &error) {
    return fs::equivalent(left, right, error);
  };
  for (const auto failure : {std::errc::permission_denied, std::errc::io_error}) {
    files = base;
    check(!modules::stabilize_optional_anchor(files, anchor,
      [&](const fs::path &path, std::error_code &error) {
        if (path == anchor) { error = std::make_error_code(failure); return fs::file_status{}; }
        return status(path, error);
      }, equivalent), "lookup errors disable a complete fingerprint");
    check(files == base, "lookup failure cannot erase observed files");
  }
  // The first successful identity check may already have removed an alias in
  // the temporary set. A later error must leave the whole original unchanged.
  files = {alias.u8string(), compiler.u8string(), other.u8string()};
  const auto original = files;
  int calls = 0;
  check(!modules::stabilize_optional_anchor(files, anchor, status,
    [&](const fs::path &left, const fs::path &right, std::error_code &error) {
      if (++calls == 2) { error = std::make_error_code(std::errc::permission_denied); return false; }
      return equivalent(left, right, error);
    }), "identity failure disables completeness");
  check(calls == 2 && files == original, "identity failure does not partially suppress aliases");

  auto additional_module = present_in_snapshot;
  const auto plugin = root / "new-plugin.dll";
  write(plugin, "plugin");
  additional_module.insert(plugin.u8string());
  check(modules::stabilize_optional_anchor(additional_module, anchor), "other module remains valid");
  check(additional_module != present_in_snapshot && additional_module.count(plugin.u8string()),
        "arbitrary observed module changes remain visible");
#ifdef _WIN32
  auto case_alias = base;
  std::wstring spelling = anchor.wstring();
  for (auto &character : spelling) if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
  case_alias.insert(fs::path(spelling).u8string());
  check(modules::stabilize_optional_anchor(case_alias, anchor) && case_alias == present_in_snapshot,
        "Windows case alias collapses only through verified file identity");
#endif
}

void mapped_file_inventory() {
  const auto mapping = [](const std::string &path) {
    return "1000-2000 r-xp 00000000 00:01 42 " + path + "\n";
  };
  const auto readable = [](const std::string &path) { return path == "/compiler" || path == "/library with spaces.so"; };
  modules::files files;
  std::istringstream complete(mapping("/compiler") + mapping("/library with spaces.so") +
                              mapping("/compiler") + "2000-3000 rw-p 00000000 00:00 0\n"
                              "3000-4000 r-xp 00000000 00:00 0 [vdso]\n");
  check(modules::collect_mapped_files(complete, files, readable), "complete readable mappings certify compiler identity");
  check(files == modules::files{"/compiler", "/library with spaces.so"}, "mapping duplicates collapse without splitting path spaces");
  for (const auto &tail : {mapping("/compiler (deleted)"), mapping("/unreadable.so"),
                           mapping("relative.so"), mapping(""), mapping("[unresolved-file]"),
                           std::string("1000-2000 r-xp 00000000 00:01 invalid /compiler\n"),
                           std::string("truncated mapping\n")}) {
    files.clear();
    std::istringstream incomplete(mapping("/compiler") + tail);
    check(!modules::collect_mapped_files(incomplete, files, readable), "partial loaded-file identity never certifies completeness");
    check(files == modules::files{"/compiler"}, "partial discovery retains valid observed inputs");
  }
  files.clear();
  std::istringstream empty;
  check(!modules::collect_mapped_files(empty, files, readable), "empty mapping inventory is incomplete");
  std::istringstream unreadable(mapping("/compiler"));
  unreadable.setstate(std::ios::badbit);
  check(!modules::collect_mapped_files(unreadable, files, readable), "unreadable mapping stream is incomplete");
}

#ifdef _WIN32
void windows_api() {
  const auto anchor = modules::system_apphelp_anchor();
  check(anchor.has_value() && anchor->is_absolute(), "wide system-directory API selects absolute anchor");
  check(anchor->filename() == L"apphelp.dll", "system anchor names only the observed optional component");
  modules::files observed;
  check(modules::collect_windows(observed), "real Windows process module snapshot completes");
  check(!observed.empty(), "real Windows snapshot retains loaded modules");
  std::error_code error;
  const auto metadata = fs::status(*anchor, error);
  if (!error && fs::is_regular_file(metadata)) {
    check(observed.count(anchor->u8string()) == 1, "system apphelp is included regardless of load presence");
    std::size_t aliases = 0;
    for (const auto &file : observed) {
      if (fs::equivalent(fs::u8path(file), *anchor, error)) ++aliases;
      check(!error, "retained real process module identity is readable");
    }
    check(aliases == 1, "real Windows snapshot has exactly one system-anchor identity");
  } else {
    check(metadata.type() == fs::file_type::not_found || error == std::errc::no_such_file_or_directory,
          "only genuine system-anchor absence is optional");
  }
}
#endif
} // namespace

int main() {
  const auto root = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-compiler-modules-");
  try {
    portable(root);
    mapped_file_inventory();
#ifdef _WIN32
    windows_api();
#endif
    fs::remove_all(root);
    std::cout << "compiler module fingerprints: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
