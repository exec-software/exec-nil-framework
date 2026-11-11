#include "../nilbuild/darwin_toolchain_inputs.hpp"
#include "../common/content_digest.hpp"
#include "../common/private_directory.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
namespace detail = nilbuild::darwin_toolchain_detail;
namespace {
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary); stream << text;
  if (!stream) throw std::runtime_error("cannot write fixture");
}
std::string quote(const std::string &text) {
  std::string result = "\"";
  for (const auto byte : text) { if (byte == '"' || byte == '\\') result += '\\'; result += byte; }
  return result + '"';
}
std::string stub(const std::string &name = "/usr/lib/libSystem.B.dylib") {
  return "--- !tapi-tbd\ntbd-version: 4\ntargets: [ arm64-macos, x86_64-macos ]\ninstall-name: " + name +
         "\nreexported-libraries:\n  - targets: [ arm64-macos ]\n    libraries: [ '/usr/lib/system/libdyld.dylib',\n"
         "                 '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation' ]\n"
         "exports:\n  - targets: [ arm64-macos ]\n    symbols: [ _one, '$ld$hide$os10.4$_two' ]\n...\n";
}
std::string plan_text(const detail::link_plan &plan, const fs::path &runtime) {
  std::string result = "Apple clang version 17.0.0 (clang-1700.0.13.5)\nTarget: arm64-apple-darwin24.6.0\n"
                       "Thread model: posix\nInstalledDir: " + plan.installed.string() + "\n ";
  for (const auto &value : std::vector<std::string>{plan.linker.string(), "-demangle", "-lto_library", plan.lto.string(),
       "-dynamic", "-arch", plan.architecture, "-platform_version", "macos", "15.0.0", "15.5", "-syslibroot",
       plan.sdk.string(), "-mllvm", "-enable-linkonceodr-outlining", "-o", "/dev/null", runtime.string(),
       "-framework", "Cocoa", "-framework", "CoreGraphics", "-lc++", "-lSystem", plan.compiler_rt.string()})
    result += quote(value) + ' ';
  return result + '\n';
}
std::map<std::string, std::string> identities(const std::vector<fs::path> &files) {
  std::map<std::string, std::string> result;
  for (const auto &path : files) result[path.string()] = nil::detail::file_digest(path);
  return result;
}
void parsing(const detail::link_plan &plan, const fs::path &runtime) {
  const auto text = plan_text(plan, runtime);
  const auto parsed = detail::parse_plan(text, runtime);
  check(parsed && parsed->sdk == plan.sdk && parsed->linker == plan.linker &&
        parsed->architecture == "arm64", "real Apple17 plan shape resolves without losing paths");
  auto intel = plan; intel.architecture = "x86_64";
  check(detail::parse_plan(plan_text(intel, runtime), runtime).has_value(), "Intel Apple17 plan resolves");
  for (const auto &extra : {" \"@response.rsp\"", " \"-L/other/lib\"", " \"-framework\" \"Unknown\"", " \"-force_load\" \"other.a\""}) {
    auto changed = text; changed.insert(changed.size() - 1, extra);
    check(!detail::parse_plan(changed, runtime), "unmodeled link inputs fail closed");
  }
  check(!detail::parse_plan("Configuration file: hidden.cfg\n" + text, runtime), "opaque driver configs fail closed");
  check(!detail::parse_plan(text + " \"second-job\"\n", runtime), "extra tool jobs fail closed");
  auto truncated = text.substr(0, text.find(quote("-platform_version"))) + "\"-platform_version\"\n";
  check(!detail::parse_plan(truncated, runtime), "truncated multi-value options fail closed without indexing past argv");
  check(!detail::quoted_arguments("\"unterminated"), "unclosed command quote rejected");
  check(!detail::quoted_arguments("\"one\"unquoted"), "adjacent unquoted text rejected");
  check(!detail::quoted_arguments("\"escaped\\nnewline\""), "unknown command escapes rejected");
  check(detail::supported_stub(stub()), "generated YAML TBDv4 re-export lists supported");
  auto aligned = stub(); aligned.replace(aligned.find("tbd-version: 4"), 14, "tbd-version:     4");
  check(detail::supported_stub(aligned), "actual SDK aligned version field supported");
  for (const auto &name : {"@rpath/library.dylib", "/AppleInternal/build/library.dylib"})
    check(detail::supported_stub(stub(name)), "SDK install IDs are interface data, not re-export file reads");
  for (const auto &name : {"relative.dylib", "@rpath/library.dylib", "/tmp/library.dylib", "/usr/lib/../../tmp/library.dylib"})
    check(!detail::supported_stub(stub() + "libraries: [ '" + name + "' ]\n"), "escaping re-export paths rejected");
  auto external = stub(); external.replace(external.find("/usr/lib/system/libdyld.dylib"), 27, "/outside/library.dylib");
  check(!detail::supported_stub(external), "escaping re-export rejected");
  check(!detail::supported_stub(stub() + "libraries: [ relative ]\n"), "relative re-export rejected");
  check(detail::supported_stub(stub() + "libraries: [ '/System/Library/Frameworks/F.framework/Resources/hidden' ]\n"),
        "actual ATS/ImageIO resource libraries are included in modeled SDK trees");
  check(!detail::supported_stub(stub() + "libraries: [ '/usr/lib/unmodeled.extension' ]\n"), "unmodeled library filename types rejected");
  check(!detail::supported_stub(stub() + "libraries: *alias\n"), "YAML aliases cannot hide dependencies");
  check(!detail::supported_stub(stub() + "reexported-libraries: [{ libraries: [ relative ] }]\n"),
        "unmodeled flow mappings cannot hide relative re-exports");
  check(!detail::supported_stub(stub() + "\"libraries\": [ relative ]\n"), "quoted YAML keys require richer parsing");
  check(!detail::supported_stub(stub() + "libraries: [ '/usr/lib/\\u0061.dylib' ]\n"), "escaped path spelling rejected");
  check(!detail::supported_stub(stub() + "libraries: [ '/usr/lib/one.dylib'\n"), "unfinished re-export list rejected");
  const auto image = plan.linker;
  const auto description = detail::parse_image(image,
      image.string() + ":\n\t@rpath/libtapi.dylib (compatibility version 1.0.0, current version 1.0.0)\n",
      "Load command 1\n          cmd LC_RPATH\n      cmdsize 32\n         path @loader_path/../lib (offset 12)\n");
  check(description && description->libraries.size() == 1 && description->rpaths ==
        std::vector<std::string>{"@loader_path/../lib"}, "otool image and rpath inputs parsed together");
  check(!detail::parse_image(image, "bad format\n", ""), "unknown image listing rejected");
  check(!detail::parse_image(image, image.string() + ":\n", "cmd LC_RPATH\n"), "truncated rpath rejected");
  check(!detail::parse_image(image, image.string() + ":\n", "cmd LC_DYLD_ENVIRONMENT\n"), "embedded loader environment rejected");
  check(!detail::parse_image(image, image.string() + ":\n", "cmd LC_LOAD_DYLINKER\ncmdsize 32\nname /unmodeled/dyld (offset 12)\n"),
        "unmodeled dynamic loader rejected");
  const auto loader = detail::parse_image(image, image.string() + ":\n",
      "cmd LC_LOAD_DYLINKER\ncmdsize 32\nname /usr/lib/dyld (offset 12)\n");
  check(loader && loader->system_loader, "standard system loader explicitly recognized");
}
void closure(const fs::path &root) {
  const auto project = root / "project", host = root / "host";
  fs::create_directories(project);
  detail::link_plan plan;
  plan.installed = root / "Xcode test.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin";
  plan.linker = plan.installed / "ld";
  plan.sdk = root / "SDKs/MacOSX15.5.sdk";
  plan.lto = plan.installed.parent_path() / "lib/libLTO.dylib";
  plan.compiler_rt = plan.installed.parent_path() / "lib/clang/17/lib/darwin/libclang_rt.osx.a";
  plan.architecture = "arm64";
  const auto driver = root / "selected clang++", runtime = root / "nil.a";
  for (const auto &path : {driver, plan.installed / "clang++", plan.linker, plan.lto,
                           plan.installed.parent_path() / "lib/libtapi.dylib", plan.compiler_rt, runtime})
    write(path, "fixture bytes");
  write(plan.sdk / "SDKSettings.json", "SDK15.5");
  write(host / "System/Library/CoreServices/SystemVersion.plist", "macOS15");
  write(plan.sdk / "usr/lib/libSystem.tbd", stub());
  write(plan.sdk / "usr/lib/system/libdyld.tbd", stub("/usr/lib/system/libdyld.dylib"));
  write(plan.sdk / "System/Library/Frameworks/Cocoa.framework/Cocoa.tbd", stub("/System/Library/Frameworks/Cocoa.framework/Cocoa"));
  write(plan.sdk / "System/Library/Frameworks/CoreGraphics.framework/CoreGraphics.tbd", stub("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"));
  std::map<fs::path, detail::image_description> descriptions;
  descriptions[plan.linker] = {{"@rpath/libtapi.dylib", "@rpath/libLTO.dylib", "/usr/lib/libSystem.B.dylib"}, {"@loader_path/../lib"}};
  descriptions[plan.lto] = {{"@rpath/libLTO.dylib", "/usr/lib/libSystem.B.dylib"}, {}};
  const auto inspect = [&](const fs::path &path) -> std::optional<detail::image_description> { return descriptions[path]; };
  const auto collect = [&]() { return detail::collector(driver, project, runtime, plan, inspect, host).files(); };
  parsing(plan, runtime);
  auto files = collect();
  check(files.has_value(), "default SDK/toolchain closure resolves");
  auto before = identities(*files);
  for (const auto &path : {driver, plan.linker, plan.lto, plan.compiler_rt, runtime,
       plan.installed.parent_path() / "lib/libtapi.dylib", plan.sdk / "usr/lib/system/libdyld.tbd"})
    check(before.count(path.string()) != 0, "driver, helpers, runtime and transitive stub bytes included");
  const auto shared = plan.installed.parent_path() / "lib/shared.dylib";
  const auto driver_leaf = plan.installed.parent_path() / "lib/driver-set/leaf.dylib";
  const auto linker_leaf = plan.installed.parent_path() / "lib/linker-set/leaf.dylib";
  for (const auto &path : {shared, driver_leaf, linker_leaf}) write(path, "context-specific image");
  descriptions[driver] = {{shared.string()}, {driver_leaf.parent_path().string()}};
  descriptions[plan.linker].libraries.push_back(shared.string());
  descriptions[plan.linker].rpaths.push_back(linker_leaf.parent_path().string());
  descriptions[shared] = {{"@rpath/leaf.dylib"}, {}};
  files = collect();
  check(files && identities(*files).count(driver_leaf.string()) && identities(*files).count(linker_leaf.string()),
        "shared image is resolved in both driver and linker inherited-runpath contexts");
  descriptions[driver] = {};
  descriptions[plan.linker].libraries.pop_back(); descriptions[plan.linker].rpaths.pop_back();
  const auto other_driver = plan.installed.parent_path() / "alternate-bin/clang++";
  const auto executable_leaf = plan.installed.parent_path() / "alternate-bin/helper.dylib";
  write(other_driver, "other driver"); write(executable_leaf, "root-specific helper");
  descriptions[other_driver] = {{"@rpath/helper.dylib"}, {"@executable_path/."}};
  files = detail::collector(other_driver, project, runtime, plan, inspect, host).files();
  check(files && identities(*files).count(executable_leaf.string()), "executable_path follows the current process root");
  files = collect(); before = identities(*files);
  const auto leaf = plan.sdk / "usr/lib/system/libdyld.tbd";
  const auto timestamp = fs::last_write_time(leaf);
  write(leaf, stub("/usr/lib/system/libdyld.dylib") + "# same-timestamp change\n");
  fs::last_write_time(leaf, timestamp);
  files = collect();
  check(files && identities(*files) != before, "stub content changes invalidate despite preserved timestamps");
  before = identities(*files);
  write(plan.sdk / "usr/lib/new-shadow.tbd", stub("/usr/lib/new-shadow.dylib"));
  files = collect(); check(files && identities(*files) != before, "new SDK shadow changes input namespace");
  before = identities(*files);
  write(plan.installed.parent_path() / "lib/new-helper.dylib", "new helper");
  files = collect(); check(files && identities(*files) != before, "new toolchain helper changes namespace");
  write(plan.sdk / "usr/lib/escape.tbd", stub() + "libraries: [ '/unmodeled/library.dylib' ]\n");
  check(!collect(), "external SDK re-export disables reuse");
  fs::remove(plan.sdk / "usr/lib/escape.tbd");
  descriptions[plan.linker].libraries.push_back("/unmodeled/helper.dylib");
  check(!collect(), "external ordinary toolchain dependency disables reuse");
  descriptions[plan.linker].libraries.pop_back();
  descriptions[plan.linker].rpaths.push_back("/unmodeled");
  check(!collect(), "unmodeled rpath disables reuse");
  descriptions[plan.linker].rpaths.pop_back();
  write(host / "System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", "ordinary host fallback");
  check(!collect(), "new ordinary host fallback for absent SDK re-export disables reuse");
  fs::remove(host / "System/Library/Frameworks/CoreFoundation.framework/CoreFoundation");
  write(runtime, "!<thin>\n");
  check(!collect(), "thin archive external members cannot evade identity");
  write(runtime, "fixture bytes");
  fs::create_directories(plan.sdk / "AppleInternal");
  check(!collect(), "AppleInternal implicit order-file policies disable default-profile reuse");
  fs::remove(plan.sdk / "AppleInternal");
#ifndef _WIN32
  const auto alias = plan.sdk / "usr/lib/alias.tbd";
  fs::create_symlink("system/libdyld.tbd", alias);
  files = collect(); check(files && identities(*files).count(alias.string()), "selected file alias retained");
  before = identities(*files);
  fs::remove(alias); fs::create_symlink("libSystem.tbd", alias);
  files = collect(); check(files && identities(*files) != before, "file alias retarget invalidates even when both old targets exist");
  const auto directory_alias = plan.sdk / "usr/lib/current";
  fs::create_directory_symlink("system", directory_alias);
  files = collect(); check(files && identities(*files).count((directory_alias / "libdyld.tbd").string()),
                          "directory alias preserves its selected child paths");
  fs::create_directory_symlink(".", plan.sdk / "usr/lib/cycle");
  check(!collect(), "directory symlink cycles fail closed within bounds");
  fs::remove(plan.sdk / "usr/lib/cycle");
#endif
  fs::remove(plan.lto);
  check(!collect(), "missing required linker helper disables reuse");
}
} // namespace

int main() {
  const auto root = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-darwin-closure-");
  try {
    closure(root);
    fs::remove_all(root);
    std::cout << "Darwin toolchain closure: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored; fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n'; return 1;
  }
}
