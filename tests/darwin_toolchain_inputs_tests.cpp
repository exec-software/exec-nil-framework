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
void integer(std::string &bytes, std::size_t offset, std::uint64_t value, unsigned width, bool big) {
  for (unsigned index = 0; index < width; ++index)
    bytes.at(offset + index) = static_cast<char>((value >> (8 * (big ? width - index - 1 : index))) & 255);
}
std::string macho(std::uint32_t kind, bool wide = true, bool big = false,
                  std::uint32_t cpu = 0x0100000c, std::uint32_t subtype = 2) {
  std::string bytes(wide ? 32 : 28, '\0');
  integer(bytes, 0, wide ? 0xfeedfacf : 0xfeedface, 4, big);
  integer(bytes, 4, cpu, 4, big); integer(bytes, 8, subtype, 4, big);
  integer(bytes, 12, kind, 4, big);
  return bytes;
}
std::string universal(std::uint32_t first = 2, std::uint32_t second = 8,
                      bool wide = false, bool big = true) {
  std::string bytes(160, '\0');
  integer(bytes, 0, wide ? 0xcafebabf : 0xcafebabe, 4, big); integer(bytes, 4, 2, 4, big);
  for (unsigned index = 0; index < 2; ++index) {
    const auto at = 8 + index * (wide ? 32 : 20), offset = 96 + index * 32;
    const auto cpu = index ? 0x01000007U : 0x0100000cU, subtype = index ? 3U : 2U;
    integer(bytes, at, cpu, 4, big); integer(bytes, at + 4, subtype, 4, big);
    integer(bytes, at + 8, offset, wide ? 8 : 4, big);
    integer(bytes, at + (wide ? 16 : 12), 32, wide ? 8 : 4, big);
    integer(bytes, at + (wide ? 24 : 16), 5, 4, big);
    bytes.replace(offset, 32, macho(index ? second : first, true, false, cpu, subtype));
  }
  return bytes;
}
void machos(const fs::path &root) {
  const auto path = root / "mach-o-control";
  const auto accepts = [&](const std::string &bytes) { write(path, bytes); return detail::non_linkable_macho(path); };
  const auto malformed = [&](const std::string &bytes) { write(path, bytes); return !detail::macho_file_types(path); };
  for (bool wide : {false, true}) for (bool big : {false, true}) {
    check(accepts(macho(2, wide, big)), "both byte orders and widths admit executable SDK resources");
    check(accepts(macho(8, wide, big)), "both byte orders and widths admit bundle SDK resources");
    check(accepts(universal(2, 8, wide, big)), "every universal executable/bundle slice is positively identified");
  }
  for (const auto kind : {1U, 6U, 9U, 0xffffffffU}) {
    check(!accepts(macho(kind)), "object/dylib/stub/unknown file types cannot be ignored as SDK resources");
    check(!accepts(universal(8, kind)), "one linkable or unknown universal slice prevents omission");
  }
  check(malformed("ordinary metadata"), "ordinary file is not positively identified as a Mach-O resource");
  check(malformed("!<arch>\n"), "an archive is not a Mach-O resource");
  check(malformed(macho(8).substr(0, 31)), "truncated 64-bit header is rejected");
  auto commands = macho(8); commands.resize(40);
  integer(commands, 16, 1, 4, false); integer(commands, 20, 8, 4, false);
  integer(commands, 32, 1, 4, false); integer(commands, 36, 8, 4, false);
  check(accepts(commands), "complete bounded load commands are accepted");
  check(malformed(commands.substr(0, 39)), "truncated load commands are rejected");
  auto changed = commands; integer(changed, 36, 7, 4, false);
  check(malformed(changed), "undersized load command is rejected");
  changed = commands; integer(changed, 16, 0, 4, false);
  check(malformed(changed), "unaccounted load command bytes are rejected");
  changed = commands; integer(changed, 20, 4 * 1024 * 1024 + 1, 4, false);
  check(malformed(changed), "excessive load command extent is rejected");
  changed = universal(); integer(changed, 4, 65, 4, true);
  check(malformed(changed), "excessive universal slice count is rejected");
  check(malformed(universal().substr(0, 47)), "truncated universal descriptor table is rejected");
  changed = universal(); integer(changed, 16, 144, 4, true);
  check(malformed(changed), "universal slice extending beyond file is rejected");
  changed = universal(); integer(changed, 36, 96, 4, true);
  check(malformed(changed), "overlapping universal slices are rejected");
  changed = universal(); integer(changed, 16, 32, 4, true);
  check(malformed(changed), "universal slice cannot overlap its descriptor table");
  changed = universal(); integer(changed, 24, 64, 4, true);
  check(malformed(changed), "invalid universal alignment is rejected before shifting");
  changed = universal(2, 8, true); integer(changed, 36, 1, 4, true);
  check(malformed(changed), "fat64 reserved field must be zero");
  changed = universal(); integer(changed, 100, 0x01000007, 4, false);
  check(malformed(changed), "universal descriptor must match its slice CPU type");
  changed = universal(); integer(changed, 104, 0x80000002, 4, false);
  check(accepts(changed), "CPU subtype capabilities are masked as in Apple ld");
  changed = universal(); integer(changed, 48, 0x0100000c, 4, true); integer(changed, 52, 0, 4, true);
  check(malformed(changed), "legacy hidden ARM64 descriptor cannot escape universal inspection");
  fs::remove(path);
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
  if (plan.local_libraries) result += quote("-L/usr/local/lib") + ' ';
  return result + '\n';
}
std::map<std::string, std::string> identities(const std::vector<fs::path> &files) {
  std::map<std::string, std::string> result;
  for (const auto &path : files) result[path.generic_string()] = nil::detail::file_digest(path);
  return result;
}
void parsing(const detail::link_plan &plan, const fs::path &runtime) {
  const auto text = plan_text(plan, runtime);
  const auto parsed = detail::parse_plan(text, runtime);
  check(parsed && parsed->sdk == plan.sdk && parsed->linker == plan.linker &&
        parsed->architecture == "arm64", "real Apple17 plan shape resolves without losing paths");
  auto intel = plan; intel.architecture = "x86_64";
  check(detail::parse_plan(plan_text(intel, runtime), runtime).has_value(), "Intel Apple17 plan resolves");
  auto local = plan; local.local_libraries = true;
  const auto local_plan = detail::parse_plan(plan_text(local, runtime), runtime);
  check(local_plan && local_plan->local_libraries, "observed explicit local library search is modeled");
  auto separate_local = text; separate_local.insert(separate_local.size() - 1, " \"-L\" \"/usr/local/lib\"");
  const auto separate_plan = detail::parse_plan(separate_local, runtime);
  check(separate_plan && separate_plan->local_libraries, "separate-argument local search is equivalent");
  check(detail::supported_archive_commands("Archive : fixture.a\nfixture.a(member.o):\nLoad command 1\ncmd LC_SEGMENT_64\n"),
        "ordinary archive load commands admit the fixed link roots");
  check(!detail::supported_archive_commands("Archive : fixture.a\ncmd LC_LINKER_OPTION\ncount 2\nstring #1 -l\nstring #2 hidden\n"),
        "archive autolinks require additional modeled library roots");
  check(!detail::supported_archive_commands(""), "empty archive inspection cannot certify absent autolinks");
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
  const auto universal_description = detail::parse_image(image,
      image.string() + " (architecture arm64e):\n\t@rpath/arm.dylib (compatibility version 1.0.0, current version 1.0.0)\n" +
      image.string() + " (architecture x86_64):\n\t@rpath/intel.dylib (compatibility version 1.0.0, current version 1.0.0)\n",
      image.string() + " (architecture arm64e):\ncmd LC_RPATH\npath @loader_path/../lib/arm (offset 12)\n" +
      image.string() + " (architecture x86_64):\ncmd LC_RPATH\npath @loader_path/../lib/intel (offset 12)\n");
  check(universal_description && universal_description->libraries.size() == 2 && universal_description->rpaths.size() == 2,
        "all-architecture tool output preserves both dependency and runpath sets");
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
  write(plan.sdk / "usr/lib/libc++.tbd", stub("/usr/lib/libc++.1.dylib"));
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
  write(plan.sdk / "usr/lib/libunrelated.a", "!<arch>\n");
  check(collect().has_value(), "unrelated SDK archive cannot introduce autolinks into fixed direct roots");
  for (const auto *name : {"c++", "System"}) {
    const auto direct = plan.sdk / "usr/lib" / (std::string("lib") + name);
    const auto archive = fs::path(direct.string() + ".a"), text_interface = fs::path(direct.string() + ".tbd");
    write(archive, "!<arch>\n");
    check(collect().has_value(), "same-directory text interface precedes a direct archive fallback");
    const auto text = name == std::string("c++") ? stub("/usr/lib/libc++.1.dylib") : stub();
    auto incompatible = text;
    const std::string compatible_targets = "arm64-macos, x86_64-macos";
    incompatible.replace(incompatible.find(compatible_targets), compatible_targets.size(), "i386-macos");
    write(text_interface, incompatible);
    check(collect().has_value(), "architecture parsing cannot resume path search into a shadowed archive (no link success claim)");
    fs::remove(text_interface);
    std::string archive_reason;
    check(!detail::collector(driver, project, runtime, plan, inspect, host, &archive_reason).files() &&
          archive_reason.find("selected SDK archive") != std::string::npos,
          "removing direct stub exposes archive autolinks and disables reuse");
    fs::remove(archive);
    check(!collect(), "missing direct text root cannot certify successful native reuse");
    write(text_interface, text);
  }
  const auto early_archive = plan.sdk / "usr/local/lib/libc++.a";
  write(early_archive, "!<arch>\n");
  check(collect().has_value(), "implicit SDK usr/lib interface precedes a later local archive");
  plan.local_libraries = true;
  check(!collect(), "explicit SDK-local archive precedes the standard SDK text interface");
  write(plan.sdk / "usr/local/lib/libc++.tbd", stub("/usr/lib/libc++.1.dylib"));
  check(collect().has_value(), "earlier SDK-local text interface safely shadows its archive");
  fs::remove_all(plan.sdk / "usr/local/lib");
  plan.local_libraries = false;
  files = collect(); before = identities(*files);
  for (const auto &path : {driver, plan.linker, plan.lto, plan.compiler_rt, runtime,
       plan.installed.parent_path() / "lib/libtapi.dylib", plan.sdk / "usr/lib/system/libdyld.tbd"})
    check(before.count(path.generic_string()) != 0, "driver, helpers, runtime and transitive stub bytes included");
  const auto shared = plan.installed.parent_path() / "lib/shared.dylib";
  const auto driver_leaf = plan.installed.parent_path() / "lib/driver-set/leaf.dylib";
  const auto linker_leaf = plan.installed.parent_path() / "lib/linker-set/leaf.dylib";
  for (const auto &path : {shared, driver_leaf, linker_leaf}) write(path, "context-specific image");
  descriptions[driver] = {{shared.string()}, {driver_leaf.parent_path().string()}};
  descriptions[plan.linker].libraries.push_back(shared.string());
  descriptions[plan.linker].rpaths.push_back(linker_leaf.parent_path().string());
  descriptions[shared] = {{"@rpath/leaf.dylib"}, {}};
  files = collect();
  check(files && identities(*files).count(driver_leaf.generic_string()) && identities(*files).count(linker_leaf.generic_string()),
        "shared image is resolved in both driver and linker inherited-runpath contexts");
  const auto arm_branch = plan.installed.parent_path() / "lib/arm/branch.dylib";
  const auto intel_branch = plan.installed.parent_path() / "lib/intel/branch.dylib";
  const auto arm_child = arm_branch.parent_path() / "child.dylib";
  const auto intel_child = intel_branch.parent_path() / "child.dylib";
  for (const auto &path : {arm_branch, intel_branch, arm_child, intel_child}) write(path, "slice-specific image");
  descriptions[driver] = {{"@rpath/branch.dylib"}, {arm_branch.parent_path().string(), intel_branch.parent_path().string()}};
  descriptions[arm_branch] = {{"@loader_path/child.dylib"}, {}};
  descriptions[intel_branch] = {{"@loader_path/child.dylib"}, {}};
  files = collect();
  check(files && identities(*files).count(arm_branch.generic_string()) && identities(*files).count(arm_child.generic_string()) &&
        identities(*files).count(intel_branch.generic_string()) && identities(*files).count(intel_child.generic_string()),
        "merged universal runpaths retain both same-name candidates and their separate dependency graphs");
  descriptions[intel_branch].libraries.push_back("/unmodeled/second-slice.dylib");
  check(!collect(), "an unsupported dependency in a later universal runpath candidate fails closed");
  descriptions[intel_branch].libraries.pop_back();
  descriptions[driver] = {};
  descriptions[plan.linker].libraries.pop_back(); descriptions[plan.linker].rpaths.pop_back();
  const auto other_driver = plan.installed.parent_path() / "alternate-bin/clang++";
  const auto executable_leaf = plan.installed.parent_path() / "alternate-bin/helper.dylib";
  write(other_driver, "other driver"); write(executable_leaf, "root-specific helper");
  descriptions[other_driver] = {{"@rpath/helper.dylib"}, {"@executable_path/."}};
  files = detail::collector(other_driver, project, runtime, plan, inspect, host).files();
  check(files && identities(*files).count(executable_leaf.generic_string()), "executable_path follows the current process root");
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
  const auto plugin = plan.sdk / "System/Library/PrivateFrameworks/EmailCore.framework/Versions/A/PlugIns/EmailCore.wkbundle/Contents/MacOS/EmailCore";
  for (const auto kind : {2U, 8U}) {
    write(plugin, macho(kind)); files = collect();
    check(files && !identities(*files).count(plugin.generic_string()), "positively nonlinkable SDK plugin image is not a native link input");
  }
  write(plugin, universal()); files = collect();
  check(files && !identities(*files).count(plugin.generic_string()), "all-nonlinkable universal SDK plugin is omitted");
  for (const auto kind : {1U, 6U, 9U, 0xffffffffU}) {
    write(plugin, macho(kind));
    check(!collect(), "SDK plugin location cannot exempt an object, dylib, stub or unknown Mach-O type");
  }
  write(plugin, macho(8).substr(0, 31));
  check(!collect(), "truncated SDK plugin image is not silently omitted");
  write(plugin, universal(8, 6));
  check(!collect(), "mixed universal plugin cannot hide a linkable SDK slice");
  write(plugin, "!<arch>\n");
  check(!collect(), "extensionless archive in plugin location still disables reuse");
  fs::remove(plugin);
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
  files = collect(); check(files && identities(*files).count(alias.generic_string()), "selected file alias retained");
  before = identities(*files);
  fs::remove(alias); fs::create_symlink("libSystem.tbd", alias);
  files = collect(); check(files && identities(*files) != before, "file alias retarget invalidates even when both old targets exist");
  const auto directory_alias = plan.sdk / "usr/lib/current";
  fs::create_directory_symlink("system", directory_alias);
  files = collect(); check(files && identities(*files).count((directory_alias / "libdyld.tbd").generic_string()),
                          "directory alias preserves its selected child paths");
  fs::create_directory_symlink(".", plan.sdk / "usr/lib/cycle");
  check(!collect(), "directory symlink cycles fail closed within bounds");
  fs::remove(plan.sdk / "usr/lib/cycle");
#endif
  plan.local_libraries = true;
  files = collect();
  check(files.has_value(), "explicit local search permits an absent host directory");
  const auto local = host / "usr/local/lib";
  write(local / "libunrelated.dylib", "unrelated ordinary binary");
  files = collect();
  check(files.has_value(), "unrelated host library cannot become an input of fixed default link roots");
  write(local / "libc++.tbd", stub("/usr/lib/libc++.1.dylib"));
  files = collect();
  check(files && identities(*files).count((local / "libc++.tbd").generic_string()),
        "direct c++ text stub shadow is captured");
  before = identities(*files);
  const auto local_time = fs::last_write_time(local / "libc++.tbd");
  write(local / "libc++.tbd", stub("/usr/lib/libc++.1.dylib") + "# same-time shadow change\n");
  fs::last_write_time(local / "libc++.tbd", local_time);
  files = collect();
  check(files && identities(*files) != before, "host stub bytes invalidate with preserved timestamps");
  before = identities(*files);
  write(local / "libdyld.tbd", stub("/usr/lib/system/libdyld.dylib") +
        "libraries: [ '/usr/lib/extra-dependency.dylib' ]\n");
  write(local / "extra-dependency.tbd", stub("/usr/lib/extra-dependency.dylib"));
  files = collect();
  check(files && identities(*files) != before && identities(*files).count((local / "extra-dependency.tbd").generic_string()),
        "new SDK indirect shadow and its additional re-export are collected to a fixed point");
  fs::remove(local / "extra-dependency.tbd");
  write(local / "extra-dependency.dylib", "unmodeled ordinary dependency");
  check(!collect(), "new transitive ordinary shadow disables reuse");
  fs::remove(local / "extra-dependency.dylib");
  write(local / "libSystem.a", "!<arch>\n");
  check(!collect(), "direct static library shadow requires additional link-input closure");
  fs::remove(local / "libSystem.a");
  write(local / "libdyld.dylib", "unmodeled ordinary shadow");
  check(!collect(), "ordinary SDK indirect library shadow disables reuse");
  write(plan.sdk / "usr/local/lib/sdk-local.tbd", stub("/usr/lib/sdk-local.dylib"));
  files = collect();
  check(files && !identities(*files).count((local / "libc++.tbd").generic_string()) &&
        identities(*files).count((plan.sdk / "usr/local/lib/sdk-local.tbd").generic_string()),
        "SDK local directory reroots explicit search and hides ordinary host shadows");
  fs::remove_all(plan.sdk / "usr/local/lib");
  check(!collect(), "removing SDK local directory observes newly exposed host shadow");
  fs::remove(local / "libdyld.dylib");
#ifndef _WIN32
  const auto local_alias = local / "libSystem.tbd";
  fs::create_symlink("libc++.tbd", local_alias);
  files = collect();
  check(files && identities(*files).count(local_alias.generic_string()), "host candidate alias name is retained");
  before = identities(*files);
  fs::remove(local_alias); fs::create_symlink("libdyld.tbd", local_alias);
  files = collect();
  check(files && identities(*files) != before, "host candidate alias retarget invalidates");
#endif
  plan.local_libraries = false;
  write(local / "libSystem.dylib", "unmodeled host default");
  check(collect().has_value(), "implicit standard local path alone never falls back outside the SDK");
  fs::remove(plan.lto);
  check(!collect(), "missing required linker helper disables reuse");
  std::string reason;
  check(!detail::collector(driver, project, runtime, plan, inspect, host, &reason).files() &&
        reason.find("libLTO.dylib") != std::string::npos,
        "failed dependency closure explains its first missing helper");
  reason.clear();
  detail::explain(&reason, "unsafe\r\n\t" + std::string(2048, 'x'));
  check(reason.size() == 1024 && reason.find_first_of("\r\n\t") == std::string::npos,
        "untrusted tool/path diagnostics are bounded and control sanitized");
  const auto first_reason = reason;
  detail::explain(&reason, "later failure");
  check(reason == first_reason, "later generic failures preserve the first concrete reason");
}
} // namespace

int main() {
  // macOS's temporary parent may be a /var -> /private/var alias. Keep the
  // synthetic toolchain rooted consistently; alias behavior is tested below
  // through deliberate fixture links rather than an incidental host prefix.
  const auto root = fs::canonical(nil::detail::create_private_directory(fs::temp_directory_path(), "nil-darwin-closure-"));
  try {
    machos(root);
    closure(root);
    fs::remove_all(root);
    std::cout << "Darwin toolchain closure: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored; fs::remove_all(root, ignored);
    std::cerr << error.what() << '\n'; return 1;
  }
}
