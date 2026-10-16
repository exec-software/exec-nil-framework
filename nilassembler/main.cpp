#include "codegen.hpp"
#include "nil_loader.hpp"
#include "nil_contract.hpp"
#include "secure_file_io.hpp"
#include "nil_version.hpp"

#ifdef NY_HAS_LLVM
#include "llvm_backend.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <sys/stat.h>
#endif

namespace {

constexpr std::uint64_t maximum_nil_bytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_object_bytes = 512ULL * 1024ULL * 1024ULL;

struct command_line {
  std::filesystem::path input;
  std::filesystem::path output;
  bool help = false;
  bool version = false;
  bool unmanaged = false;
  bool embed_context = false;
};

void print_usage(std::ostream &stream) {
  stream
      << "Usage: nilassembler [-o <output.o>] [--unmanaged] [--embed-context] <input.nil>\n"
      << "\n"
      << "Options:\n"
      << "  -o, --output <path>  Write the host relocatable object to <path>\n"
      << "  --unmanaged          Use manual native memory management\n"
      << "  --embed-context      Retain explicit AI annotations in Linux x86_64 objects\n"
      << "  --version            Show the nilassembler version\n"
      << "  -h, --help           Show this help\n";
}

bool parse_command_line(int argc, char **argv, command_line &result,
                        std::string &error) {
  bool options = true;
  bool output_seen = false;
  bool unmanaged_seen = false;
  bool embed_context_seen = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (options && argument == "--") {
      options = false;
      continue;
    }
    if (options && (argument == "-h" || argument == "--help")) {
      if (result.help) {
        error = "the help option was provided more than once";
        return false;
      }
      result.help = true;
      continue;
    }
    if (options && argument == "--version") {
      if (result.version) {
        error = "the version option was provided more than once";
        return false;
      }
      result.version = true;
      continue;
    }
    if (options && argument == "--unmanaged") {
      if (unmanaged_seen) {
        error = "the unmanaged option was provided more than once";
        return false;
      }
      unmanaged_seen = true;
      result.unmanaged = true;
      continue;
    }
    if (options && argument == "--embed-context") {
      if (embed_context_seen) {
        error = "the embed-context option was provided more than once";
        return false;
      }
      embed_context_seen = true;
      result.embed_context = true;
      continue;
    }
    if (options && (argument == "-o" || argument == "--output")) {
      if (output_seen) {
        error = "the output option was provided more than once";
        return false;
      }
      if (++index >= argc) {
        error = "missing output path after " + argument;
        return false;
      }
      result.output = argv[index];
      if (result.output.empty()) {
        error = "the output path must not be empty";
        return false;
      }
      output_seen = true;
      continue;
    }
    if (options && !argument.empty() && argument.front() == '-') {
      error = "unknown option '" + argument + "'";
      return false;
    }
    if (!result.input.empty()) {
      error = "unexpected extra input '" + argument + "'";
      return false;
    }
    result.input = argument;
  }

  if (result.help || result.version) {
    if (!result.input.empty() || output_seen || unmanaged_seen ||
        embed_context_seen ||
        (result.help && result.version)) {
      error = "--help and --version must be used without input or output paths";
      return false;
    }
    return true;
  }
  if (result.input.empty()) {
    error = "missing executable NIL input";
    return false;
  }
  if (result.output.empty()) {
    result.output = result.input;
#ifdef _WIN32
    result.output.replace_extension(".obj");
#else
    result.output.replace_extension(".o");
#endif
  }
  return true;
}

bool same_path(const std::filesystem::path &left,
               const std::filesystem::path &right) {
  // Lexical normalization does not identify hard links or intermediate
  // directory symlinks (and can misinterpret a symlink followed by "..").
  // Compare filesystem identities without changing the no-follow policy of
  // the actual input reader and output publisher.
  std::error_code error;
  const bool equivalent = std::filesystem::equivalent(left, right, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    throw std::filesystem::filesystem_error(
        "cannot compare input and output file identities", left, right, error);
  }
  // A missing output is expected; a missing input is diagnosed by the reader.
  return !error && equivalent;
}

class private_workspace {
public:
  private_workspace() = default;
  private_workspace(const private_workspace &) = delete;
  private_workspace &operator=(const private_workspace &) = delete;
  private_workspace(private_workspace &&other) noexcept
      : directory_(std::move(other.directory_)),
        object_(std::move(other.object_)) {
    other.directory_.clear();
    other.object_.clear();
  }
  ~private_workspace() {
    std::error_code ignored;
    if (!object_.empty())
      std::filesystem::remove(object_, ignored);
    ignored.clear();
    if (!directory_.empty())
      std::filesystem::remove(directory_, ignored);
  }

  static std::optional<private_workspace>
  create(const std::filesystem::path &output, std::string &error) {
    std::filesystem::path parent = output.parent_path();
    if (parent.empty())
      parent = ".";

    static std::random_device entropy;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      const auto random_bits =
          (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy();
      const auto candidate =
          parent / (".nilassembler-" + std::to_string(random_bits) + "-" +
                    std::to_string(attempt));
#ifdef _WIN32
      std::error_code create_error;
      if (!std::filesystem::create_directory(candidate, create_error))
        continue;
      std::error_code permission_error;
      std::filesystem::permissions(candidate, std::filesystem::perms::owner_all,
                                   std::filesystem::perm_options::replace,
                                   permission_error);
      if (permission_error) {
        std::filesystem::remove(candidate, permission_error);
        error = "could not secure private object staging directory";
        return std::nullopt;
      }
#else
      if (::mkdir(candidate.c_str(), S_IRWXU) != 0) {
        if (errno == EEXIST)
          continue;
        error = "could not create private object staging directory";
        return std::nullopt;
      }
#endif
      private_workspace workspace;
      workspace.directory_ = candidate;
#ifdef _WIN32
      workspace.object_ = candidate / "module.obj";
#else
      workspace.object_ = candidate / "module.o";
#endif
      return workspace;
    }
    error = "could not allocate a unique private object staging directory";
    return std::nullopt;
  }

  const std::filesystem::path &object() const { return object_; }

private:
  std::filesystem::path directory_;
  std::filesystem::path object_;
};

void validate_binary_v1_metadata_boundary(const ny::nil_module &module) {
  std::unordered_map<std::string, std::vector<const ny::nil_function *>>
      functions_by_name;
  for (const auto &function : module.functions)
    functions_by_name[function.name].push_back(&function);

  std::unordered_set<const ny::nil_function *> reachable;
  std::vector<const ny::nil_function *> worklist;
  const auto add_named_functions = [&](const std::string &name) {
    const auto definitions = functions_by_name.find(name);
    if (definitions == functions_by_name.end())
      return;
    for (const auto *function : definitions->second) {
      if (reachable.insert(function).second)
        worklist.push_back(function);
    }
  };

  // Keep this root and direct-CALL closure aligned with NativeCodegen. Invalid
  // or ambiguous calls remain the backend's responsibility; this pass only
  // decides whether metadata omitted by binary NIL v1 is needed by emitted
  // native code. Dead imported library bodies must not constrain an otherwise
  // metadata-independent executable.
  add_named_functions("main");
  add_named_functions("__globals_init__");
  for (std::size_t index = 0; index < worklist.size(); ++index) {
    const auto &function = *worklist[index];
    for (const auto &instruction : function.body) {
      const std::string &opcode = instruction.opname;
      const bool needs_schema =
          opcode == "NEW_STRUCT" || opcode == "GET_FIELD" ||
          opcode == "SET_FIELD" || opcode == "SET_FIELD_PTR" ||
          opcode == "NEW_VARIANT" || opcode == "IS_VARIANT" ||
          opcode == "UNWRAP_VARIANT";
      if (needs_schema) {
        throw std::runtime_error(
            "binary NIL version 1 does not serialize struct/enum schema "
            "metadata required by opcode '" +
            opcode + "' in function '" + function.name +
            "'; emit compiler textual NIL for native assembly");
      }

      // Direct array.length carries its complete concrete ABI in the
      // instruction and is deliberately supported by NativeCodegen. Other
      // compiler intrinsic wrappers rely on their function-level dispatch
      // annotation, which binary NIL v1 does not serialize.
      if (opcode == "INTEROP_CALL" && instruction.args.size() >= 2U &&
          instruction.args[0] == "intrinsic" &&
          instruction.args[1] != "array.length") {
        throw std::runtime_error(
            "binary NIL version 1 does not serialize function annotation "
            "metadata required by native intrinsic '" +
            instruction.args[1] + "' in function '" + function.name +
            "'; emit compiler textual NIL for native assembly");
      }

      if (opcode == "CALL" && !instruction.args.empty()) {
#ifdef NY_HAS_LLVM
        // A declared function takes precedence over legacy builtin dispatch.
        if (!functions_by_name.count(instruction.args.front()) &&
            ny::native_builtin_argument_count(module, instruction.args.front()))
          continue;
#endif
        add_named_functions(instruction.args.front());
      }
    }
  }
}

std::unique_ptr<ny::nil_module>
load_executable_nil(const std::filesystem::path &input) {
  const auto read =
      ny::detail::read_regular_file_bounded(input.string(), maximum_nil_bytes);
  if (!read.ok)
    throw std::runtime_error("cannot read NIL input securely: " + read.error);
  if (read.bytes.empty())
    throw std::runtime_error("NIL input is empty");

  const bool binary = read.bytes.size() >= 4U &&
                      std::memcmp(read.bytes.data(), "NIL\0", 4) == 0;
  if (!binary && std::find(read.bytes.begin(), read.bytes.end(), '\0') !=
                     read.bytes.end()) {
    throw std::runtime_error(
        "input contains binary data without current NIL binary magic");
  }
  const std::string contents(read.bytes.begin(), read.bytes.end());
  std::istringstream stream(contents, std::ios::in | std::ios::binary);
  auto module = binary ? ny::parseBinaryNilModule(stream)
                       : ny::parseTextNilModule(stream);
  // The parser has already checked the complete little-endian version word.
  // V3 carries these records; only old v1 artifacts need the omission guard.
  if (binary && read.bytes[4] == ny::legacy_executable_binary_nil_version)
    validate_binary_v1_metadata_boundary(*module);
  if (module->module_name.empty()) {
    throw std::runtime_error(
        "executable NIL is missing its compiler-emitted module declaration");
  }
  if (module->functions.empty())
    throw std::runtime_error("executable NIL contains no functions");
  for (const auto &function : module->functions) {
    if (function.return_type.empty()) {
      throw std::runtime_error("function '" + function.name +
                               "' lacks return-type metadata; recompile with "
                               "the current nilcompiler");
    }
  }
  return module;
}

int run_assembler(const command_line &arguments) {
#ifndef NY_HAS_LLVM
  (void)arguments;
  std::cerr << "nilassembler: native object emission is unavailable because "
               "this NIL build has no LLVM backend\n";
  return 1;
#else
  if (same_path(arguments.input, arguments.output)) {
    std::cerr << "nilassembler: input and output paths must differ\n";
    return 2;
  }

  std::unique_ptr<ny::nil_module> module;
  try {
    module = load_executable_nil(arguments.input);
  } catch (const std::exception &error) {
    std::cerr << "nilassembler: " << error.what() << '\n';
    return 1;
  }

  std::string staging_error;
  auto workspace = private_workspace::create(arguments.output, staging_error);
  if (!workspace) {
    std::cerr << "nilassembler: " << staging_error << '\n';
    return 1;
  }

  ny::NativeCodegen native;
  native.setMemoryMode(arguments.unmanaged ? ny::MemoryMode::UNSAFE_MANUAL
                                           : ny::MemoryMode::SAFE_GC);
  native.setEmbedContext(arguments.embed_context);
  if (!native.initialize()) {
    std::cerr << "nilassembler: failed to initialize the LLVM target\n";
    return 1;
  }
  if (!native.generateModule(*module)) {
    std::cerr << "nilassembler: native lowering failed\n";
    return 1;
  }
  if (!native.emitObjectFile(workspace->object().string())) {
    std::cerr << "nilassembler: object emission failed\n";
    return 1;
  }

  const auto object = ny::detail::read_regular_file_bounded(
      workspace->object().string(), maximum_object_bytes);
  if (!object.ok || object.bytes.empty()) {
    std::cerr << "nilassembler: staged object validation failed: "
              << (object.error.empty() ? "object is empty" : object.error)
              << '\n';
    return 1;
  }
  // Recheck after lowering in case an alias was introduced while generating
  // the object. Publication itself retains the shared no-follow/atomic I/O
  // contract; path-based checks do not lock a concurrently mutable namespace.
  if (same_path(arguments.input, arguments.output)) {
    std::cerr << "nilassembler: input and output paths must differ\n";
    return 2;
  }
  const auto publish = ny::detail::write_file_atomic(arguments.output.string(),
                                                     object.bytes, true);
  if (!publish.ok) {
    std::cerr << "nilassembler: could not publish object atomically: "
              << publish.error << '\n';
    return 1;
  }

  std::cout << "Assembled " << arguments.input.string() << " to "
            << arguments.output.string() << '\n';
  return 0;
#endif
}

} // namespace

int main(int argc, char **argv) {
  command_line arguments;
  std::string error;
  if (!parse_command_line(argc, argv, arguments, error)) {
    std::cerr << "nilassembler: " << error << '\n';
    print_usage(std::cerr);
    return 2;
  }
  if (arguments.help) {
    print_usage(std::cout);
    return 0;
  }
  if (arguments.version) {
    std::cout << "nilassembler " << NIL_VERSION << '\n';
    return 0;
  }
  try {
    return run_assembler(arguments);
  } catch (const std::exception &error) {
    std::cerr << "nilassembler: unexpected failure: " << error.what() << '\n';
    return 1;
  }
}
