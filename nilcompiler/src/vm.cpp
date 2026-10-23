#include "vm.hpp"
#include "concurrency_contract.hpp"
#include "owned_contract.hpp"
#include "parallel_support.hpp"
#include "codegen.hpp"
#include "array_intrinsic.hpp"
#include "matrix_hag_bridge.hpp"
#include "nil_contract.hpp"
#include "numeric_literal.hpp"
#include "scalar_text.hpp"
#include "integer_conversion.hpp"
#include "secure_file_io.hpp"
#include "time_support.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem> // Added by instruction
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#else
#include <io.h>
#define ssize_t SSIZE_T
#endif
#include <unordered_map>

#ifndef _WIN32
extern char **environ;
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef NY_HAS_POSTGRESQL
#include <libpq-fe.h>
#endif

namespace ny {

namespace {

constexpr std::uint64_t max_intrinsic_file_bytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t max_environment_entries = 4096;
constexpr std::size_t max_environment_entry_bytes = 1024 * 1024;
constexpr std::size_t max_environment_total_bytes = 8 * 1024 * 1024;
constexpr std::uint64_t heap_allocation_guard_bytes = 8;

bool is_byte_heap_type(const std::string &type_name) {
  return type_name == "bytes" || type_name == "u8";
}

bool is_array_storage_type(const std::string &type_name) {
  return (type_name.rfind("array<", 0) == 0 && type_name.back() == '>') ||
         (type_name.size() >= 2 &&
          type_name.compare(type_name.size() - 2, 2, "[]") == 0);
}

std::uint64_t heap_element_stride(const std::string &type_name) {
  if (type_name == "bytes" || type_name == "u8" || type_name == "bool")
    return 1;
  if (type_name == "i32")
    return 4;
  if (type_name == "i64")
    return 8;
  return 8;
}

void account_heap_allocation(std::uint64_t &total, std::uint64_t bytes) {
  const auto observable_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const auto increment = std::min(bytes, observable_limit);
  if (total > observable_limit - increment)
    total = observable_limit;
  else
    total += increment;
}

using detail::IntegerParseResult;
using detail::parse_signed_integer;

std::string format_signed_integer(std::int64_t value, int base,
                                  bool uppercase) {
  static constexpr char lower_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  static constexpr char upper_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char *digits = uppercase ? upper_digits : lower_digits;
  const bool negative = value < 0;
  std::uint64_t magnitude = negative
                                ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                                : static_cast<std::uint64_t>(value);
  std::string result;
  do {
    result.push_back(digits[magnitude % static_cast<std::uint64_t>(base)]);
    magnitude /= static_cast<std::uint64_t>(base);
  } while (magnitude != 0);
  if (negative)
    result.push_back('-');
  std::reverse(result.begin(), result.end());
  return result;
}

std::optional<double> parse_finite_f64(const std::string &text) {
  if (text.empty())
    return std::nullopt;
  std::istringstream input(text);
  input.imbue(std::locale::classic());
  input >> std::noskipws;
  double value = 0.0;
  if (!(input >> value) || !input.eof() || !std::isfinite(value))
    return std::nullopt;
  return value;
}

std::optional<bool> parse_bool_text(const std::string &text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const unsigned char byte : text)
    normalized.push_back(static_cast<char>(std::tolower(byte)));
  if (normalized == "true" || normalized == "1" || normalized == "yes" ||
      normalized == "on")
    return true;
  if (normalized == "false" || normalized == "0" || normalized == "no" ||
      normalized == "off")
    return false;
  return std::nullopt;
}

std::optional<std::int32_t> decode_single_utf8_scalar(const std::string &text) {
  if (text.empty() || text.size() > 4)
    return std::nullopt;
  const auto byte = [&](std::size_t index) {
    return static_cast<unsigned char>(text[index]);
  };
  const unsigned char first = byte(0);
  std::size_t length = 0;
  std::uint32_t value = 0;
  std::uint32_t minimum = 0;
  if (first <= 0x7f) {
    length = 1;
    value = first;
  } else if ((first & 0xe0U) == 0xc0U) {
    length = 2;
    value = first & 0x1fU;
    minimum = 0x80;
  } else if ((first & 0xf0U) == 0xe0U) {
    length = 3;
    value = first & 0x0fU;
    minimum = 0x800;
  } else if ((first & 0xf8U) == 0xf0U) {
    length = 4;
    value = first & 0x07U;
    minimum = 0x10000;
  } else {
    return std::nullopt;
  }
  if (text.size() != length)
    return std::nullopt;
  for (std::size_t index = 1; index < length; ++index) {
    const unsigned char continuation = byte(index);
    if ((continuation & 0xc0U) != 0x80U)
      return std::nullopt;
    value = (value << 6U) | (continuation & 0x3fU);
  }
  if (value < minimum || value > 0x10ffffU ||
      (value >= 0xd800U && value <= 0xdfffU))
    return std::nullopt;
  return static_cast<std::int32_t>(value);
}

std::optional<std::string> encode_utf8_scalar(std::int64_t value) {
  if (value < 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
    return std::nullopt;
  std::string result;
  if (value <= 0x7f) {
    result.push_back(static_cast<char>(value));
  } else if (value <= 0x7ff) {
    result.push_back(static_cast<char>(0xc0 | (value >> 6)));
    result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  } else if (value <= 0xffff) {
    result.push_back(static_cast<char>(0xe0 | (value >> 12)));
    result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
    result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  } else {
    result.push_back(static_cast<char>(0xf0 | (value >> 18)));
    result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
    result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
    result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
  }
  return result;
}

bool valid_environment_name(const std::string &name) {
  return !name.empty() && name.size() <= 32767 &&
         name.find('=') == std::string::npos &&
         name.find('\0') == std::string::npos;
}
} // namespace

// Free function implementation for invoking a NyForeignFunc. Defined here to
// avoid circular dependencies in the header (Value depends on NyForeignFunc
// and vice-versa).
Value callNyForeignFunc(const ny::NyForeignFunc &ff, const std::vector<Value> &args, VM *vm) {
  if (!ff.fn_ptr) {
    throw std::runtime_error("NyForeignFunc: null function pointer");
  }
  using BridgeFn = Value(*)(const std::vector<Value>&, VM*);
  BridgeFn bridge = reinterpret_cast<BridgeFn>(ff.fn_ptr);
  try {
    return bridge(args, vm);
  } catch (const std::exception &e) {
    std::cerr << "[NyForeignFunc] call exception: " << e.what() << std::endl;
    throw;
  } catch (...) {
    std::cerr << "[NyForeignFunc] call unknown exception" << std::endl;
    throw;
  }
}

// Helper: convert ArrayValue element (narrow variant without NyForeignFunc) to Value
using ArrayElem = std::variant<std::string, int64_t, double, StructValue, ArrayValue, EnumValue, ArrayHandle>;
static inline Value array_elem_to_value(const ArrayElem &e) {
  return std::visit([](auto &&v) -> Value { return v; }, e);
}
static bool numeric_value(const Value &value, double &result) {
  if (const auto *integer = std::get_if<int64_t>(&value)) {
    result = static_cast<double>(*integer);
    return true;
  }
  if (const auto *floating = std::get_if<double>(&value)) {
    result = *floating;
    return true;
  }
  return false;
}

// Interpret unsigned bits without an overflowing signed operation or an
// implementation-defined unsigned-to-signed conversion.
static std::int64_t scalar_integer_bits(std::uint64_t bits, unsigned width,
                                        bool is_unsigned) {
  if (width < 64)
    bits &= (std::uint64_t(1) << width) - 1;
  if (is_unsigned)
    return static_cast<std::int64_t>(bits);
  if (width == 64)
    return bits <= std::uint64_t(INT64_MAX)
        ? static_cast<std::int64_t>(bits)
        : -1 - static_cast<std::int64_t>(~bits);
  const auto value = static_cast<std::int64_t>(bits);
  return bits & (std::uint64_t(1) << (width - 1))
      ? value - (std::int64_t(1) << width) : value;
}

static Value scalar_arithmetic(const Instruction &instruction,
                               const Value &left, const Value &right) {
  const auto op = instruction.op;
  const bool unary = op == Opcode::NEG;
  const bool bitwise = op == Opcode::BIT_AND || op == Opcode::BIT_OR ||
      op == Opcode::BIT_XOR || op == Opcode::SHL || op == Opcode::SHR;
  if (instruction.args.size() > 1)
    throw std::runtime_error("Arithmetic requires at most one scalar type operand");
  std::string type = instruction.args.empty() ? "" : instruction.args.front();
  if (type.empty() && instruction.args.empty())
    type = std::holds_alternative<double>(left) ||
               (!unary && std::holds_alternative<double>(right)) ? "f64" : "i64";
  if (type != "i32" && type != "i64" && type != "u8" &&
      !(type == "f64" && !bitwise))
    throw std::runtime_error("Unsupported scalar arithmetic type '" + type + "'");
  if (type == "f64") {
    double a = 0.0, b = 0.0;
    if (!numeric_value(left, a) || (!unary && !numeric_value(right, b)))
      throw std::runtime_error("Arithmetic operands must be numeric scalars");
    if ((op == Opcode::DIV || op == Opcode::MOD) && b == 0.0)
      throw std::runtime_error(op == Opcode::DIV ? "Division by zero" : "Modulo by zero");
    switch (op) {
    case Opcode::ADD: return a + b;
    case Opcode::SUB: return a - b;
    case Opcode::MUL: return a * b;
    case Opcode::DIV: return a / b;
    case Opcode::MOD: return std::fmod(a, b);
    case Opcode::NEG: return -a;
    default: throw std::runtime_error("Invalid floating arithmetic operation");
    }
  }
  if (!std::holds_alternative<int64_t>(left) ||
      (!unary && !std::holds_alternative<int64_t>(right)))
    throw std::runtime_error("Integer arithmetic operands must be integers");
  const unsigned width = type == "i32" ? 32 : type == "u8" ? 8 : 64;
  const bool is_unsigned = type == "u8";
  const auto a = scalar_integer_bits(std::uint64_t(std::get<int64_t>(left)),
                                     width, is_unsigned);
  const auto raw_b = unary ? int64_t(0) : std::get<int64_t>(right);
  const auto b = scalar_integer_bits(std::uint64_t(raw_b), width, is_unsigned);
  const auto ua = std::uint64_t(a), ub = std::uint64_t(b);
  std::uint64_t result = 0;
  switch (op) {
  case Opcode::ADD: result = ua + ub; break;
  case Opcode::SUB: result = ua - ub; break;
  case Opcode::MUL: result = ua * ub; break;
  case Opcode::NEG: result = std::uint64_t(0) - ua; break;
  case Opcode::BIT_AND: result = ua & ub; break;
  case Opcode::BIT_OR: result = ua | ub; break;
  case Opcode::BIT_XOR: result = ua ^ ub; break;
  case Opcode::DIV:
  case Opcode::MOD: {
    if (b == 0)
      throw std::runtime_error(op == Opcode::DIV ? "Division by zero" : "Modulo by zero");
    const auto minimum = width == 64 ? std::numeric_limits<int64_t>::min()
                                    : -(int64_t(1) << (width - 1));
    if (!is_unsigned && a == minimum && b == -1)
      throw std::runtime_error(op == Opcode::DIV ? "Integer division overflow"
                                                : "Integer modulo overflow");
    result = std::uint64_t(op == Opcode::DIV ? a / b : a % b);
    break;
  }
  case Opcode::SHL:
  case Opcode::SHR: {
    // Validate the full count before narrowing it to the left operand width.
    if (raw_b < 0 || std::uint64_t(raw_b) >= width)
      throw std::runtime_error("Shift count out of range");
    const auto count = static_cast<unsigned>(raw_b);
    if (op == Opcode::SHL) result = ua << count;
    else {
      result = ua >> count;
      if (!is_unsigned && a < 0 && count != 0)
        result |= ~std::uint64_t(0) << (64 - count);
    }
    break;
  }
  default: throw std::runtime_error("Invalid integer arithmetic operation");
  }
  return scalar_integer_bits(result, width, is_unsigned);
}

struct BuiltinCallArity {
  std::size_t minimum;
  std::size_t maximum;
};

static std::optional<BuiltinCallArity>
builtin_call_arity(const std::string &name) {
  if (name == "panic" || name == "ny__lang__core__panic") {
    return BuiltinCallArity{1, 1};
  }
  if (name == "core__to_string" || name == "core__to_string_i64" ||
      name == "core__to_string_f64" ||
      name == "f64_to_str" ||
      name.find("__f64_to_str") != std::string::npos ||
      name.find("vm__get_address") != std::string::npos ||
      name == "String__len" || name == "String__length" ||
      name == "String__to_uppercase" || name == "String__to_upper" ||
      name == "String__to_lowercase" || name == "String__to_lower" ||
      name == "String__trim" || name == "array__len" ||
      name == "array__length" || name == "__ritual_len" ||
      name == "__ritual_iter" || name == "__ritual_next" ||
      name == "__ritual_is_done" || name == "__ritual_value") {
    return BuiltinCallArity{1, 1};
  }
  if (name == "String__contains" || name == "String__starts_with" ||
      name == "String__ends_with" || name == "array__push" ||
      name == "array__get" || name == "__ritual_index" ||
      name == "__ritual_tuple_get") {
    return BuiltinCallArity{2, 2};
  }
  if (name == "String__find" || name == "String__substring") {
    return BuiltinCallArity{2, 3};
  }
  if (name == "String__substr" || name == "String__replace" ||
      name == "array__set") {
    return BuiltinCallArity{3, 3};
  }
  return std::nullopt;
}

static void validate_builtin_call_arity(const std::string &name,
                                        int argument_count) {
  const auto arity = builtin_call_arity(name);
  if (!arity ||
      (argument_count >= 0 &&
       static_cast<std::size_t>(argument_count) >= arity->minimum &&
       static_cast<std::size_t>(argument_count) <= arity->maximum)) {
    return;
  }

  std::string expectation;
  if (arity->minimum == arity->maximum) {
    expectation = std::to_string(arity->minimum) + " argument";
    if (arity->minimum != 1)
      expectation += "s";
  } else {
    expectation = std::to_string(arity->minimum) + " to " +
                  std::to_string(arity->maximum) + " arguments";
  }
  throw std::runtime_error("CALL: builtin '" + name + "' expects " +
                           expectation + ", got " +
                           std::to_string(argument_count));
}

static std::string canonical_variant_name(std::string name) {
  // The parser accepts both Enum.Variant and Enum::Variant. NIL uses one
  // spelling so construction and matching remain representation-independent.
  for (std::size_t dot = name.find('.'); dot != std::string::npos;
       dot = name.find('.', dot + 2)) {
    name.replace(dot, 1, "::");
  }

  const auto variant_separator = name.rfind("::");
  if (variant_separator == std::string::npos)
    return name;
  const auto enum_separator =
      variant_separator == 0
          ? std::string::npos
          : name.rfind("::", variant_separator - 1);
  const std::size_t enum_start =
      enum_separator == std::string::npos ? 0 : enum_separator + 2;

  std::string enum_name =
      name.substr(enum_start, variant_separator - enum_start);
  std::string variant_name = name.substr(variant_separator + 2);
  std::transform(enum_name.begin(), enum_name.end(), enum_name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  std::transform(variant_name.begin(), variant_name.end(), variant_name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  std::string canonical_enum;
  std::string canonical_variant;
  if (enum_name == "option") {
    canonical_enum = "Option";
    if (variant_name == "some")
      canonical_variant = "Some";
    else if (variant_name == "none")
      canonical_variant = "None";
  } else if (enum_name == "result") {
    canonical_enum = "Result";
    if (variant_name == "ok")
      canonical_variant = "Ok";
    else if (variant_name == "error" || variant_name == "err")
      canonical_variant = "Error";
  }

  if (canonical_variant.empty())
    return name;
  name.replace(enum_start, variant_separator - enum_start, canonical_enum);
  const auto new_variant_separator = name.rfind("::");
  name.replace(new_variant_separator + 2, std::string::npos,
               canonical_variant);
  return name;
}

// FFI Infrastructure
namespace ffi {

// DLL/shared library handle cache
static std::unordered_map<std::string, void *> dll_cache;

// Function pointer cache: "lib::func" -> pointer
static std::unordered_map<std::string, void *> func_cache;

// Hot-reload tracking
struct HotDllInfo {
  std::string source_path;
  std::string current_shadow;
  std::filesystem::file_time_type source_mtime;
  int shadow_idx{0};
};
static std::unordered_map<std::string, HotDllInfo> hot_dll_info;
static std::chrono::steady_clock::time_point last_hot_check{};
static bool hot_check_initialized = false;

static std::string make_shadow_path(const std::string &source, int idx) {
  namespace fs = std::filesystem;
  fs::path p(source);
  return (p.parent_path() / (p.stem().string() + ".hot" + std::to_string(idx) + p.extension().string())).string();
}

static void *load_with_shadow(const std::string &name, const std::string &source_path) {
  namespace fs = std::filesystem;
  std::string shadow = make_shadow_path(source_path, 0);
  std::error_code ec;
  fs::copy_file(source_path, shadow, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    // Shadow copy failed — load directly (no hot-reload for this lib)
    std::cerr << "[FFI] Shadow copy failed (" << ec.message() << "), loading directly\n";
#ifdef _WIN32
    void *h = LoadLibraryA(source_path.c_str());
#else
    void *h = dlopen(source_path.c_str(), RTLD_LAZY);
#endif
    return h;
  }
#ifdef _WIN32
  void *handle = LoadLibraryA(shadow.c_str());
#else
  void *handle = dlopen(shadow.c_str(), RTLD_LAZY);
#endif
  if (handle) {
    hot_dll_info[name] = {source_path, shadow, fs::last_write_time(source_path), 0};
  }
  return handle;
}

// Forward-declare so maybe_hot_reload_all can call it
void *get_function(const std::string &lib_name, const std::string &fn_name);

static void maybe_hot_reload_all() {
  namespace fs = std::filesystem;
  if (!hot_check_initialized) {
    last_hot_check = std::chrono::steady_clock::now();
    hot_check_initialized = true;
    return;
  }
  auto now = std::chrono::steady_clock::now();
  if (now - last_hot_check < std::chrono::seconds(2)) return;
  last_hot_check = now;

  for (auto &[name, info] : hot_dll_info) {
    std::error_code ec;
    if (!fs::exists(info.source_path, ec)) continue;
    auto current_mtime = fs::last_write_time(info.source_path, ec);
    if (ec || current_mtime <= info.source_mtime) continue;

    int new_idx = 1 - info.shadow_idx;
    std::string new_shadow = make_shadow_path(info.source_path, new_idx);
    fs::copy_file(info.source_path, new_shadow, fs::copy_options::overwrite_existing, ec);
    if (ec) { std::cerr << "[HotReload] Copy failed: " << ec.message() << "\n"; continue; }

#ifdef _WIN32
    void *new_handle = LoadLibraryA(new_shadow.c_str());
#else
    void *new_handle = dlopen(new_shadow.c_str(), RTLD_LAZY);
#endif
    if (!new_handle) { std::cerr << "[HotReload] Load failed: " << new_shadow << "\n"; continue; }

    // Evict function cache entries for this lib
    std::string prefix = name + "::";
    for (auto fit = func_cache.begin(); fit != func_cache.end();) {
      if (fit->first.compare(0, prefix.size(), prefix) == 0) fit = func_cache.erase(fit);
      else ++fit;
    }

    // Free old shadow
    void *old_handle = dll_cache[name];
#ifdef _WIN32
    if (old_handle) FreeLibrary((HMODULE)old_handle);
#else
    if (old_handle) dlclose(old_handle);
#endif

    dll_cache[name] = new_handle;
    info.current_shadow = new_shadow;
    info.source_mtime = current_mtime;
    info.shadow_idx = new_idx;
    std::cerr << "[HotReload] Reloaded " << name << "\n";

  }
}

// Load a library (with .dll extension on Windows), shadow-copied for hot-reload
void *load_library(const std::string &name) {
  auto it = dll_cache.find(name);
  if (it != dll_cache.end())
    return it->second;

  std::string lib_path = name;
#ifdef _WIN32
  lib_path += ".dll";
#else
  lib_path = "lib" + name + ".so";
#endif

  void *handle = nullptr;

#ifdef _WIN32
  // Load once to discover the real on-disk path via GetModuleFileNameA
  HMODULE probe = LoadLibraryA(lib_path.c_str());
  if (!probe) {
    std::cerr << "[FFI] Failed to load library: " << lib_path << std::endl;
    return nullptr;
  }
  char real_buf[MAX_PATH];
  GetModuleFileNameA(probe, real_buf, MAX_PATH);
  std::string source_path = real_buf;
  FreeLibrary(probe);

  // Shadow-copy so the original file is never locked by us
  handle = load_with_shadow(name, source_path);
  if (!handle) {
    // Shadow failed; fall back to direct load (no hot-reload)
    handle = LoadLibraryA(source_path.c_str());
  }
#else
  void *probe = dlopen(lib_path.c_str(), RTLD_LAZY);
  if (!probe) {
    std::cerr << "[FFI] Failed to load library: " << lib_path << std::endl;
    return nullptr;
  }
  // On Linux dlinfo could give us the path; skip shadow for now
  handle = probe;
#endif

  if (!handle) {
    std::cerr << "[FFI] Failed to load library: " << lib_path << std::endl;
    return nullptr;
  }

  std::cerr << "[FFI] Loaded library: " << lib_path << std::endl;
  dll_cache[name] = handle;
  return handle;
}

// Get function pointer from library
void *get_function(const std::string &lib_name, const std::string &fn_name) {
  std::string key = lib_name + "::" + fn_name;
  auto it = func_cache.find(key);
  if (it != func_cache.end())
    return it->second;

  void *lib = load_library(lib_name);
  if (!lib)
    return nullptr;

#ifdef _WIN32
  void *fn = (void *)GetProcAddress((HMODULE)(dll_cache[lib_name]), fn_name.c_str());
#else
  void *fn = dlsym(dll_cache[lib_name], fn_name.c_str());
#endif

  if (!fn) {
    std::cerr << "[FFI] Function not found: " << fn_name << " in " << lib_name
              << std::endl;
    return nullptr;
  }

  func_cache[key] = fn;
  return fn;
}

} // namespace ffi

// --- FFI Dispatch Helpers ---
// Generic bit cast for same-size types (double, int64_t)
template <typename T> T bit_cast_from_i64(int64_t bits) {
  T val;
  std::memcpy(&val, &bits, sizeof(T));
  return val;
}

// Specialization for float - extract from lower 32 bits
template <> inline float bit_cast_from_i64<float>(int64_t bits) {
  float val;
  int32_t lower_bits = static_cast<int32_t>(bits);
  std::memcpy(&val, &lower_bits, sizeof(float));
  return val;
}

// Helper to pack a float into an int64_t (in the lower 32 bits)
inline int64_t pack_float_bits(float f) {
  int32_t bits;
  std::memcpy(&bits, &f, sizeof(float));
  return static_cast<int64_t>(bits);
}

// Helper to pack a double into an int64_t
inline int64_t pack_double_bits(double d) {
  int64_t bits;
  std::memcpy(&bits, &d, sizeof(double));
  return bits;
}

template <int Mask>
int64_t invoke_native(void *fn_ptr, const std::vector<int64_t> &args) {
  using T0 = std::conditional_t<((Mask & 1) != 0), double, int64_t>;
  using T1 = std::conditional_t<((Mask & 2) != 0), double, int64_t>;
  using T2 = std::conditional_t<((Mask & 4) != 0), double, int64_t>;
  using T3 = std::conditional_t<((Mask & 8) != 0), double, int64_t>;

  using Fn = int64_t (*)(T0, T1, T2, T3, int64_t, int64_t, int64_t, int64_t,
                         int64_t, int64_t, int64_t, int64_t);
  auto fn = reinterpret_cast<Fn>(fn_ptr);

  return fn(bit_cast_from_i64<T0>(args[0]), bit_cast_from_i64<T1>(args[1]),
            bit_cast_from_i64<T2>(args[2]), bit_cast_from_i64<T3>(args[3]),
            args[4], args[5], args[6], args[7], args[8], args[9], args[10],
            args[11]);
}
// ----------------------------

VM::VM() : pc_(0) {}

void VM::beginMemoryProfile() {
  if (memory_profile_started_ || !memory_profile_allowed_)
    return;
  memory_profile_started_ = true;
  if (!memory_profile_.begin("vm"))
    throw std::runtime_error("Could not start Aperture memory profile");
}

bool VM::finishMemoryProfile(const char *status, bool completed) noexcept {
  if (!memory_profile_started_ || memory_profile_finished_)
    return true;
  memory_profile_finished_ = true;
  return memory_profile_.finish(status, completed);
}

std::istream &VM::inputStream() {
  return input_stream_ ? *input_stream_ : std::cin;
}

std::ostream &VM::outputStream() {
  return output_stream_ ? *output_stream_ : std::cout;
}

void VM::resetRuntimeState(const std::string &root_function) {
  // Configuration belongs to the host and survives a restart. Everything that
  // can be observed or owned by the Ny program belongs to one invocation.
  // A restart abandons a paused invocation; it must not combine its allocation
  // history with the next run or count teardown as program releases.
  if (!finishMemoryProfile("abandoned", false))
    throw std::runtime_error("Could not finish Aperture memory profile");
  unwindOwned(0);
  databases_.clear();
  file_handles_.clear();
  next_file_handle_ = 1;
  stack_.clear();
  globals_.clear();
  gc_marked_structs_.clear();
  heap_.clear();
  next_ptr_ = 0x400000000LL;
  alloc_count_ = 0;
  total_collections_ = 0;
  total_allocated_bytes_ = 0;
  gc_critical_depth_ = 0;
  requested_exit_code_.reset();
  call_stack_.clear();
  pending_fault_.reset();
  saved_faults_.clear();
  fault_regions_.clear();
  pushFrame(root_function, program_.size());
  pc_ = 0;
  debug_state_ = DebugState::PAUSED;
}

void VM::loadProgram(const std::vector<Instruction> &program,
                     const std::unordered_map<std::string, Value> &constants) {
  validate_nil_parallel_contract(program);
  validate_nil_owned_contract(program);
  validate_nil_fault_contract(program);
  validate_nil_class_contract(program);
  program_ = program;
  class_metadata_ = program.empty() ? nullptr : program.front().module_metadata;
  constants_ = constants;
  labels_.clear();
  functions_.clear();
  breakpoints_.clear();
  std::string open_function;
  for (size_t i = 0; i < program_.size(); ++i) {
    if (program_[i].op == Opcode::LABEL) {
      if (program_[i].args.empty()) {
        throw std::runtime_error("LABEL instruction has no name at program index " +
                                 std::to_string(i));
      }
      const auto [ignored, inserted] = labels_.emplace(program_[i].args[0], i);
      (void)ignored;
      if (!inserted) {
        throw std::runtime_error("Duplicate NIL label '" +
                                 program_[i].args[0] + "'");
      }
      if (program_[i].is_function_entry) {
        if (!open_function.empty())
          functions_.at(open_function).end_pc = i;
        open_function = program_[i].args[0];
        functions_.emplace(
            program_[i].args[0],
            FunctionInfo{i, program_[i].function_param_count,
                         program_[i].function_return_type, program_.size(),
                         program_[i].function_parameter_types});
      } else if (program_[i].is_function_end && !open_function.empty()) {
        functions_.at(open_function).end_pc = i;
        open_function.clear();
      }
    }
  }

  resetRuntimeState("<root>");
}

void VM::resetExecution(const std::string &entry_point) {
  const auto function = functions_.find(entry_point);
  if (function == functions_.end()) {
    throw std::runtime_error("Entry function '" + entry_point +
                             "' was not found");
  }
  if (function->second.param_count != 0) {
    throw std::runtime_error(
        "Entry function '" + entry_point + "' must declare 0 parameters, found " +
        std::to_string(function->second.param_count));
  }

  resetRuntimeState(entry_point);
  pc_ = function->second.entry_pc;
}

bool VM::stepInstruction() try {
  validateInstructionLocation();
  if (pc_ >= program_.size())
    return false;
  beginMemoryProfile();
  executeInstruction(program_[pc_]);
  ++pc_;
  validateInstructionLocation();
  debug_state_ = DebugState::PAUSED;
  if (pc_ >= program_.size() &&
      !finishMemoryProfile("completed", true))
    throw std::runtime_error("Could not finish Aperture memory profile");
  return pc_ < program_.size();
} catch (...) {
  (void)finishMemoryProfile("failed", false);
  unwindOwned(0);
  throw;
}

std::vector<VM::DebugFrameSnapshot> VM::getDebugFrames() const {
  std::vector<DebugFrameSnapshot> frames;
  frames.reserve(call_stack_.size());
  for (const auto &frame : call_stack_) {
    frames.push_back(
        DebugFrameSnapshot{frame.fn_name, frame.return_pc, frame.locals});
  }
  return frames;
}

void VM::execute(bool reset) try {
  beginMemoryProfile();
  if (reset) {
    // Preserve the historical embedding contract: callers may load a program,
    // inject globals with setGlobal(), and then use execute(true).  A debugger
    // that needs a pristine process-style restart uses resetExecution().
    auto main_it = functions_.find("main");
    if (main_it != functions_.end()) {
      if (main_it->second.param_count != 0) {
        throw std::runtime_error(
            "Entry function 'main' must declare 0 parameters, found " +
            std::to_string(main_it->second.param_count));
      }
      pc_ = main_it->second.entry_pc;
      if (!call_stack_.empty()) {
        call_stack_.back().fn_name = "main";
        call_stack_.back().return_type = main_it->second.return_type;
        call_stack_.back().entry_pc = main_it->second.entry_pc;
        call_stack_.back().end_pc = main_it->second.end_pc;
        call_stack_.back().returned = false;
      }
    } else {
      pc_ = 0;
    }
    debug_state_ = DebugState::RUNNING;
  }

  while (pc_ < program_.size()) {
    validateInstructionLocation();
    // 1. Check Breakpoints
    // If we are RUNNING and hit a breakpoint, pause.
    // Logic for "continue" from a breakpoint must be handled by caller (step
    // over then run)
    if (debug_state_ == DebugState::RUNNING && hasBreakpoint(pc_)) {
      debug_state_ = DebugState::PAUSED;
      bool should_continue = false;
      if (debug_callback_) {
        should_continue = debug_callback_(*this, program_[pc_], pc_);
      }
      if (!should_continue)
        return;
      debug_state_ = DebugState::RUNNING;
    }

    // 2. Step Mode
    if (debug_state_ == DebugState::STEPPING) {
      executeInstruction(program_[pc_]);
      ++pc_;
      validateInstructionLocation();
      debug_state_ = DebugState::PAUSED;
      if (pc_ >= program_.size()) {
        if (!finishMemoryProfile("completed", true))
          throw std::runtime_error("Could not finish Aperture memory profile");
        return;
      }
      bool should_continue = false;
      if (debug_callback_) {
        // Callback with NEXT instruction (if valid)
        if (pc_ < program_.size()) {
          should_continue = debug_callback_(*this, program_[pc_], pc_);
        }
      }
      if (!should_continue)
        return;
      debug_state_ = DebugState::RUNNING;
    }

    executeInstruction(program_[pc_]);
    ++pc_;
    validateInstructionLocation();
  }
  if (!finishMemoryProfile("completed", true))
    throw std::runtime_error("Could not finish Aperture memory profile");
}

catch (...) {
  (void)finishMemoryProfile("failed", false);
  unwindOwned(0);
  throw;
}

void VM::executeInstruction(const Instruction &instr) {
  if (memory_profile_.enabled())
    memory_profile_.site(instr.source_file,
                         call_stack_.empty() ? "" : call_stack_.back().fn_name,
                         instr.line > 0 ? static_cast<std::uint64_t>(instr.line) : 0);
  if (pending_fault_ && instr.op != Opcode::LABEL && instr.op != Opcode::FAULT_SAVE)
    throw std::runtime_error("pending fault must be saved before ordinary execution");
  const auto is_array_value = [](const Value &value) {
    return std::holds_alternative<ArrayHandle>(value) ||
           std::holds_alternative<ArrayValue>(value);
  };
  const auto array_default_value = [](const HeapBlock &block) -> Value {
    if (is_array_tuple_type(block.element_type))
      throw std::runtime_error("tuple array read is out of bounds or has an invalid handle");
    // Match intrinsic metadata normalization for supported scalar types.
    // Keep the original block metadata intact: nominal aggregate names are
    // case-sensitive even though built-in scalar aliases are normalized here.
    const auto element = canonical_array_scalar(block.element_type);
    if (element == "text")
      return std::string{};
    if (element == "f64")
      return 0.0;
    if (is_array_storage_type(block.element_type))
      return ArrayHandle{};
    return int64_t(0);
  };
  const auto validate_tuple_schema = [&](const auto &self, const std::string &type,
                                          const std::string &operation,
                                          unsigned depth) -> const nil_struct_def * {
    const auto shape = array_tuple_elements(type);
    const auto reject = [&]() {
      throw std::runtime_error(operation + ": requires an exact declared tuple schema for '" + type + "'");
    };
    if (!shape || depth >= 64 || !class_metadata_)
      reject();
    const nil_struct_def *schema = nullptr;
    for (const auto &candidate : class_metadata_->structs) {
      if (canonical_array_type(candidate.name) != canonical_array_type(type)) continue;
      if (schema) reject();
      schema = &candidate;
    }
    if (!schema || schema->fields.size() != shape->size() ||
        std::find(schema->annotations.begin(), schema->annotations.end(), "@nil_tuple") == schema->annotations.end())
      reject();
    for (std::size_t index = 0; index < shape->size(); ++index) {
      if (schema->fields[index].first != std::to_string(index) ||
          canonical_array_type(schema->fields[index].second) != (*shape)[index])
        reject();
      if (is_array_tuple_type((*shape)[index]))
        (void)self(self, (*shape)[index], operation, depth + 1);
    }
    return schema;
  };
  const auto resolve_storage_schema = [&](const auto &definitions, const std::string &type) {
    using Definition = typename std::decay_t<decltype(definitions)>::value_type;
    const Definition *match = nullptr;
    const auto expected = canonical_array_type(type);
    for (const auto &candidate : definitions) {
      if (canonical_array_type(candidate.name) != expected) continue;
      if (match) return static_cast<const Definition *>(nullptr);
      match = &candidate;
    }
    if (match) return match;
    const auto suffix = "::" + expected;
    for (const auto &candidate : definitions) {
      const auto actual = canonical_array_type(candidate.name);
      if (actual.size() <= suffix.size() ||
          actual.compare(actual.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
      if (match) return static_cast<const Definition *>(nullptr);
      match = &candidate;
    }
    return match;
  };
  std::unordered_set<const StructData *> validated_storage_objects;
  const auto validate_tuple_value = [&](const auto &self, const Value &value,
                                         const std::string &type,
                                         const std::string &operation,
                                         unsigned depth) -> void {
    const auto expected = canonical_array_type(type);
    const auto reject = [&]() {
      throw std::runtime_error(operation + ": expected a fully initialized tuple element of exact type '" + type + "'");
    };
    if (depth >= 64 || !class_metadata_) reject();
    if (expected == "text") {
      if (!std::holds_alternative<std::string>(value)) reject();
      return;
    }
    if (expected == "f64") {
      if (!std::holds_alternative<double>(value)) reject();
      return;
    }
    if (expected == "i32" || expected == "i64" || expected == "u8" || expected == "bool") {
      const auto *integer = std::get_if<int64_t>(&value);
      if (!integer || (expected == "i32" && (*integer < INT32_MIN || *integer > INT32_MAX)) ||
          (expected == "u8" && (*integer < 0 || *integer > 255)) ||
          (expected == "bool" && *integer != 0 && *integer != 1)) reject();
      return;
    }
    if (expected.rfind("ptr<", 0) == 0) {
      const auto *pointer = std::get_if<int64_t>(&value);
      if (!pointer) reject();
      if (*pointer == 0) return; // A null raw pointer remains a valid stored value.
      const auto location = findHeapPointer(*pointer, true);
      const auto element = expected.substr(4, expected.size() - 5);
      if (!location || canonical_array_type(location->block->type_name) != element) reject();
      return;
    }
    if (expected.rfind("array<", 0) == 0) {
      const auto *handle = std::get_if<ArrayHandle>(&value);
      if (!handle) reject();
      const auto found = heap_.find(handle->address);
      if (found == heap_.end() || found->second.type_name != "array" ||
          "array<" + canonical_array_type(found->second.element_type) + ">" != expected) reject();
      return;
    }
    if (const auto *enumeration = resolve_storage_schema(class_metadata_->enums, expected)) {
      const auto *item = std::get_if<EnumValue>(&value);
      if (!item) reject();
      const auto tag = canonical_variant_name(item->variant_name);
      const auto separator = tag.rfind("::");
      const auto owner = separator == std::string::npos ? std::string{} : tag.substr(0, separator);
      const auto concrete = item->type_name.empty() ? owner : item->type_name;
      if (concrete.empty() || resolve_storage_schema(class_metadata_->enums, concrete) != enumeration) reject();
      auto schema_owner = canonical_array_type(enumeration->name);
      if (owner.find('<') == std::string::npos) {
        const auto generic = schema_owner.find('<');
        if (generic != std::string::npos) schema_owner.resize(generic);
      }
      const auto suffix = "::" + owner;
      if (!owner.empty() && owner != schema_owner &&
          (schema_owner.size() <= suffix.size() ||
           schema_owner.compare(schema_owner.size() - suffix.size(), suffix.size(), suffix) != 0)) reject();
      const auto variant_name = separator == std::string::npos ? tag : tag.substr(separator + 2);
      const auto variant = std::find_if(enumeration->variants.begin(), enumeration->variants.end(),
          [&](const auto &candidate) { return candidate.name == variant_name; });
      if (variant == enumeration->variants.end()) reject();
      std::vector<std::string> payload_types;
      const auto &payload = variant->associated_type;
      if (!payload.empty()) {
        if (payload.front() == '(' && payload.back() == ')') {
          const auto parts = array_tuple_elements("tuple<" + payload.substr(1, payload.size() - 2) + ">");
          if (!parts) reject();
          payload_types = *parts;
        } else payload_types.push_back(payload);
      }
      if (payload_types.size() != item->payloads.size()) reject();
      for (std::size_t index = 0; index < payload_types.size(); ++index)
        std::visit([&](const auto &stored) {
          using Payload = std::decay_t<decltype(stored)>;
          if constexpr (std::is_same_v<Payload, std::shared_ptr<EnumValue>>) {
            if (!stored) reject();
            self(self, Value(*stored), payload_types[index], operation, depth + 1);
          } else self(self, Value(stored), payload_types[index], operation, depth + 1);
        }, item->payloads[index]);
      return;
    }
    auto nominal = expected;
    if (nominal.rfind("ref<", 0) == 0 && nominal.back() == '>')
      nominal = nominal.substr(4, nominal.size() - 5);
    const auto *schema = is_array_tuple_type(nominal)
        ? validate_tuple_schema(validate_tuple_schema, nominal, operation, depth)
        : resolve_storage_schema(class_metadata_->structs, nominal);
    const auto *object = std::get_if<StructValue>(&value);
    if (!schema || !object || !object->data) reject();
    const auto *actual_schema = resolve_storage_schema(class_metadata_->structs, object->type_name);
    if (find_nil_class(*class_metadata_, nominal)) {
      validateClassValue(value, nominal);
      if (!actual_schema) reject();
      schema = actual_schema;
    } else if (actual_schema != schema) reject();
    // Repeated record references preserve aliasing, and recursive interpreter
    // records must not recurse forever during this storage-boundary check.
    if (!validated_storage_objects.insert(object->data.get()).second) return;
    const auto &data = *object->data;
    if (data.fields.size() + data.array_fields.size() + data.string_fields.size() +
        data.double_fields.size() + data.struct_fields.size() + data.enum_fields.size() != schema->fields.size()) reject();
    for (const auto &[field, field_type] : schema->fields) {
      std::optional<Value> stored;
      const auto take = [&](const auto &fields) {
        if (const auto found = fields.find(field); found != fields.end()) {
          if (stored) reject();
          using Stored = std::decay_t<decltype(found->second)>;
          if constexpr (std::is_same_v<Stored, std::shared_ptr<StructValue>>) {
            if (!found->second) reject();
            stored = Value(*found->second);
          } else stored = Value(found->second);
        }
      };
      take(data.fields); take(data.array_fields); take(data.string_fields);
      take(data.double_fields); take(data.struct_fields); take(data.enum_fields);
      if (!stored) reject();
      self(self, *stored, field_type, operation, depth + 1);
    }
  };
  const auto canonical_pointer_value =
      [&](const HeapBlock &block, const Value &value,
         const std::string &operation) -> Value {
    if (block.type_name == "array" && is_array_tuple_type(block.element_type)) {
      validate_tuple_value(validate_tuple_value, value, block.element_type, operation, 0);
      return value;
    }
    const std::string storage_type = canonical_array_scalar(
        block.type_name == "array" ? block.element_type : block.type_name);
    if (storage_type == "f64") {
      if (!std::holds_alternative<double>(value))
        throw std::runtime_error(operation + ": expected an f64 value");
      return value;
    }
    if (storage_type == "text") {
      if (!std::holds_alternative<std::string>(value))
        throw std::runtime_error(operation + ": expected a text value");
      return value;
    }
    if (storage_type != "bytes" && storage_type != "u8" &&
        storage_type != "bool" && storage_type != "i32" &&
        storage_type != "i64")
      return value;

    const auto *integer = std::get_if<int64_t>(&value);
    if (!integer)
      throw std::runtime_error(operation + ": expected an integer value");
    if (storage_type == "bytes" || storage_type == "u8")
      return static_cast<int64_t>(static_cast<std::uint8_t>(*integer));
    if (storage_type == "bool")
      return int64_t(*integer != 0 ? 1 : 0);
    if (storage_type == "i32") {
      const std::uint64_t low_word =
          static_cast<std::uint64_t>(*integer) & 0xffff'ffffULL;
      return low_word >= 0x8000'0000ULL
                 ? static_cast<int64_t>(low_word) - 0x1'0000'0000LL
                 : static_cast<int64_t>(low_word);
    }
    return *integer;
  };
  const auto validate_global_value = [&](const Value &value) {
    if (instr.args.size() != 2)
      return;
    const auto &type = instr.args[1];
    const auto *integer = std::get_if<int64_t>(&value);
    bool valid = type == "text" ? std::holds_alternative<std::string>(value)
                 : type == "f64" ? std::holds_alternative<double>(value)
                 : integer != nullptr;
    if (valid && integer) {
      valid = (type != "bool" || (*integer == 0 || *integer == 1)) &&
          (type != "u8" || (*integer >= 0 && *integer <= 255)) &&
          (type != "i32" || (*integer >= INT32_MIN && *integer <= INT32_MAX));
    }
    if (!valid)
      throw std::runtime_error("Global '" + instr.args[0] +
                               "' value does not match storage type '" + type + "'");
  };
  switch (instr.op) {
  case Opcode::PUSH_CONST_STR:
    pushStack(constants_.at(instr.args[0]));
    break;
  case Opcode::PUSH_CONST:
    // A hexadecimal integer may contain e/E as a digit, so determine its
    // radix before treating exponent characters as a floating-point marker.
    if (is_floating_literal_spelling(instr.args[0])) {
      double value = 0.0;
      if (const auto *message = parse_floating_literal(instr.args[0], value))
        throw std::runtime_error(std::string(message) + ": " + instr.args[0]);
      pushStack(value);
    } else {
      const std::string &literal = instr.args[0];
      const bool hexadecimal = has_hexadecimal_integer_prefix(literal);
      std::size_t consumed = 0;
      const auto value = std::stoll(literal, &consumed, hexadecimal ? 0 : 10);
      if (consumed != literal.size())
        throw std::runtime_error("Invalid numeric literal: " + literal);
      pushStack(static_cast<int64_t>(value));
    }
    break;
  case Opcode::LOAD_LOCAL:
    // Look up locals in the current call frame. If the local doesn't
    // exist a runtime error will be thrown by `getLocal`.
    pushStack(getLocal(instr.args[0]));
    break;
  case Opcode::STORE_LOCAL:
    // Store into the current frame's local mapping.
    setLocal(instr.args[0], popStack());
    break;
  case Opcode::LOAD_GLOBAL:
    // Load from global storage
    {
      auto it = globals_.find(instr.args[0]);
      if (it != globals_.end()) {
        validate_global_value(it->second);
        pushStack(it->second);
      } else {
        throw std::runtime_error("Global not found: " + instr.args[0]);
      }
    }
    break;
  case Opcode::STORE_GLOBAL: {
    auto value = popStack();
    validate_global_value(value);
    globals_[instr.args[0]] = std::move(value);
    break;
  }
  case Opcode::CLASS_UPCAST: {
    if (!class_metadata_ || instr.args.size() != 1)
      throw std::runtime_error("CLASS_UPCAST requires explicit class metadata");
    auto value = popStack();
    auto *object = std::get_if<StructValue>(&value);
    if (!object || !nil_class_assignable(*class_metadata_, instr.args[0], object->static_class_view) ||
        !nil_class_assignable(*class_metadata_, object->static_class_view, object->type_name))
      throw std::runtime_error("CLASS_UPCAST requires a valid ancestor view of a class handle");
    object->static_class_view = instr.args[0];
    pushStack(value);
    break;
  }
  case Opcode::CALL_METHOD: {
    if (!class_metadata_ || instr.args.size() != 3)
      throw std::runtime_error("CALL_METHOD requires explicit class metadata");
    const auto count = static_cast<std::size_t>(std::stoull(instr.args[2]));
    requireStackValues(count, "CALL_METHOD");
    const auto slot = resolve_nil_class_method(*class_metadata_, instr.args[0], instr.args[1]);
    if (!slot.method || count != slot.method->parameter_types.size() + 1)
      throw std::runtime_error("CALL_METHOD has an invalid virtual slot or signature");
    const bool reference = slot.method->receiver_type.rfind("ref<", 0) == 0;
    auto &receiver = stack_[stack_.size() - count];
    validateClassValue(receiver, reference ? "ref<" + instr.args[0] + ">" : instr.args[0]);
    auto *object = std::get_if<StructValue>(&receiver);
    if (!object) throw std::runtime_error("CALL_METHOD receiver is not a class handle");
    const auto target = slot.method->is_virtual
        ? resolve_nil_class_method(*class_metadata_, object->type_name, instr.args[1]) : slot;
    if (!target.method || target.method->is_abstract || target.method->implementation.empty())
      throw std::runtime_error("CALL_METHOD dynamic class has no concrete implementation");
    const auto function = functions_.find(target.method->implementation);
    if (function == functions_.end() || function->second.param_count != count)
      throw std::runtime_error("CALL_METHOD implementation is unavailable");
    // Internal dispatch changes the callee's view to its declaring owner only;
    // the original alias and immutable dynamic identity remain untouched.
    object->static_class_view = target.method->receiver_type;
    pushFrame(target.method->implementation, pc_, count);
    pc_ = function->second.entry_pc;
    break;
  }
  case Opcode::CALL: {
    std::string fn_name = instr.args[0];
    int arg_count = std::stoi(instr.args[1]);

    // Lexical function values and parsed declarations are authoritative. A
    // legacy builtin spelling (or a substring used by an old adapter) must
    // never replace a real Ny function body or impose its builtin arity.
    std::string actual_fn_name = fn_name;
    if (hasLocal(fn_name)) {
      const Value fn_value = getLocal(fn_name);
      if (std::holds_alternative<std::string>(fn_value))
        actual_fn_name = std::get<std::string>(fn_value);
    }
    const auto function = functions_.find(actual_fn_name);
    if (function != functions_.end()) {
      if (arg_count < 0 ||
          static_cast<size_t>(arg_count) != function->second.param_count) {
        throw std::runtime_error(
            "CALL: function '" + actual_fn_name + "' expects " +
            std::to_string(function->second.param_count) + " arguments, got " +
            std::to_string(arg_count));
      }
      pushFrame(actual_fn_name, pc_, static_cast<size_t>(arg_count));
      pc_ = function->second.entry_pc;
      break;
    }
    if (actual_fn_name != fn_name)
      throw std::runtime_error("CALL: unknown function " + actual_fn_name);

    // Legacy builtins have no parsed-function metadata, so validate their
    // operand counts before any handler indexes or pops the argument list.
    validate_builtin_call_arity(fn_name, arg_count);
    if (arg_count < 0)
      throw std::runtime_error("CALL: argument count must not be negative");
    requireStackValues(static_cast<size_t>(arg_count), "CALL " + fn_name);

    // Debug Trace
    // Debug Trace removed

    // Built-in: print functions
    // core__print / console__writeln: print string + newline
    // println / console__write: print string (no extra newline)
    bool is_println = (fn_name == "println" || fn_name == "core__print" ||
                       fn_name == "console__writeln");
    bool is_write   = (fn_name == "console__write" || fn_name == "console__sys_write");
    if (fn_name == "panic" || fn_name == "ny__lang__core__panic") {
      const Value message_value = popStack();
      const auto *message = std::get_if<std::string>(&message_value);
      if (message == nullptr)
        throw std::runtime_error("panic: message must be text");
      throw std::runtime_error("panic: " + *message);
    }
    if (is_println || is_write) {
      auto &output = outputStream();
      for (int i = 0; i < arg_count; ++i) {
        Value v = popStack();
        if (std::holds_alternative<std::string>(v)) {
          output << std::get<std::string>(v);
          if (is_println)
            output << std::endl;
        } else if (std::holds_alternative<int64_t>(v)) {
          output << std::get<int64_t>(v);
          if (is_println)
            output << std::endl;
        } else if (std::holds_alternative<double>(v)) {
          output << std::get<double>(v);
          if (is_println)
            output << std::endl;
        } else {
          if (std::holds_alternative<StructValue>(v)) {
            output << "[Struct]";
          } else {
            output << "[Value]";
          }
          if (is_println)
            output << std::endl;
        }
      }
      break;
    }

    // Built-in: core stdlib interpreted mode builtins
    // core__to_string(n: i32) -> text
    if (fn_name == "core__to_string") {
      Value v = popStack();
      std::string result;
      if (std::holds_alternative<int64_t>(v))
        result = std::to_string(std::get<int64_t>(v));
      else if (std::holds_alternative<double>(v))
        result = std::to_string(std::get<double>(v));
      else if (std::holds_alternative<std::string>(v))
        result = std::get<std::string>(v);
      else
        result = "[value]";
      pushStack(result);
      break;
    }
    // core__to_string overloads: i64, f64
    // NY-003: use ostringstream for f64 to avoid std::to_string's fixed 6
    // decimal places ("0.850000" → "0.85"). Also handles f64_to_str alias.
    if (fn_name == "core__to_string_i64" || fn_name == "core__to_string_f64" ||
        fn_name == "f64_to_str" || fn_name.find("__f64_to_str") != std::string::npos) {
      Value v = popStack();
      std::string result;
      if (std::holds_alternative<int64_t>(v))
        result = std::to_string(std::get<int64_t>(v));
      else if (std::holds_alternative<double>(v)) {
        std::ostringstream oss;
        oss << std::get<double>(v);
        result = oss.str();
      } else
        result = "[value]";
      pushStack(result);
      break;
    }

    // Built-in: vm__get_address (Unsafe pointer resolution)
    // Note: The function is defined in engine_vulkan, so it is mangled.
    if (fn_name.find("vm__get_address") != std::string::npos) {
      Value v = popStack();
      int64_t result = 0;
      if (std::holds_alternative<int64_t>(v) || std::holds_alternative<ArrayHandle>(v)) {
        const auto *array = std::get_if<ArrayHandle>(&v);
        int64_t handle = array ? array->address : std::get<int64_t>(v);
        if (handle != 0) {
          auto it = heap_.find(handle);
          if (it != heap_.end() && (array || it->second.type_name != "array")) {
            if (it->second.type_name == "string") {
              result = reinterpret_cast<int64_t>(it->second.byte_data.data());
            } else {
              result = reinterpret_cast<int64_t>(it->second.data.data());
            }
          } else {
            // Not found in heap? Maybe it is already a pointer? or invalid
            // For now return handle (consistent with resolve behavior)
            result = handle;
          }
        }
      }
      pushStack(result);
      break;
    }

    // Note: Mimic and other stdlib functions are now handled via INTEROP_CALL
    // opcode The hardcoded handlers have been removed in favor of the generic
    // FFI system

    // Built-in: String method dispatch
    // String__len(self: text) -> i64
    if (fn_name == "String__len") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      pushStack((int64_t)s.size());
      break;
    }
    // String__length(self: text) -> i64  [alias for String__len]
    if (fn_name == "String__length") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      pushStack((int64_t)s.size());
      break;
    }
    // String__substr(self: text, start: i64, len: i64) -> text
    if (fn_name == "String__substr") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      int64_t start = std::holds_alternative<int64_t>(args[1])
                          ? std::get<int64_t>(args[1])
                          : 0;
      int64_t len = std::holds_alternative<int64_t>(args[2])
                        ? std::get<int64_t>(args[2])
                        : 0;
      if (start < 0) start = 0;
      if (start >= (int64_t)s.size()) {
        pushStack(std::string(""));
        break;
      }
      if (len < 0) len = 0;
      if (len > (int64_t)s.size() - start) len = (int64_t)s.size() - start;
      pushStack(s.substr((size_t)start, (size_t)len));
      break;
    }
    // String__contains(self: text, needle: text) -> bool (i64 0/1)
    if (fn_name == "String__contains") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      std::string needle = std::holds_alternative<std::string>(args[1])
                               ? std::get<std::string>(args[1])
                               : "";
      pushStack((int64_t)(s.find(needle) != std::string::npos ? 1 : 0));
      break;
    }
    // String__starts_with(self: text, prefix: text) -> bool (i64 0/1)
    if (fn_name == "String__starts_with") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      std::string prefix = std::holds_alternative<std::string>(args[1])
                               ? std::get<std::string>(args[1])
                               : "";
      bool result = s.size() >= prefix.size() &&
                    s.substr(0, prefix.size()) == prefix;
      pushStack((int64_t)(result ? 1 : 0));
      break;
    }
    // String__ends_with(self: text, suffix: text) -> bool (i64 0/1)
    if (fn_name == "String__ends_with") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0])
                          ? std::get<std::string>(args[0])
                          : "";
      std::string suffix = std::holds_alternative<std::string>(args[1])
                               ? std::get<std::string>(args[1])
                               : "";
      bool result = s.size() >= suffix.size() &&
                    s.substr(s.size() - suffix.size()) == suffix;
      pushStack((int64_t)(result ? 1 : 0));
      break;
    }

    // String__find(self: text, needle: text, from_pos: i64) -> i64
    if (fn_name == "String__find") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      std::string needle = args.size() > 1 && std::holds_alternative<std::string>(args[1]) ? std::get<std::string>(args[1]) : "";
      int64_t from = args.size() > 2 && std::holds_alternative<int64_t>(args[2]) ? std::get<int64_t>(args[2]) : 0;
      if (from < 0) from = 0;
      auto pos = s.find(needle, (size_t)from);
      pushStack(pos == std::string::npos ? (int64_t)-1 : (int64_t)pos);
      break;
    }
    // String__substring(self: text, start: i64, len: i64) -> text
    if (fn_name == "String__substring") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      int64_t start = args.size() > 1 && std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
      int64_t len   = args.size() > 2 && std::holds_alternative<int64_t>(args[2]) ? std::get<int64_t>(args[2]) : (int64_t)s.size();
      if (start < 0) start = 0;
      if (start >= (int64_t)s.size()) { pushStack(std::string("")); break; }
      if (len < 0) len = 0;
      if (len > (int64_t)s.size() - start) len = (int64_t)s.size() - start;
      pushStack(s.substr((size_t)start, (size_t)len));
      break;
    }
    // String__to_uppercase / String__to_lowercase
    if (fn_name == "String__to_uppercase" || fn_name == "String__to_upper") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      std::transform(s.begin(), s.end(), s.begin(), ::toupper);
      pushStack(s);
      break;
    }
    if (fn_name == "String__to_lowercase" || fn_name == "String__to_lower") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      std::transform(s.begin(), s.end(), s.begin(), ::tolower);
      pushStack(s);
      break;
    }
    // String__trim / String__trim_start / String__trim_end
    if (fn_name == "String__trim") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      size_t st = s.find_first_not_of(" \t\r\n");
      size_t en = s.find_last_not_of(" \t\r\n");
      pushStack(st == std::string::npos ? std::string("") : s.substr(st, en - st + 1));
      break;
    }
    // String__replace(self: text, from: text, to: text) -> text
    if (fn_name == "String__replace") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      std::string s = std::holds_alternative<std::string>(args[0]) ? std::get<std::string>(args[0]) : "";
      std::string from = args.size() > 1 && std::holds_alternative<std::string>(args[1]) ? std::get<std::string>(args[1]) : "";
      std::string to = args.size() > 2 && std::holds_alternative<std::string>(args[2]) ? std::get<std::string>(args[2]) : "";
      if (!from.empty()) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
          s.replace(pos, from.size(), to);
          pos += to.size();
        }
      }
      pushStack(s);
      break;
    }
    // array__push(self: array<T> handle, elem: T) -> void
    // Appends an element to a heap-allocated array (NEW_ARRAY_LITERAL).
    if (fn_name == "array__push") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (args.size() >= 2 && std::holds_alternative<ArrayHandle>(args[0])) {
        int64_t handle = std::get<ArrayHandle>(args[0]).address;
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "array") {
          it->second.data.push_back(
              canonical_pointer_value(it->second, args[1], "array__push"));
          it->second.element_count++;
          // Match the GC statistics' logical heap slots, independently of
          // Aperture's typed payload bytes. Charge only a successful append.
          account_heap_allocation(total_allocated_bytes_,
                                  heap_element_stride(it->second.type_name));
          if (memory_profile_.enabled())
            memory_profile_.resize(
                reinterpret_cast<const void *>(static_cast<std::uintptr_t>(handle)),
                profileHeapBlockBytes(it->second));
        }
      }
      // A void call has no operand-stack result.
      break;
    }
    // array__get(self: array<T> handle, index: i64) -> T
    if (fn_name == "array__get") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (args.size() >= 2 && std::holds_alternative<ArrayHandle>(args[0])) {
        int64_t handle = std::get<ArrayHandle>(args[0]).address;
        int64_t idx = std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "array" &&
            idx >= 0 && idx < (int64_t)it->second.data.size()) {
          pushStack(it->second.data[(size_t)idx]);
        } else {
          pushStack(it != heap_.end() && it->second.type_name == "array"
                        ? array_default_value(it->second) : Value(int64_t(0)));
        }
      } else {
        pushStack((int64_t)0);
      }
      break;
    }
    // array__len(self: array<T> handle) -> i64
    if (fn_name == "array__len") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (!args.empty() && std::holds_alternative<ArrayHandle>(args[0])) {
        int64_t handle = std::get<ArrayHandle>(args[0]).address;
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "array") {
          pushStack((int64_t)it->second.element_count);
        } else {
          pushStack((int64_t)0);
        }
      } else {
        pushStack((int64_t)0);
      }
      break;
    }
    // array__length(self: array<T> handle) -> i64  [alias for array__len]
    if (fn_name == "array__length") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (!args.empty() && std::holds_alternative<ArrayHandle>(args[0])) {
        int64_t handle = std::get<ArrayHandle>(args[0]).address;
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "array") {
          pushStack((int64_t)it->second.element_count);
        } else {
          pushStack((int64_t)0);
        }
      } else {
        pushStack((int64_t)0);
      }
      break;
    }
    // array__set(self: array<T> handle, index: i64, value: T) -> void
    if (fn_name == "array__set") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (args.size() >= 3 && std::holds_alternative<ArrayHandle>(args[0])) {
        int64_t handle = std::get<ArrayHandle>(args[0]).address;
        int64_t idx = std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "array" &&
            idx >= 0 && idx < (int64_t)it->second.data.size()) {
          it->second.data[(size_t)idx] =
              canonical_pointer_value(it->second, args[2], "array__set");
        }
      }
      // A void call has no operand-stack result.
      break;
    }

    // __ritual_len(collection) -> i64: get length of array/string for for-in loops
    if (fn_name == "__ritual_len") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (!args.empty()) {
        Value &v = args[0];
        if (std::holds_alternative<ArrayValue>(v)) {
          pushStack((int64_t)std::get<ArrayValue>(v).elements.size());
        } else if (std::holds_alternative<std::string>(v)) {
          pushStack((int64_t)std::get<std::string>(v).size());
        } else if (std::holds_alternative<ArrayHandle>(v)) {
          int64_t handle = std::get<ArrayHandle>(v).address;
          auto it = heap_.find(handle);
          if (it != heap_.end() && it->second.type_name == "array") {
            pushStack((int64_t)it->second.element_count);
          } else {
            pushStack((int64_t)0);
          }
        } else {
          pushStack((int64_t)0);
        }
      } else {
        pushStack((int64_t)0);
      }
      break;
    }

    // NY-020: __ritual_iter / __ritual_next / __ritual_is_done / __ritual_value
    // Iterator protocol for for-in loops.
    // State is a HeapBlock {type="__iter", data=[collection, int64_t(idx)]}.
    // __ritual_next mutates the heap block in place and returns the same handle
    // so that the loop body can reload tmp_iter each iteration and still see
    // the updated index.
    if (fn_name == "__ritual_iter") {
      Value coll = popStack();
      // Allocate iterator state on the heap
      int64_t handle = ++next_ptr_;
      HeapBlock b;
      b.type_name = "__iter";
      b.data.resize(2);
      b.data[0] = coll;       // the collection (ArrayHandle or ArrayValue)
      b.data[1] = (int64_t)-1; // index starts at -1; __ritual_next increments to 0 first
      storeHeapBlock(handle, std::move(b));
      pushStack(handle);
      break;
    }
    if (fn_name == "__ritual_next") {
      Value h = popStack();
      if (std::holds_alternative<int64_t>(h)) {
        int64_t handle = std::get<int64_t>(h);
        auto it = heap_.find(handle);
        if (it != heap_.end() && it->second.type_name == "__iter") {
          // Advance index
          int64_t idx = std::get<int64_t>(it->second.data[1]);
          it->second.data[1] = idx + 1;
        }
        pushStack(handle); // return same handle
      } else {
        pushStack(h); // pass through if not a handle
      }
      break;
    }
    if (fn_name == "__ritual_is_done") {
      Value h = popStack();
      if (!std::holds_alternative<int64_t>(h)) { pushStack((int64_t)1); break; }
      int64_t handle = std::get<int64_t>(h);
      auto it = heap_.find(handle);
      if (it == heap_.end() || it->second.type_name != "__iter") {
        pushStack((int64_t)1); break;
      }
      int64_t idx = std::get<int64_t>(it->second.data[1]);
      const Value &coll = it->second.data[0];
      int64_t len = 0;
      if (std::holds_alternative<ArrayHandle>(coll)) {
        int64_t ch = std::get<ArrayHandle>(coll).address;
        auto cit = heap_.find(ch);
        if (cit != heap_.end()) {
          len = (int64_t)cit->second.data.size();
        }
      } else if (std::holds_alternative<ArrayValue>(coll)) {
        len = (int64_t)std::get<ArrayValue>(coll).elements.size();
      }
      pushStack((int64_t)(idx >= len ? 1 : 0)); // 1 = done
      break;
    }
    if (fn_name == "__ritual_value") {
      Value h = popStack();
      if (!std::holds_alternative<int64_t>(h)) { pushStack((int64_t)0); break; }
      int64_t handle = std::get<int64_t>(h);
      auto it = heap_.find(handle);
      if (it == heap_.end() || it->second.type_name != "__iter") {
        pushStack((int64_t)0); break;
      }
      int64_t idx = std::get<int64_t>(it->second.data[1]);
      const Value &coll = it->second.data[0];
      if (std::holds_alternative<ArrayHandle>(coll)) {
        int64_t ch = std::get<ArrayHandle>(coll).address;
        auto cit = heap_.find(ch);
        if (cit != heap_.end() && idx >= 0 && idx < (int64_t)cit->second.data.size()) {
          pushStack(cit->second.data[idx]);
        } else {
          pushStack((int64_t)0);
        }
      } else if (std::holds_alternative<ArrayValue>(coll)) {
        const auto &arr = std::get<ArrayValue>(coll).elements;
        if (idx >= 0 && idx < (int64_t)arr.size())
          pushStack(array_elem_to_value(arr[idx]));
        else
          pushStack((int64_t)0);
      } else {
        pushStack((int64_t)0);
      }
      break;
    }
    // __ritual_index(container, idx) -> element: direct index into array/heap for for-in loops
    if (fn_name == "__ritual_index") {
      std::vector<Value> args;
      for (int i = 0; i < arg_count; ++i) args.push_back(popStack());
      std::reverse(args.begin(), args.end());
      if (args.size() < 2) { pushStack((int64_t)0); break; }
      Value &coll = args[0];
      int64_t idx = std::holds_alternative<int64_t>(args[1]) ? std::get<int64_t>(args[1]) : 0;
      if (std::holds_alternative<ArrayValue>(coll)) {
        const auto &arr = std::get<ArrayValue>(coll).elements;
        if (idx >= 0 && idx < (int64_t)arr.size())
          pushStack(array_elem_to_value(arr[idx]));
        else
          pushStack((int64_t)0);
      } else if (std::holds_alternative<ArrayHandle>(coll)) {
        int64_t handle = std::get<ArrayHandle>(coll).address;
        auto it = heap_.find(handle);
        if (it != heap_.end() && idx >= 0 && idx < (int64_t)it->second.data.size())
          pushStack(it->second.data[idx]);
        else
          pushStack((int64_t)0);
      } else if (std::holds_alternative<std::string>(coll)) {
        // String indexing — return single char as string
        const std::string &s = std::get<std::string>(coll);
        if (idx >= 0 && idx < (int64_t)s.size())
          pushStack(std::string(1, s[idx]));
        else
          pushStack(std::string(""));
      } else {
        pushStack((int64_t)0);
      }
      break;
    }

    // __ritual_tuple_get: extract element N from a tuple/array value
    if (fn_name == "__ritual_tuple_get") {
      Value idx_v = popStack();
      Value tup = popStack();
      int64_t idx = std::holds_alternative<int64_t>(idx_v) ? std::get<int64_t>(idx_v) : 0;
      if (std::holds_alternative<ArrayHandle>(tup)) {
        int64_t ch = std::get<ArrayHandle>(tup).address;
        auto cit = heap_.find(ch);
        if (cit != heap_.end() && idx >= 0 && idx < (int64_t)cit->second.data.size())
          pushStack(cit->second.data[idx]);
        else
          pushStack((int64_t)0);
      } else if (std::holds_alternative<ArrayValue>(tup)) {
        const auto &arr = std::get<ArrayValue>(tup).elements;
        if (idx >= 0 && idx < (int64_t)arr.size())
          pushStack(array_elem_to_value(arr[idx]));
        else
          pushStack((int64_t)0);
      } else {
        pushStack((int64_t)0);
      }
      break;
    }

    std::cerr << "NIL interpreter error: CALL: unknown function '" << actual_fn_name << "'"
              << std::endl;
    std::cerr << "Available functions (" << functions_.size()
              << "):" << std::endl;
    int count = 0;
    for (const auto &kv : functions_) {
      std::cerr << "  " << kv.first << std::endl;
      if (++count > 2000) {
        std::cerr << "  ... (truncated)" << std::endl;
        break;
      }
    }
    throw std::runtime_error("CALL: unknown function " + actual_fn_name);
  } break;
  case Opcode::RET: {
    if (pending_fault_ ||
        (!fault_regions_.empty() &&
         fault_regions_.back().frame_depth == call_stack_.size()) ||
        std::any_of(saved_faults_.begin(), saved_faults_.end(), [&](const auto &saved) {
          return saved.frame_depth == call_stack_.size();
        }))
      throw std::runtime_error("RET bypasses live fault state");
    if (!instr.args.empty() &&
        instr.args.front() == nil_nonvoid_fallthrough_operand) {
      throw std::runtime_error(
          "non-void function reached compiler-generated fallthrough");
    }
    if (!instr.args.empty())
      throw std::runtime_error("invalid RET operand");
    if (!call_stack_.empty() && !call_stack_.back().return_type.empty()) {
      const auto &frame = call_stack_.back();
      const size_t expected = frame.return_type == "void" ? 0 : 1;
      const size_t actual = availableStackValues();
      if (actual != expected)
        throw std::runtime_error(
            "invalid RET stack in function '" + frame.fn_name +
            "': expected " + std::to_string(expected) + " value(s), found " +
            std::to_string(actual));
      if (expected == 1) validateClassValue(stack_.back(), frame.return_type);
    }
    // If we're in the root frame or call stack is empty/has only root, exit
    if (call_stack_.size() <= 1) {
      unwindOwned(call_stack_.empty() ? 0 : call_stack_.back().owned_depth);
      if (!call_stack_.empty())
        call_stack_.back().returned = true;
      pc_ = program_.size(); // exit
    } else {
      // Pop call frame and return to caller
      size_t ret_pc = call_stack_.back().return_pc;
      popFrame();
      pc_ = ret_pc; // Resume after the CALL instruction
    }
  } break;
  case Opcode::NOP:
    break;
  case Opcode::OWN_ENTER:
    if (!instr.args.empty() || call_stack_.empty())
      throw std::runtime_error("OWN_ENTER: invalid affine scope");
    owned_scopes_.push_back({call_stack_.size(), {}});
    break;
  case Opcode::OWN_LEAVE:
    if (!instr.args.empty() || owned_scopes_.empty() ||
        owned_scopes_.back().frame_depth != call_stack_.size())
      throw std::runtime_error("OWN_LEAVE: no current affine scope");
    unwindOwned(owned_scopes_.size() - 1);
    break;
  case Opcode::OWN_BIND: {
    if (instr.args.size() != 1 || owned_scopes_.empty() ||
        owned_scopes_.back().frame_depth != call_stack_.size() || !hasLocal(instr.args[0]))
      throw std::runtime_error("OWN_BIND: missing affine scope or local");
    const auto value = getLocal(instr.args[0]);
    const auto *pointer = std::get_if<int64_t>(&value);
    if (!pointer) throw std::runtime_error("OWN_BIND: expected allocation pointer");
    const auto allocation = heap_.find(*pointer);
    if (allocation == heap_.end() ||
        (allocation->second.type_name != "i32" && allocation->second.type_name != "i64" && allocation->second.type_name != "u8"))
      throw std::runtime_error("OWN_BIND: expected exact scalar buffer allocation");
    for (const auto &scope : owned_scopes_)
      for (const auto &slot : scope.slots)
        if (slot.second == *pointer ||
            (scope.frame_depth == call_stack_.size() && slot.first == instr.args[0]))
          throw std::runtime_error("OWN_BIND: allocation or slot already owned");
    owned_scopes_.back().slots.emplace(instr.args[0], *pointer);
    break;
  }
  case Opcode::OWN_MOVE: {
    if (instr.args.size() != 1 || !hasLocal(instr.args[0]))
      throw std::runtime_error("OWN_MOVE: expected owned local");
    bool moved = false;
    for (auto scope = owned_scopes_.rbegin(); scope != owned_scopes_.rend(); ++scope) {
      if (scope->frame_depth != call_stack_.size()) break;
      const auto slot = scope->slots.find(instr.args[0]);
      if (slot == scope->slots.end()) continue;
      pushStack(slot->second);
      scope->slots.erase(slot);
      setLocal(instr.args[0], int64_t(0));
      moved = true;
      break;
    }
    if (!moved) throw std::runtime_error("OWN_MOVE: local has already moved or was not owned");
    break;
  }
  case Opcode::FAULT_ENTER: {
    if (instr.args.size() != 2 || instr.args[0].empty() || pending_fault_ ||
        call_stack_.empty())
      throw std::runtime_error("invalid FAULT_ENTER state or operands");
    const auto label = labels_.find(instr.args[1]);
    const auto &frame = call_stack_.back();
    if (label == labels_.end() || label->second <= frame.entry_pc ||
        label->second >= frame.end_pc)
      throw std::runtime_error("FAULT_ENTER dispatch must be inside its function");
    for (const auto &region : fault_regions_)
      if (region.frame_depth == call_stack_.size() && region.id == instr.args[0])
        throw std::runtime_error("duplicate live fault region '" + instr.args[0] + "'");
    FaultRegion region;
    region.frame_depth = call_stack_.size();
    region.stack_height = stack_.size();
    region.id = instr.args[0];
    region.dispatch_pc = label->second;
    region.owned_depth = owned_scopes_.size();
    for (const auto &saved : saved_faults_)
      if (saved.frame_depth == region.frame_depth)
        region.saved_slots.push_back(saved.slot);
    fault_regions_.push_back(std::move(region));
    break;
  }
  case Opcode::FAULT_LEAVE:
    if (instr.args.size() != 1 || pending_fault_ || fault_regions_.empty() ||
        fault_regions_.back().frame_depth != call_stack_.size() ||
        fault_regions_.back().id != instr.args[0])
      throw std::runtime_error("FAULT_LEAVE does not match the innermost region");
    fault_regions_.pop_back();
    break;
  case Opcode::FAULT_RAISE: {
    if (instr.args.size() != 1 || instr.args[0].empty() || pending_fault_)
      throw std::runtime_error("invalid FAULT_RAISE state or operands");
    const Value value = popStack();
    const auto *payload = std::get_if<StructValue>(&value);
    if (!payload || !payload->data || payload->type_name != instr.args[0])
      throw std::runtime_error("FAULT_RAISE requires the exact concrete struct payload '" +
                               instr.args[0] + "'");
    pending_fault_ = FaultValue{instr.args[0], *payload, instr.line};
    propagateFault();
    break;
  }
  case Opcode::FAULT_SAVE:
    if (instr.args.size() != 1 || instr.args[0].empty() || !pending_fault_)
      throw std::runtime_error("FAULT_SAVE requires a pending fault and one slot");
    if (std::any_of(saved_faults_.begin(), saved_faults_.end(), [&](const auto &saved) {
          return saved.frame_depth == call_stack_.size() && saved.slot == instr.args[0];
        }))
      throw std::runtime_error("FAULT_SAVE would overwrite live saved fault");
    saved_faults_.push_back({call_stack_.size(), instr.args[0], *pending_fault_});
    pending_fault_.reset();
    break;
  case Opcode::FAULT_MATCH:
  case Opcode::FAULT_BIND: {
    if (instr.args.size() != 2 || instr.args[1].empty() || pending_fault_)
      throw std::runtime_error("invalid fault match/bind state or operands");
    const auto &saved = savedFault(instr.args[0]);
    const bool matched = saved.fault.type_name == instr.args[1];
    if (instr.op == Opcode::FAULT_MATCH)
      pushStack(static_cast<std::int64_t>(matched));
    else if (matched)
      pushStack(saved.fault.payload);
    else
      throw std::runtime_error("FAULT_BIND payload does not match exact type '" + instr.args[1] + "'");
    break;
  }
  case Opcode::FAULT_DROP:
  case Opcode::FAULT_RESUME:
  case Opcode::FAULT_RETHROW: {
    if (instr.args.size() != 1 || pending_fault_)
      throw std::runtime_error("invalid saved-fault operation state or operands");
    auto &saved = savedFault(instr.args[0]);
    if (instr.op != Opcode::FAULT_DROP)
      pending_fault_ = saved.fault;
    if (instr.op != Opcode::FAULT_RETHROW)
      saved_faults_.erase(std::remove_if(saved_faults_.begin(), saved_faults_.end(),
                         [&](const auto &item) {
                           return item.frame_depth == call_stack_.size() && item.slot == instr.args[0];
                         }), saved_faults_.end());
    if (instr.op != Opcode::FAULT_DROP)
      propagateFault();
    break;
  }
  case Opcode::INLINE_ASM:
    throw std::runtime_error(
        "INLINE_ASM requires a matching LLVM native route; nilinterpreter "
        "does not execute machine assembly");
  case Opcode::POP:
    (void)popStack();
    break;
  case Opcode::INTEROP_CALL: {
    // INTEROP_CALL lib_name fn_name arg_count
    // Dynamically dispatch to native function
    std::string lib_name = instr.args[0];
    std::string fn_name = instr.args[1];
    int arg_count = std::stoi(instr.args[2]);
    if (arg_count < 0)
      throw std::runtime_error("INTEROP_CALL: argument count must not be negative");
    requireStackValues(static_cast<size_t>(arg_count),
                       "INTEROP_CALL " + lib_name + "::" + fn_name);
    const size_t result_base = stack_.size() - static_cast<size_t>(arg_count);
    const bool returns_void = instr.args.size() > 3 && instr.args[3] == "void";

    // Existing intrinsic handlers use both return and break. Funnel their
    // successful exits through one boundary so host status values cannot leak
    // from a declared-void call, including calls nested in larger expressions.
    const auto dispatch_interop = [&]() {
    do {

    // Check for hot-reloadable DLL changes (throttled to every 2 seconds)
    ffi::maybe_hot_reload_all();

    // Prepare std::filesystem alias
    namespace fs = std::filesystem;

    // Collect arguments
    std::vector<Value> args;
    for (int i = 0; i < arg_count; ++i) {
      args.push_back(popStack());
    }
    std::reverse(args.begin(), args.end());

    // Helper to get string from Value
    auto get_str = [](const Value &v) -> std::string {
      if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
      return "";
    };

    // ---- HAG: GPU compute through the dynamically loaded C ABI ----------
    //
    // The VM owns every buffer. A Matrix `pointer to byte` is a VM-virtual
    // address into heap_, not a host pointer, so each region is resolved and
    // bounds-checked here before its host address is lent to HAG for the
    // duration of one call. Nothing HAG sees outlives the call, and HAG never
    // allocates anything the VM must free.
    if (lib_name == "intrinsic" && fn_name.rfind("hag.", 0) == 0) {
      const auto integer_at = [&](std::size_t index) -> std::int64_t {
        if (index >= args.size())
          return 0;
        if (const auto *value = std::get_if<std::int64_t>(&args[index]))
          return *value;
        if (const auto *value = std::get_if<double>(&args[index]))
          return static_cast<std::int64_t>(*value);
        return 0;
      };

      // A buffer argument arrives either as a raw pointer -- an integer
      // virtual address from 'allocate' -- or as an array handle, which is
      // what ny.lang.io.file.read_bytes returns. Accepting both is what lets
      // a shader be loaded from a file and handed straight to the GPU
      // instead of being rebuilt byte by byte in source.
      const auto pointer_at = [&](std::size_t index) -> std::int64_t {
        if (index >= args.size())
          return 0;
        if (const auto *handle = std::get_if<ArrayHandle>(&args[index]))
          return handle->address;
        if (const auto *value = std::get_if<std::int64_t>(&args[index]))
          return *value;
        return 0;
      };

      // Resolve a VM byte-buffer pointer to a host region, refusing anything
      // that is not a live allocation or does not wholly contain the span.
      struct host_region {
        unsigned char *data = nullptr;
        int status = matrix_hag::hag_ok;
      };
      const auto resolve = [&](std::int64_t pointer, std::uint64_t offset,
                               std::uint64_t length) -> host_region {
        auto found = heap_.find(pointer);
        if (found == heap_.end())
          return {nullptr, matrix_hag::bridge_bad_pointer};
        if (found->second.byte_data.empty())
          return {nullptr, found->second.data.empty()
                               ? matrix_hag::bridge_bad_pointer
                               : matrix_hag::bridge_not_byte_backed};
        auto &bytes = found->second.byte_data;
        const auto capacity = static_cast<std::uint64_t>(bytes.size());
        if (offset > capacity || length > capacity - offset)
          return {nullptr, matrix_hag::bridge_out_of_bounds};
        return {reinterpret_cast<unsigned char *>(bytes.data()) + offset,
                matrix_hag::hag_ok};
      };

      if (fn_name == "hag.available") {
        stack_.push_back(static_cast<std::int64_t>(matrix_hag::available() ? 1 : 0));
        return;
      }
      if (fn_name == "hag.status.text") {
        stack_.push_back(matrix_hag::status_text(static_cast<int>(integer_at(0))));
        return;
      }
      if (fn_name == "hag.min.vgprs") {
        stack_.push_back(static_cast<std::int64_t>(matrix_hag::min_kernel_vgprs()));
        return;
      }
      if (fn_name == "hag.max.vgprs") {
        stack_.push_back(static_cast<std::int64_t>(matrix_hag::max_kernel_vgprs()));
        return;
      }
      if (fn_name == "hag.max.groups") {
        stack_.push_back(static_cast<std::int64_t>(matrix_hag::max_dispatch_groups()));
        return;
      }
      if (fn_name == "hag.open") {
        // (device, program_ptr, program_bytes, slot0, slot1, slot2, vgprs,
        //  max_groups, budget)
        const auto program = resolve(pointer_at(1), 0,
                                     static_cast<std::uint64_t>(integer_at(2)));
        if (program.status != matrix_hag::hag_ok) {
          stack_.push_back(static_cast<std::int64_t>(program.status));
          return;
        }
        std::int64_t handle = 0;
        const int status = matrix_hag::queue_open(
            get_str(args.empty() ? Value{std::string()} : args[0]), program.data,
            static_cast<std::uint32_t>(integer_at(2)),
            static_cast<std::uint32_t>(integer_at(3)),
            static_cast<std::uint32_t>(integer_at(4)),
            static_cast<std::uint32_t>(integer_at(5)),
            static_cast<std::uint32_t>(integer_at(6)),
            static_cast<std::uint32_t>(integer_at(7)),
            static_cast<std::uint64_t>(integer_at(8)), &handle);
        // A handle is positive; a failure is the negative status, so one
        // value carries both without a second out-parameter.
        stack_.push_back(status == matrix_hag::hag_ok
                             ? handle
                             : static_cast<std::int64_t>(status));
        return;
      }
      if (fn_name == "hag.write" || fn_name == "hag.read") {
        // (handle, slot, slot_offset, buffer_ptr, buffer_offset, length)
        const auto length = static_cast<std::uint64_t>(integer_at(5));
        const auto region = resolve(pointer_at(3),
                                    static_cast<std::uint64_t>(integer_at(4)),
                                    length);
        if (region.status != matrix_hag::hag_ok) {
          stack_.push_back(static_cast<std::int64_t>(region.status));
          return;
        }
        const int status =
            fn_name == "hag.write"
                ? matrix_hag::queue_write(integer_at(0),
                                          static_cast<std::uint32_t>(integer_at(1)),
                                          static_cast<std::uint64_t>(integer_at(2)),
                                          region.data, length)
                : matrix_hag::queue_read(integer_at(0),
                                         static_cast<std::uint32_t>(integer_at(1)),
                                         static_cast<std::uint64_t>(integer_at(2)),
                                         region.data, length);
        stack_.push_back(static_cast<std::int64_t>(status));
        return;
      }
      if (fn_name == "hag.dispatch") {
        // (handle, program, off0..2, len0..2, scalar0, scalar1, groups)
        const std::uint64_t offsets[3] = {
            static_cast<std::uint64_t>(integer_at(2)),
            static_cast<std::uint64_t>(integer_at(3)),
            static_cast<std::uint64_t>(integer_at(4))};
        const std::uint32_t lengths[3] = {
            static_cast<std::uint32_t>(integer_at(5)),
            static_cast<std::uint32_t>(integer_at(6)),
            static_cast<std::uint32_t>(integer_at(7))};
        const std::uint32_t scalars[2] = {
            static_cast<std::uint32_t>(integer_at(8)),
            static_cast<std::uint32_t>(integer_at(9))};
        const int status = matrix_hag::queue_dispatch(
            integer_at(0), static_cast<std::uint32_t>(integer_at(1)), offsets,
            lengths, scalars, static_cast<std::uint32_t>(integer_at(10)));
        stack_.push_back(static_cast<std::int64_t>(status));
        return;
      }
      if (fn_name == "hag.allocation.bytes") {
        stack_.push_back(static_cast<std::int64_t>(
            matrix_hag::queue_allocation_bytes(integer_at(0))));
        return;
      }
      if (fn_name == "hag.poisoned") {
        stack_.push_back(
            static_cast<std::int64_t>(matrix_hag::queue_is_poisoned(integer_at(0))));
        return;
      }
      if (fn_name == "hag.close") {
        stack_.push_back(
            static_cast<std::int64_t>(matrix_hag::queue_close(integer_at(0))));
        return;
      }
      throw std::runtime_error("INTEROP_CALL: unknown HAG operation '" + fn_name +
                               "'");
    }

    if (lib_name == "intrinsic" && fn_name.rfind("array.", 0) == 0) {
      auto metadata = instr.args;
      // Legacy NIL encoded array.length as a word result without an element
      // operand. Recover that proof only from a real typed heap array.
      if (metadata.size() == 4 && metadata[1] == "array.length" &&
          metadata[2] == "1" && metadata[3] == "i64" && args.size() == 1) {
        const auto *handle = std::get_if<ArrayHandle>(&args[0]);
        const auto found = handle ? heap_.find(handle->address) : heap_.end();
        if (found != heap_.end() && found->second.type_name == "array") {
          metadata[3] = "i32";
          metadata.push_back(found->second.element_type);
        }
      }
      const auto contract = parse_array_intrinsic(metadata);
      if (is_array_tuple_type(contract.element))
        (void)validate_tuple_schema(validate_tuple_schema, contract.element, fn_name, 0);
      if (args.size() != contract.argument_count)
        throw std::runtime_error("array intrinsic argument count mismatch");
      const auto integer = [&](std::size_t index) {
        const auto *value = std::get_if<std::int64_t>(&args.at(index));
        if (!value)
          throw std::runtime_error("array intrinsic requires an integer size/index");
        return *value;
      };
      if (contract.operation == array_intrinsic_operation::create) {
        const auto size = integer(0);
        if (size < 0 || size > std::numeric_limits<std::int32_t>::max())
          throw std::runtime_error("array size must be between 0 and 2147483647");
        const auto count = static_cast<std::uint64_t>(size);
        const auto extent = std::max<std::uint64_t>(count, 1) * 8U;
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value) ||
            next_ptr_ < 0 || extent > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max() - next_ptr_))
          throw std::runtime_error("array requested allocation is too large");
        HeapBlock block;
        block.type_name = "array";
        block.element_type = contract.element;
        block.element_count = static_cast<std::size_t>(count);
        const Value initial = canonical_pointer_value(block, args[1], fn_name);
        block.data.assign(static_cast<std::size_t>(count), initial);
        const auto handle = next_ptr_;
        next_ptr_ += static_cast<std::int64_t>(extent);
        storeHeapBlock(handle, std::move(block));
        pushStack(ArrayHandle{handle});
        // The result now roots the initializer's nested array handles. The
        // popped intrinsic arguments alone are not roots during collection.
        if (memory_mode_ == MemoryMode::SAFE_GC && gc_critical_depth_ == 0 &&
            ++alloc_count_ >= gc_threshold_)
          gcCollect();
        return;
      }
      const auto *handle = std::get_if<ArrayHandle>(&args[0]);
      const auto found = handle ? heap_.find(handle->address) : heap_.end();
      if (found == heap_.end() || found->second.type_name != "array" ||
          canonical_array_type(found->second.element_type) != contract.element)
        throw std::runtime_error("array intrinsic requires a matching typed array handle");
      auto &block = found->second;
      if (contract.operation == array_intrinsic_operation::length) {
        if (block.data.size() > static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max()))
          throw std::runtime_error("array length exceeds the i32 range");
        pushStack(static_cast<std::int64_t>(block.data.size()));
        return;
      }
      const auto index = integer(1);
      if (index < std::numeric_limits<std::int32_t>::min() ||
          index > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("array index is outside the i32 range");
      const bool in_bounds = index >= 0 &&
          static_cast<std::uint64_t>(index) < block.data.size();
      if (contract.operation == array_intrinsic_operation::get) {
        pushStack(in_bounds ? block.data[static_cast<std::size_t>(index)]
                            : array_default_value(block));
      } else {
        const auto value = canonical_pointer_value(block, args[2], fn_name);
        if (in_bounds)
          block.data[static_cast<std::size_t>(index)] = value;
      }
      return;
    }

    // --- Core conversion intrinsics ---
    if (lib_name == "intrinsic" && fn_name.rfind("convert.", 0) == 0) {
      auto require_count = [&](std::size_t expected) {
        if (args.size() != expected)
          throw std::runtime_error(fn_name + ": expected " +
                                   std::to_string(expected) + " argument(s)");
      };
      auto require_text = [&](std::size_t index) -> const std::string & {
        if (index >= args.size() ||
            !std::holds_alternative<std::string>(args[index]))
          throw std::runtime_error(fn_name + ": expected a text argument");
        return std::get<std::string>(args[index]);
      };
      auto require_integer = [&](std::size_t index) -> std::int64_t {
        if (index >= args.size() ||
            !std::holds_alternative<std::int64_t>(args[index]))
          throw std::runtime_error(fn_name + ": expected an integer argument");
        return std::get<std::int64_t>(args[index]);
      };
      auto require_number = [&](std::size_t index) -> double {
        if (index < args.size()) {
          if (const auto *floating = std::get_if<double>(&args[index]))
            return *floating;
          if (const auto *integer = std::get_if<std::int64_t>(&args[index]))
            return static_cast<double>(*integer);
        }
        throw std::runtime_error(fn_name + ": expected a numeric argument");
      };
      auto push_ok = [&](const EnumPayload &payload) {
        EnumValue result;
        result.variant_name = "Result::Ok";
        result.payloads.push_back(payload);
        pushStack(result);
      };
      auto push_error = [&](const std::string &message) {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(message);
        pushStack(result);
      };

      if (fn_name == "convert.to_i32" ||
          fn_name == "convert.try_to_i32") {
        require_count(1);
        IntegerParseResult parsed =
            parse_signed_integer(require_text(0), 10, false);
        if (parsed.ok &&
            (parsed.value < std::numeric_limits<std::int32_t>::min() ||
             parsed.value > std::numeric_limits<std::int32_t>::max())) {
          parsed.ok = false;
          parsed.error = "value is outside the i32 range";
        }
        if (fn_name == "convert.try_to_i32") {
          if (parsed.ok)
            push_ok(parsed.value);
          else
            push_error(std::string("convert.try_to_i32: ") + parsed.error);
        } else if (parsed.ok) {
          pushStack(parsed.value);
        } else {
          throw std::runtime_error(std::string("convert.to_i32: ") + parsed.error);
        }
        return;
      }
      if (fn_name == "convert.to_i64" ||
          fn_name == "convert.try_to_i64") {
        require_count(1);
        const IntegerParseResult parsed =
            parse_signed_integer(require_text(0), 10, false);
        if (fn_name == "convert.try_to_i64") {
          if (parsed.ok)
            push_ok(parsed.value);
          else
            push_error(std::string("convert.try_to_i64: ") + parsed.error);
        } else if (parsed.ok) {
          pushStack(parsed.value);
        } else {
          throw std::runtime_error(std::string("convert.to_i64: ") + parsed.error);
        }
        return;
      }
      if (fn_name == "convert.to_f64" ||
          fn_name == "convert.try_to_f64") {
        require_count(1);
        const auto parsed = parse_finite_f64(require_text(0));
        if (fn_name == "convert.try_to_f64") {
          if (parsed)
            push_ok(*parsed);
          else
            push_error("convert.try_to_f64: expected a finite decimal number");
        } else if (parsed) {
          pushStack(*parsed);
        } else {
          throw std::runtime_error(
              "convert.to_f64: expected a finite decimal number");
        }
        return;
      }
      if (fn_name == "convert.to_bool" ||
          fn_name == "convert.try_to_bool") {
        require_count(1);
        const auto parsed = parse_bool_text(require_text(0));
        if (fn_name == "convert.try_to_bool") {
          if (parsed)
            push_ok(std::int64_t(*parsed ? 1 : 0));
          else
            push_error("convert.try_to_bool: expected true/false, yes/no, on/off, or 1/0");
        } else if (parsed) {
          pushStack(std::int64_t(*parsed ? 1 : 0));
        } else {
          throw std::runtime_error(
              "convert.to_bool: expected true/false, yes/no, on/off, or 1/0");
        }
        return;
      }
      if (fn_name == "convert.i32_to_text" ||
          fn_name == "convert.i64_to_text") {
        require_count(1);
        const std::int64_t value = require_integer(0);
        if (fn_name == "convert.i32_to_text" &&
            (value < std::numeric_limits<std::int32_t>::min() ||
             value > std::numeric_limits<std::int32_t>::max()))
          throw std::runtime_error(
              "convert.i32_to_text: value is outside the i32 range");
        pushStack(std::to_string(value));
        return;
      }
      if (fn_name == "convert.f64_to_text") {
        require_count(1);
        const double value = require_number(0);
        if (!std::isfinite(value))
          throw std::runtime_error("convert.f64_to_text: value must be finite");
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10)
               << value;
        pushStack(output.str());
        return;
      }
      if (fn_name == "convert.f64_to_text_precision") {
        require_count(2);
        const double value = require_number(0);
        const std::int64_t decimals = require_integer(1);
        if (!std::isfinite(value))
          throw std::runtime_error(
              "convert.f64_to_text_precision: value must be finite");
        if (decimals < 0 || decimals > 100)
          throw std::runtime_error(
              "convert.f64_to_text_precision: decimals must be between 0 and 100");
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::fixed << std::setprecision(static_cast<int>(decimals))
               << value;
        pushStack(output.str());
        return;
      }
      if (fn_name == "convert.to_hex" ||
          fn_name == "convert.to_hex_upper" ||
          fn_name == "convert.to_hex_padded" ||
          fn_name == "convert.to_binary" ||
          fn_name == "convert.to_octal") {
        const bool padded = fn_name == "convert.to_hex_padded";
        require_count(padded ? 2 : 1);
        const std::int64_t value = require_integer(0);
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max())
          throw std::runtime_error(fn_name + ": value is outside the i32 range");
        const int base = fn_name == "convert.to_binary"
                             ? 2
                             : (fn_name == "convert.to_octal" ? 8 : 16);
        std::string formatted = format_signed_integer(
            value, base, fn_name == "convert.to_hex_upper");
        if (padded) {
          const std::int64_t width = require_integer(1);
          if (width < 0 || width > 1024)
            throw std::runtime_error(
                "convert.to_hex_padded: width must be between 0 and 1024");
          const bool negative = !formatted.empty() && formatted.front() == '-';
          const std::size_t digits = formatted.size() - (negative ? 1U : 0U);
          if (digits < static_cast<std::size_t>(width))
            formatted.insert(negative ? 1U : 0U,
                             static_cast<std::size_t>(width) - digits, '0');
        }
        pushStack(formatted);
        return;
      }
      if (fn_name == "convert.from_hex" ||
          fn_name == "convert.from_hex_i64" ||
          fn_name == "convert.from_binary" ||
          fn_name == "convert.from_octal" ||
          fn_name == "convert.from_base") {
        require_count(fn_name == "convert.from_base" ? 2 : 1);
        const int base = fn_name == "convert.from_binary"
                             ? 2
                             : (fn_name == "convert.from_octal"
                                    ? 8
                                    : (fn_name == "convert.from_base"
                                           ? static_cast<int>(require_integer(1))
                                           : 16));
        const IntegerParseResult parsed =
            parse_signed_integer(require_text(0), base, true);
        if (!parsed.ok)
          throw std::runtime_error(fn_name + ": " + parsed.error);
        if (fn_name != "convert.from_hex_i64" &&
            fn_name != "convert.from_base" &&
            (parsed.value < std::numeric_limits<std::int32_t>::min() ||
             parsed.value > std::numeric_limits<std::int32_t>::max()))
          throw std::runtime_error(fn_name + ": value is outside the i32 range");
        pushStack(parsed.value);
        return;
      }
      if (fn_name == "convert.char_to_i32") {
        require_count(1);
        const auto scalar = decode_single_utf8_scalar(require_text(0));
        if (!scalar)
          throw std::runtime_error(
              "convert.char_to_i32: expected exactly one valid Unicode scalar");
        pushStack(static_cast<std::int64_t>(*scalar));
        return;
      }
      if (fn_name == "convert.i32_to_char") {
        require_count(1);
        const auto encoded = encode_utf8_scalar(require_integer(0));
        if (!encoded)
          throw std::runtime_error(
              "convert.i32_to_char: value is not a Unicode scalar");
        pushStack(*encoded);
        return;
      }
      throw std::runtime_error("unsupported core conversion intrinsic: " +
                               fn_name);
    }

    // --- Runtime garbage-collector intrinsics ---
    if (lib_name == "intrinsic" && fn_name.rfind("gc.", 0) == 0) {
      const auto counter_value = [&](std::uint64_t value) -> std::int64_t {
        if (value > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()))
          throw std::runtime_error(fn_name + ": counter exceeds i64 range");
        return static_cast<std::int64_t>(value);
      };
      if (fn_name == "gc.collect") {
        if (args.size() != 1 ||
            !std::holds_alternative<std::int64_t>(args[0]))
          throw std::runtime_error("gc.collect: expected one integer generation");
        const std::int64_t generation = std::get<std::int64_t>(args[0]);
        if (generation < 0 || generation > 2)
          throw std::runtime_error(
              "gc.collect: generation must be 0, 1, or 2");
        if (memory_mode_ != MemoryMode::SAFE_GC)
          throw std::runtime_error(
              "gc.collect: nilinterpreter must use --memory-mode=safe");
        if (gc_critical_depth_ != 0)
          throw std::runtime_error(
              "gc.collect: collection is disabled inside a critical section");
        gcCollect();
        return;
      }
      if (fn_name == "gc.allocated_bytes") {
        if (!args.empty())
          throw std::runtime_error("gc.allocated_bytes: expected no arguments");
        pushStack(counter_value(currentHeapBytes()));
        return;
      }
      if (fn_name == "gc.stats") {
        if (!args.empty())
          throw std::runtime_error("gc.stats: expected no arguments");
        StructValue stats;
        stats.type_name = "GCStats";
        stats.fields()["total_collections"] =
            counter_value(total_collections_);
        stats.fields()["total_allocated"] =
            counter_value(total_allocated_bytes_);
        stats.fields()["current_heap_usage"] =
            counter_value(currentHeapBytes());
        stats.fields()["live_allocations"] =
            counter_value(heap_.size());
        stats.fields()["critical_section_depth"] =
            counter_value(gc_critical_depth_);
        pushStack(stats);
        return;
      }
      if (fn_name == "gc.enter_critical") {
        if (!args.empty())
          throw std::runtime_error("gc.enter_critical: expected no arguments");
        if (gc_critical_depth_ == std::numeric_limits<std::uint64_t>::max())
          throw std::runtime_error("gc.enter_critical: nesting limit exceeded");
        ++gc_critical_depth_;
        return;
      }
      if (fn_name == "gc.leave_critical") {
        if (!args.empty())
          throw std::runtime_error("gc.leave_critical: expected no arguments");
        if (gc_critical_depth_ == 0)
          throw std::runtime_error(
              "gc.leave_critical: no critical section is active");
        --gc_critical_depth_;
        return;
      }
      throw std::runtime_error("unsupported garbage-collector intrinsic: " +
                               fn_name);
    }

    // --- Process environment intrinsics ---
    if (lib_name == "intrinsic" &&
        fn_name.rfind("environment.", 0) == 0) {
      auto require_count = [&](std::size_t expected) {
        if (args.size() != expected)
          throw std::runtime_error(fn_name + ": expected " +
                                   std::to_string(expected) + " argument(s)");
      };
      auto require_text = [&](std::size_t index) -> const std::string & {
        if (index >= args.size() ||
            !std::holds_alternative<std::string>(args[index]))
          throw std::runtime_error(fn_name + ": expected a text argument");
        return std::get<std::string>(args[index]);
      };
      auto push_result = [&](bool ok, const std::string &message) {
        EnumValue result;
        result.variant_name = ok ? "Result::Ok" : "Result::Error";
        if (ok)
          result.payloads.push_back(std::int64_t(1));
        else
          result.payloads.push_back(message);
        pushStack(result);
      };

      if (fn_name == "environment.get") {
        require_count(1);
        const std::string &name = require_text(0);
        if (!valid_environment_name(name))
          throw std::runtime_error("environment.get: invalid variable name");
        EnumValue result;
        const char *value = std::getenv(name.c_str());
        if (value == nullptr) {
          result.variant_name = "Option::None";
        } else {
          const std::size_t length = std::strlen(value);
          if (length > max_environment_entry_bytes)
            throw std::runtime_error(
                "environment.get: value exceeds the 1 MiB safety limit");
          result.variant_name = "Option::Some";
          result.payloads.push_back(std::string(value, length));
        }
        pushStack(result);
        return;
      }
      if (fn_name == "environment.set") {
        require_count(2);
        const std::string &name = require_text(0);
        const std::string &value = require_text(1);
        if (!valid_environment_name(name)) {
          push_result(false, "environment.set: invalid variable name");
          return;
        }
        if (value.size() > max_environment_entry_bytes) {
          push_result(false,
                      "environment.set: value exceeds the 1 MiB safety limit");
          return;
        }
        if (value.find('\0') != std::string::npos) {
          push_result(false,
                      "environment.set: value must not contain a NUL byte");
          return;
        }
#ifdef _WIN32
        const int status = _putenv_s(name.c_str(), value.c_str());
#else
        const int status = setenv(name.c_str(), value.c_str(), 1);
#endif
        if (status == 0)
          push_result(true, {});
        else
          push_result(false, "environment.set: host rejected the update");
        return;
      }
      if (fn_name == "environment.unset") {
        require_count(1);
        const std::string &name = require_text(0);
        if (!valid_environment_name(name)) {
          push_result(false, "environment.unset: invalid variable name");
          return;
        }
#ifdef _WIN32
        const int status = _putenv_s(name.c_str(), "");
#else
        const int status = unsetenv(name.c_str());
#endif
        if (status == 0)
          push_result(true, {});
        else
          push_result(false, "environment.unset: host rejected the update");
        return;
      }
      if (fn_name == "environment.list") {
        require_count(0);
        std::vector<std::pair<std::string, std::string>> entries;
        std::size_t total_bytes = 0;
        auto append_entry = [&](const std::string &entry) {
          const std::size_t separator = entry.find('=');
          if (separator == std::string::npos || separator == 0)
            return;
          if (entries.size() >= max_environment_entries)
            throw std::runtime_error(
                "environment.list: environment exceeds the 4096 item safety limit");
          if (entry.size() > max_environment_entry_bytes ||
              total_bytes > max_environment_total_bytes - entry.size())
            throw std::runtime_error(
                "environment.list: environment exceeds the 8 MiB safety limit");
          total_bytes += entry.size();
          entries.emplace_back(entry.substr(0, separator),
                               entry.substr(separator + 1));
        };
#ifdef _WIN32
        LPCH block = GetEnvironmentStringsA();
        if (block == nullptr)
          throw std::runtime_error(
              "environment.list: host environment is unavailable");
        try {
          for (const char *entry = block; *entry != '\0';
               entry += std::strlen(entry) + 1)
            append_entry(entry);
        } catch (...) {
          FreeEnvironmentStringsA(block);
          throw;
        }
        FreeEnvironmentStringsA(block);
#else
        if (::environ == nullptr)
          throw std::runtime_error(
              "environment.list: host environment is unavailable");
        for (char **entry = ::environ; *entry != nullptr; ++entry)
          append_entry(*entry);
#endif
        std::sort(entries.begin(), entries.end());
        HeapBlock result;
        result.type_name = "array";
        result.element_type = "environment_variable";
        result.element_count = entries.size();
        result.data.reserve(entries.size());
        for (const auto &[name, value] : entries) {
          StructValue item;
          item.type_name = "environment_variable";
          item.string_fields()["name"] = name;
          item.string_fields()["value"] = value;
          result.data.push_back(item);
        }
        const int64_t handle = next_ptr_;
        const std::size_t storage_bytes =
            std::max<std::size_t>(entries.size(), 1U) * 8U;
        next_ptr_ +=
            static_cast<int64_t>((storage_bytes + 7U) & ~std::size_t(7U));
        storeHeapBlock(handle, std::move(result));
        pushStack(ArrayHandle{handle});
        return;
      }
      if (fn_name == "environment.current_directory") {
        require_count(0);
        std::error_code error;
        const fs::path current = fs::current_path(error);
        if (error)
          throw std::runtime_error(
              "environment.current_directory: " + error.message());
        pushStack(current.string());
        return;
      }
      if (fn_name == "environment.set_current_directory") {
        require_count(1);
        std::error_code error;
        fs::current_path(require_text(0), error);
        if (error)
          push_result(false,
                      "environment.set_current_directory: " + error.message());
        else
          push_result(true, {});
        return;
      }
      if (fn_name == "environment.machine_name") {
        require_count(0);
#ifdef _WIN32
        char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD size = sizeof(buffer);
        if (!GetComputerNameA(buffer, &size))
          throw std::runtime_error(
              "environment.machine_name: host name is unavailable");
        pushStack(std::string(buffer, size));
#else
        char buffer[256] = {};
        if (gethostname(buffer, sizeof(buffer) - 1) != 0)
          throw std::runtime_error(
              "environment.machine_name: host name is unavailable");
        buffer[sizeof(buffer) - 1] = '\0';
        pushStack(std::string(buffer));
#endif
        return;
      }
      if (fn_name == "environment.user_name") {
        require_count(0);
#ifdef _WIN32
        char buffer[256] = {};
        DWORD size = static_cast<DWORD>(sizeof(buffer));
        if (!GetUserNameA(buffer, &size) || size <= 1)
          throw std::runtime_error(
              "environment.user_name: user name is unavailable");
        pushStack(std::string(buffer, static_cast<std::size_t>(size - 1)));
#else
        long buffer_size = sysconf(_SC_GETPW_R_SIZE_MAX);
        if (buffer_size < 1024 || buffer_size > 1024 * 1024)
          buffer_size = 16384;
        std::vector<char> buffer(static_cast<std::size_t>(buffer_size));
        struct passwd record {};
        struct passwd *resolved = nullptr;
        if (getpwuid_r(geteuid(), &record, buffer.data(), buffer.size(),
                       &resolved) != 0 ||
            resolved == nullptr || resolved->pw_name == nullptr ||
            *resolved->pw_name == '\0')
          throw std::runtime_error(
              "environment.user_name: user name is unavailable");
        pushStack(std::string(resolved->pw_name));
#endif
        return;
      }
      throw std::runtime_error("unsupported environment intrinsic: " +
                               fn_name);
    }

    // --- Runtime Intrinsics ---
    if (lib_name == "runtime") {
      if (fn_name == "_char_code") {
        std::string s = get_str(args[0]);
        pushStack((int64_t)(s.empty() ? 0 : (unsigned char)s[0]));
        return;
      }
      if (fn_name == "_char_from_code") {
        int32_t code = (int32_t)std::get<int64_t>(args[0]);
        std::string s(1, (char)code);
        pushStack(s);
        return;
      }
      if (fn_name == "_int_to_string" || fn_name == "_int64_to_string") {
        int64_t n = std::get<int64_t>(args[0]);
        pushStack(std::to_string(n));
        return;
      }
      if (fn_name == "_f64_to_string") {
        double d = std::get<double>(args[0]);
        pushStack(std::to_string(d));
        return;
      }
      if (fn_name == "_text_to_f64") {
        std::string s = get_str(args[0]);
        try {
          pushStack(std::stod(s));
        } catch (...) {
          pushStack(0.0);
        }
        return;
      }
    }
    // --- Console Intrinsics ---
    if (lib_name == "intrinsic" && (fn_name == "console.writeln" || fn_name == "console.write")) {
      if (!args.empty()) {
        const Value &v = args[0];
        std::string s = "";
        if (std::holds_alternative<std::string>(v))
          s = std::get<std::string>(v);
        else if (std::holds_alternative<int64_t>(v))
          s = std::to_string(std::get<int64_t>(v));
        else if (std::holds_alternative<double>(v))
          s = std::to_string(std::get<double>(v));
        auto &output = outputStream();
        output << s;
        if (fn_name == "console.writeln") output << std::endl;
        else output << std::flush;
      }
      if (instr.args.size() > 3 && instr.args[3] != "void") {
        pushStack((int64_t)0);
      }
      return;
    }
    if (lib_name == "intrinsic" && fn_name == "console.error") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        std::cerr << std::get<std::string>(args[0]) << std::flush;
      }
      if (instr.args.size() > 3 && instr.args[3] != "void") {
        pushStack((int64_t)0);
      }
      return;
    }
    if (lib_name == "intrinsic" && fn_name == "console.read_line") {
      std::string line;
      if (!std::getline(inputStream(), line)) {
        throw std::runtime_error(
            "console.read_line: input is unavailable or reached EOF");
      }
      pushStack(line);
      return;
    }
    if (lib_name == "intrinsic" && fn_name == "console.flush") {
      outputStream() << std::flush;
      return;
    }
    // --- Standard Intrinsics (std_sys, std_time) ---
    if (lib_name == "std_time") {
      if (fn_name == "now") {
        if (!args.empty())
          throw std::runtime_error("std_time.now: expected no arguments");
        pushStack(time_support::now_ms());
        return;
      }
      if (fn_name == "sleep") {
        if (args.size() != 1 || !std::holds_alternative<int64_t>(args[0]))
          throw std::runtime_error("std_time.sleep: expected one integer duration");
        time_support::sleep_ms(std::get<int64_t>(args[0]));
        pushStack((int64_t)0);
        return;
      }
    }

    if (lib_name == "std_sys" || (lib_name == "default" && fn_name == "sys_write")) {
      if (fn_name == "sys_write") {
        if (!args.empty()) {
          const Value &v = args[0];
          std::string s = "";
          if (std::holds_alternative<std::string>(v))
            s = std::get<std::string>(v);
          else if (std::holds_alternative<int64_t>(v))
            s = std::to_string(std::get<int64_t>(v));
          else if (std::holds_alternative<double>(v))
            s = std::to_string(std::get<double>(v));
          outputStream() << s << std::flush;
        }
        return;
      }
      if (fn_name == "exit") {
        if (args.size() != 1 ||
            !std::holds_alternative<std::int64_t>(args[0]))
          throw std::runtime_error("std_sys.exit: expected one integer code");
        const std::int64_t requested = std::get<std::int64_t>(args[0]);
        if (requested < 0 || requested > 255)
          throw std::runtime_error(
              "std_sys.exit: code must be between 0 and 255");
        const int code = static_cast<int>(requested);
        if (trap_process_exit_) {
          requested_exit_code_ = code;
          pc_ = program_.size();
          return;
        }
        // std::exit bypasses VM destruction. Preserve the retained heap before
        // leaving, while keeping the program's requested status on success.
        std::exit(finishMemoryProfile("completed", true) ? code : 1);
      } else if (fn_name == "arg_count") {
        if (!args.empty())
          throw std::runtime_error("std_sys.arg_count: expected no arguments");
        pushStack((int64_t)args_.size());
        return;
      } else if (fn_name == "arg") {
        if (args.size() != 1 ||
            !std::holds_alternative<std::int64_t>(args[0]))
          throw std::runtime_error("std_sys.arg: expected one integer index");
        const std::int64_t index = std::get<std::int64_t>(args[0]);
        if (index < 0 || index >= static_cast<std::int64_t>(args_.size()))
          throw std::runtime_error("std_sys.arg: index is outside the argument list");
        pushStack(args_[static_cast<std::size_t>(index)]);
        return;
      }
    }

    // --- Path Intrinsics ---
    if (fn_name == "path.combine") {
      if (args.size() == 2) {
        fs::path p1 = get_str(args[0]);
        fs::path p2 = get_str(args[1]);
        pushStack((p1 / p2).string());
      } else {
        pushStack(std::string(""));
      }
      return;
    }
    if (fn_name == "path.basename") {
      if (args.size() == 1)
        pushStack(fs::path(get_str(args[0])).filename().string());
      else
        pushStack(std::string(""));
      return;
    }
    if (fn_name == "path.dirname") {
      if (args.size() == 1)
        pushStack(fs::path(get_str(args[0])).parent_path().string());
      else
        pushStack(std::string(""));
      return;
    }
    if (fn_name == "path.extension") {
      if (args.size() == 1)
        pushStack(fs::path(get_str(args[0])).extension().string());
      else
        pushStack(std::string(""));
      return;
    }
    if (fn_name == "path.extension_no_dot") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        std::string extension =
            fs::path(std::get<std::string>(args[0])).extension().string();
        if (!extension.empty() && extension.front() == '.')
          extension.erase(extension.begin());
        pushStack(std::move(extension));
      } else {
        pushStack(std::string(""));
      }
      return;
    }
    if (fn_name == "path.stem") {
      if (args.size() == 1)
        pushStack(fs::path(get_str(args[0])).stem().string());
      else
        pushStack(std::string(""));
      return;
    }
    if (fn_name == "path.absolute") {
      if (args.size() == 1) {
        try {
          pushStack(fs::absolute(get_str(args[0])).string());
        } catch (...) {
          pushStack(std::string(""));
        }
      } else
        pushStack(std::string(""));
      return;
    }
    if (fn_name == "path.is_absolute") {
      if (args.size() == 1) {
        pushStack((int64_t)(fs::path(get_str(args[0])).is_absolute() ? 1 : 0));
      } else
        pushStack((int64_t)0);
      return;
    }

    // --- Dir Intrinsics ---
    if (fn_name == "dir.exists") {
      if (args.size() == 1) {
        std::string p = get_str(args[0]);
        std::error_code ec;
        bool exists = fs::exists(p, ec) && fs::is_directory(p, ec);
        pushStack((int64_t)(exists ? 1 : 0));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.create") {
      if (args.size() == 1) {
        std::error_code ec;
        pushStack(
            (int64_t)(fs::create_directories(get_str(args[0]), ec) ? 1 : 0));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.delete") {
      if (args.size() == 1) {
        std::error_code ec;
        pushStack((int64_t)fs::remove_all(get_str(args[0]), ec));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.list") {
      if (args.size() == 1) {
        std::string p = get_str(args[0]);
        ArrayValue arr;
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
          for (const auto &entry : fs::directory_iterator(p, ec)) {
            StructValue sv;
            sv.type_name = "dir_entry";
            sv.string_fields()["name"] = entry.path().filename().string();
            sv.string_fields()["path"] = entry.path().string();
            sv.fields()["is_file"] = entry.is_regular_file() ? 1 : 0;
            sv.fields()["is_directory"] = entry.is_directory() ? 1 : 0;
            arr.elements.push_back(sv);
          }
        }
        pushStack(arr);
      } else {
        ArrayValue arr;
        pushStack(arr);
      }
      return;
    }

    // --- Additional Path Intrinsics ---
    if (fn_name == "path.separator") {
#ifdef _WIN32
      pushStack(std::string("\\"));
#else
      pushStack(std::string("/"));
#endif
      return;
    }
    if (fn_name == "path.normalize") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        pushStack(fs::path(std::get<std::string>(args[0]))
                      .lexically_normal()
                      .string());
      } else {
        pushStack(std::string(""));
      }
      return;
    }
    if (fn_name == "path.relative") {
      if (args.size() == 2 &&
          std::holds_alternative<std::string>(args[0]) &&
          std::holds_alternative<std::string>(args[1])) {
        pushStack(fs::path(std::get<std::string>(args[1]))
                      .lexically_relative(std::get<std::string>(args[0]))
                      .string());
      } else {
        pushStack(std::string(""));
      }
      return;
    }
    if (fn_name == "path.ends_with") {
      if (args.size() == 2 &&
          std::holds_alternative<std::string>(args[0]) &&
          std::holds_alternative<std::string>(args[1])) {
        const std::string &path = std::get<std::string>(args[0]);
        const std::string &suffix = std::get<std::string>(args[1]);
        const bool matches = suffix.size() <= path.size() &&
                             path.compare(path.size() - suffix.size(),
                                          suffix.size(), suffix) == 0;
        pushStack(int64_t(matches ? 1 : 0));
      } else {
        pushStack(int64_t(0));
      }
      return;
    }
    if (fn_name == "path.with_extension") {
      if (args.size() == 2 &&
          std::holds_alternative<std::string>(args[0]) &&
          std::holds_alternative<std::string>(args[1])) {
        fs::path path(std::get<std::string>(args[0]));
        path.replace_extension(std::get<std::string>(args[1]));
        pushStack(path.string());
      } else {
        pushStack(std::string(""));
      }
      return;
    }

    // --- Additional Dir Intrinsics ---
    if (fn_name == "dir.create_all") {
      if (args.size() == 1) {
        std::error_code ec;
        pushStack(
            (int64_t)(fs::create_directories(get_str(args[0]), ec) ? 1 : 0));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.delete_all") {
      if (args.size() == 1) {
        std::error_code ec;
        pushStack((int64_t)fs::remove_all(get_str(args[0]), ec));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.move") {
      if (args.size() == 2) {
        std::error_code ec;
        fs::rename(get_str(args[0]), get_str(args[1]), ec);
        pushStack((int64_t)(ec ? 0 : 1));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.current") {
      std::error_code ec;
      pushStack(fs::current_path(ec).string());
      return;
    }
    if (fn_name == "dir.set_current") {
      if (args.size() == 1) {
        std::error_code ec;
        fs::current_path(get_str(args[0]), ec);
        pushStack((int64_t)(ec ? 0 : 1));
      } else
        pushStack((int64_t)0);
      return;
    }
    if (fn_name == "dir.home") {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif
      pushStack(std::string(home ? home : ""));
      return;
    }
    if (fn_name == "dir.temp") {
      std::error_code ec;
      pushStack(fs::temp_directory_path(ec).string());
      return;
    }

    // --- File Intrinsics ---
    auto file_byte_span = [&](int64_t pointer, int64_t length)
        -> std::optional<std::pair<char *, std::size_t>> {
      if (length < 0 ||
          static_cast<std::uint64_t>(length) > max_intrinsic_file_bytes) {
        return std::nullopt;
      }

      for (auto &[base, block] : heap_) {
        if (!is_byte_heap_type(block.type_name))
          continue;
        if (pointer < base)
          continue;
        const auto offset = static_cast<std::uint64_t>(pointer) -
                            static_cast<std::uint64_t>(base);
        const auto block_size =
            static_cast<std::uint64_t>(block.byte_data.size());
        const bool points_into_block = offset < block_size;
        const bool points_to_empty_end = length == 0 && offset == block_size;
        if (!points_into_block && !points_to_empty_end)
          continue;

        const auto byte_offset = static_cast<std::size_t>(offset);
        const auto requested = static_cast<std::size_t>(length);
        if (byte_offset > block.byte_data.size() ||
            requested > block.byte_data.size() - byte_offset) {
          return std::nullopt;
        }
        char *data = block.byte_data.empty()
                         ? nullptr
                         : block.byte_data.data() + byte_offset;
        return std::make_pair(data, requested);
      }
      return std::nullopt;
    };

    if (fn_name == "file.handle_open") {
      if (args.size() != 2 ||
          !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<int64_t>(args[1])) {
        pushStack(int64_t(-1));
        return;
      }

      const std::string &path = std::get<std::string>(args[0]);
      const int64_t mode = std::get<int64_t>(args[1]);
      auto stream = std::make_unique<std::fstream>();
      bool readable = false;
      bool writable = false;
      std::ios::openmode open_mode = std::ios::binary;
      switch (mode) {
      case 0: // read an existing file
        readable = true;
        open_mode |= std::ios::in;
        break;
      case 1: // create/truncate for writing
        writable = true;
        open_mode |= std::ios::out | std::ios::trunc;
        break;
      case 2: // create/append for writing
        writable = true;
        open_mode |= std::ios::out | std::ios::app;
        break;
      case 3: // read/write, creating the file when absent
        readable = true;
        writable = true;
        open_mode |= std::ios::in | std::ios::out;
        break;
      default:
        pushStack(int64_t(-1));
        return;
      }

      stream->open(path, open_mode);
      if (!*stream && mode == 3) {
        // std::fstream(in|out) does not create a missing file. Create it
        // without truncating an existing file, then retry the requested mode.
        stream->clear();
        std::ofstream create(path, std::ios::binary | std::ios::app);
        if (create) {
          create.close();
          stream->open(path, open_mode);
        }
      }
      if (!*stream) {
        pushStack(int64_t(-1));
        return;
      }

      if (next_file_handle_ <= 0 || next_file_handle_ >= next_ptr_) {
        pushStack(int64_t(-1));
        return;
      }
      const int64_t handle = next_file_handle_++;
      FileHandle owned;
      owned.stream = std::move(stream);
      owned.readable = readable;
      owned.writable = writable;
      file_handles_.emplace(handle, std::move(owned));
      pushStack(handle);
      return;
    }
    if (fn_name == "file.handle_read") {
      if (args.size() != 3 || !std::holds_alternative<int64_t>(args[0]) ||
          !std::holds_alternative<int64_t>(args[1]) ||
          !std::holds_alternative<int64_t>(args[2])) {
        pushStack(int64_t(-1));
        return;
      }

      const int64_t handle = std::get<int64_t>(args[0]);
      const int64_t pointer = std::get<int64_t>(args[1]);
      const int64_t length = std::get<int64_t>(args[2]);
      auto file = file_handles_.find(handle);
      const auto span = file_byte_span(pointer, length);
      if (file == file_handles_.end() || !file->second.readable || !span) {
        pushStack(int64_t(-1));
        return;
      }
      if (span->second == 0) {
        pushStack(int64_t(0));
        return;
      }

      std::fstream &stream = *file->second.stream;
      stream.read(span->first, static_cast<std::streamsize>(span->second));
      const std::streamsize count = stream.gcount();
      if (stream.bad() || (!stream && !stream.eof())) {
        pushStack(int64_t(-1));
      } else {
        pushStack(static_cast<int64_t>(count));
      }
      return;
    }
    if (fn_name == "file.handle_write") {
      if (args.size() != 3 || !std::holds_alternative<int64_t>(args[0]) ||
          !std::holds_alternative<int64_t>(args[1]) ||
          !std::holds_alternative<int64_t>(args[2])) {
        pushStack(int64_t(-1));
        return;
      }

      const int64_t handle = std::get<int64_t>(args[0]);
      const int64_t pointer = std::get<int64_t>(args[1]);
      const int64_t length = std::get<int64_t>(args[2]);
      auto file = file_handles_.find(handle);
      const auto span = file_byte_span(pointer, length);
      if (file == file_handles_.end() || !file->second.writable || !span) {
        pushStack(int64_t(-1));
        return;
      }
      if (span->second == 0) {
        pushStack(int64_t(0));
        return;
      }

      std::fstream &stream = *file->second.stream;
      stream.write(span->first, static_cast<std::streamsize>(span->second));
      if (!stream) {
        pushStack(int64_t(-1));
      } else {
        pushStack(static_cast<int64_t>(span->second));
      }
      return;
    }
    if (fn_name == "file.handle_flush") {
      if (args.size() != 1 || !std::holds_alternative<int64_t>(args[0])) {
        pushStack(int64_t(-1));
        return;
      }
      auto file = file_handles_.find(std::get<int64_t>(args[0]));
      if (file == file_handles_.end() || !file->second.writable) {
        pushStack(int64_t(-1));
        return;
      }
      file->second.stream->flush();
      pushStack(*file->second.stream ? int64_t(0) : int64_t(-1));
      return;
    }
    if (fn_name == "file.handle_close") {
      if (args.size() != 1 || !std::holds_alternative<int64_t>(args[0])) {
        pushStack(int64_t(-1));
        return;
      }
      auto file = file_handles_.find(std::get<int64_t>(args[0]));
      if (file == file_handles_.end()) {
        pushStack(int64_t(-1));
        return;
      }
      // EOF is a normal read condition and leaves failbit set. Clear logical
      // I/O state before close so it cannot be mistaken for a close failure.
      file->second.stream->clear();
      file->second.stream->close();
      const bool closed_cleanly = !file->second.stream->fail();
      file_handles_.erase(file);
      pushStack(closed_cleanly ? int64_t(0) : int64_t(-1));
      return;
    }
    if (fn_name == "file.handle_seek") {
      if (args.size() != 3 || !std::holds_alternative<int64_t>(args[0]) ||
          !std::holds_alternative<int64_t>(args[1]) ||
          !std::holds_alternative<int64_t>(args[2])) {
        pushStack(int64_t(-1));
        return;
      }

      auto file = file_handles_.find(std::get<int64_t>(args[0]));
      const int64_t offset = std::get<int64_t>(args[1]);
      const int64_t origin = std::get<int64_t>(args[2]);
      if (file == file_handles_.end() || origin < 0 || origin > 2) {
        pushStack(int64_t(-1));
        return;
      }

      std::fstream &stream = *file->second.stream;
      auto tell = [&]() -> std::streampos {
        std::streampos position = std::streampos(-1);
        if (file->second.readable)
          position = stream.tellg();
        if (position == std::streampos(-1) && file->second.writable)
          position = stream.tellp();
        return position;
      };
      auto seek_absolute = [&](int64_t position) -> bool {
        stream.clear();
        if (file->second.readable)
          stream.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (file->second.writable)
          stream.seekp(static_cast<std::streamoff>(position), std::ios::beg);
        return static_cast<bool>(stream);
      };

      stream.clear();
      const std::streampos saved_position = tell();
      int64_t base = 0;
      if (origin == 1) {
        if (saved_position == std::streampos(-1)) {
          pushStack(int64_t(-1));
          return;
        }
        base = static_cast<int64_t>(
            static_cast<std::streamoff>(saved_position));
      } else if (origin == 2) {
        stream.clear();
        if (file->second.readable)
          stream.seekg(0, std::ios::end);
        else
          stream.seekp(0, std::ios::end);
        const std::streampos end_position = tell();
        if (end_position == std::streampos(-1)) {
          pushStack(int64_t(-1));
          return;
        }
        base = static_cast<int64_t>(static_cast<std::streamoff>(end_position));
        if (saved_position != std::streampos(-1) &&
            !seek_absolute(static_cast<int64_t>(
                static_cast<std::streamoff>(saved_position)))) {
          pushStack(int64_t(-1));
          return;
        }
      }

      if ((offset > 0 && base > std::numeric_limits<int64_t>::max() - offset) ||
          (offset < 0 &&
           (offset == std::numeric_limits<int64_t>::min() || base < -offset))) {
        pushStack(int64_t(-1));
        return;
      }
      const int64_t target = base + offset;
      if (target < 0 || !seek_absolute(target)) {
        pushStack(int64_t(-1));
        return;
      }
      pushStack(target);
      return;
    }

    if (fn_name == "file.exists") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        std::error_code ec;
        const std::string &path = std::get<std::string>(args[0]);
        bool exists = fs::exists(path, ec) && fs::is_regular_file(path, ec);
        pushStack((int64_t)(exists ? 1 : 0));
      } else
        pushStack((int64_t)0);
      return;
    }
    // file.read_text(path) -> result<text, text>
    if (fn_name == "file.read_text") {
      auto push_ok_str = [&](const std::string &s) {
        EnumValue ev; ev.variant_name = "Result::Ok"; ev.payloads.push_back(s); pushStack(ev);
      };
      auto push_err_str = [&](const std::string &s) {
        EnumValue ev; ev.variant_name = "Result::Error"; ev.payloads.push_back(s); pushStack(ev);
      };
      if (args.size() != 1 ||
          !std::holds_alternative<std::string>(args[0])) {
        push_err_str("file.read_text: expected one text path");
        return;
      }

      const std::string &path = std::get<std::string>(args[0]);
      std::ifstream input(path, std::ios::binary | std::ios::ate);
      if (!input) {
        push_err_str("File not found or unreadable: " + path);
        return;
      }
      const std::streampos end = input.tellg();
      if (end < 0 || static_cast<std::uint64_t>(end) >
                         max_intrinsic_file_bytes) {
        push_err_str("file.read_text: file exceeds the 512 MiB safety limit");
        return;
      }
      const auto byte_count = static_cast<std::size_t>(end);
      std::string contents(byte_count, '\0');
      input.seekg(0, std::ios::beg);
      if (byte_count != 0 &&
          !input.read(contents.data(),
                      static_cast<std::streamsize>(byte_count))) {
        push_err_str("file.read_text: short read from " + path);
        return;
      }
      push_ok_str(contents);
      return;
    }
    // file.read_bytes(path) -> result<array<u8>, text>
    if (fn_name == "file.read_bytes") {
      auto push_error = [&](const std::string &message) {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(message);
        pushStack(result);
      };
      if (args.size() != 1 || !std::holds_alternative<std::string>(args[0])) {
        push_error("file.read_bytes: expected one text path");
        return;
      }

      const std::string path = std::get<std::string>(args[0]);
      std::ifstream input(path, std::ios::binary | std::ios::ate);
      if (!input) {
        push_error("File not found: " + path);
        return;
      }
      const std::streampos end = input.tellg();
      if (end < 0 || static_cast<std::uint64_t>(end) > max_intrinsic_file_bytes) {
        push_error("file.read_bytes: file exceeds the 512 MiB safety limit");
        return;
      }
      const auto byte_count = static_cast<std::size_t>(end);
      input.seekg(0, std::ios::beg);

      HeapBlock block;
      block.type_name = "bytes";
      block.element_count = byte_count;
      block.byte_data.resize(byte_count);
      if (byte_count != 0 &&
          !input.read(block.byte_data.data(),
                      static_cast<std::streamsize>(byte_count))) {
        push_error("file.read_bytes: short read from " + path);
        return;
      }

      const int64_t handle = next_ptr_;
      const std::size_t storage_bytes = std::max<std::size_t>(byte_count, 1U);
      next_ptr_ += static_cast<int64_t>((storage_bytes + 7U) & ~std::size_t(7U));
      storeHeapBlock(handle, std::move(block));

      EnumValue result;
      result.variant_name = "Result::Ok";
      result.payloads.push_back(ArrayHandle{handle});
      pushStack(result);
      return;
    }
    // file.read_regular_bytes(path, maximum_bytes)
    //     -> result<array<u8>, text>
    if (fn_name == "file.read_regular_bytes") {
      auto push_error = [&](const std::string &message) {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(message);
        pushStack(result);
      };
      if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<int64_t>(args[1])) {
        push_error(
            "file.read_regular_bytes: expected a text path and i64 maximum_bytes");
        return;
      }
      const std::string &path = std::get<std::string>(args[0]);
      const int64_t requested_limit = std::get<int64_t>(args[1]);
      if (!detail::valid_file_path(path)) {
        push_error(
            "file.read_regular_bytes: path must be non-empty and contain no NUL byte");
        return;
      }
      if (requested_limit < 0 ||
          static_cast<std::uint64_t>(requested_limit) >
              max_intrinsic_file_bytes) {
        push_error(
            "file.read_regular_bytes: maximum_bytes must be between 0 and 536870912");
        return;
      }

      detail::BoundedFileRead loaded;
      try {
        loaded = detail::read_regular_file_bounded(
            path, static_cast<std::uint64_t>(requested_limit));
      } catch (const std::bad_alloc &) {
        push_error("file.read_regular_bytes: allocation failed");
        return;
      } catch (...) {
        push_error("file.read_regular_bytes: host operation failed");
        return;
      }
      if (!loaded.ok) {
        push_error(loaded.error);
        return;
      }

      HeapBlock block;
      block.type_name = "bytes";
      block.element_count = loaded.bytes.size();
      block.byte_data = std::move(loaded.bytes);
      const int64_t handle = next_ptr_;
      const std::size_t storage_bytes =
          std::max<std::size_t>(block.byte_data.size(), 1U);
      next_ptr_ +=
          static_cast<int64_t>((storage_bytes + 7U) & ~std::size_t(7U));
      storeHeapBlock(handle, std::move(block));

      EnumValue result;
      result.variant_name = "Result::Ok";
      result.payloads.push_back(ArrayHandle{handle});
      pushStack(result);
      return;
    }
    // file.write_text(path, content) -> result<bool, text>
    if (fn_name == "file.write_text") {
      auto push_ok_b = [&](bool b) {
        EnumValue ev; ev.variant_name = "Result::Ok"; ev.payloads.push_back((int64_t)(b ? 1 : 0)); pushStack(ev);
      };
      auto push_err_s = [&](const std::string &s) {
        EnumValue ev; ev.variant_name = "Result::Error"; ev.payloads.push_back(s); pushStack(ev);
      };
      if (args.size() != 2 ||
          !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<std::string>(args[1])) {
        push_err_s("file.write_text: expected a text path and text content");
        return;
      }
      const std::string &path = std::get<std::string>(args[0]);
      const std::string &contents = std::get<std::string>(args[1]);
      if (contents.size() > max_intrinsic_file_bytes) {
        push_err_s("file.write_text: content exceeds the 512 MiB safety limit");
        return;
      }
      std::ofstream output(path, std::ios::binary | std::ios::trunc);
      if (!output) {
        push_err_s("Cannot write: " + path);
        return;
      }
      if (!contents.empty())
        output.write(contents.data(),
                     static_cast<std::streamsize>(contents.size()));
      output.flush();
      if (!output) {
        push_err_s("Write failed: " + path);
        return;
      }
      push_ok_b(true);
      return;
    }
    // file.write_bytes(path, content) -> result<bool, text>
    if (fn_name == "file.write_bytes") {
      auto push_ok = [&]() {
        EnumValue result;
        result.variant_name = "Result::Ok";
        result.payloads.push_back(int64_t(1));
        pushStack(result);
      };
      auto push_error = [&](const std::string &message) {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(message);
        pushStack(result);
      };
      if (args.size() != 2 || !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<ArrayHandle>(args[1])) {
        push_error("file.write_bytes: expected a text path and byte array");
        return;
      }

      const int64_t handle = std::get<ArrayHandle>(args[1]).address;
      const auto block = heap_.find(handle);
      if (block == heap_.end()) {
        push_error("file.write_bytes: invalid byte array");
        return;
      }

      std::vector<char> bytes;
      if (is_byte_heap_type(block->second.type_name)) {
        if (block->second.byte_data.size() > max_intrinsic_file_bytes) {
          push_error("file.write_bytes: byte array exceeds the 512 MiB safety limit");
          return;
        }
        bytes = block->second.byte_data;
      } else if (block->second.type_name == "array") {
        if (block->second.data.size() > max_intrinsic_file_bytes) {
          push_error("file.write_bytes: array exceeds the 512 MiB safety limit");
          return;
        }
        bytes.reserve(block->second.data.size());
        for (const Value &value : block->second.data) {
          const auto *integer = std::get_if<int64_t>(&value);
          if (integer == nullptr || *integer < 0 || *integer > 255) {
            push_error("file.write_bytes: array contains a value outside u8");
            return;
          }
          bytes.push_back(static_cast<char>(static_cast<unsigned char>(*integer)));
        }
      } else {
        push_error("file.write_bytes: value is not a byte array");
        return;
      }

      std::ofstream output(std::get<std::string>(args[0]),
                           std::ios::binary | std::ios::trunc);
      if (!output) {
        push_error("Cannot write: " + std::get<std::string>(args[0]));
        return;
      }
      if (!bytes.empty())
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      output.flush();
      if (!output) {
        push_error("file.write_bytes: write failed");
        return;
      }
      push_ok();
      return;
    }
    // file.write_bytes_atomic(path, content, replace) -> result<bool, text>
    if (fn_name == "file.write_bytes_atomic") {
      auto push_ok = [&]() {
        EnumValue result;
        result.variant_name = "Result::Ok";
        result.payloads.push_back(int64_t(1));
        pushStack(result);
      };
      auto push_error = [&](const std::string &message) {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(message);
        pushStack(result);
      };
      if (args.size() != 3 || !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<ArrayHandle>(args[1]) ||
          !std::holds_alternative<int64_t>(args[2])) {
        push_error(
            "file.write_bytes_atomic: expected a text path, byte array, and bool replace");
        return;
      }
      const std::string &path = std::get<std::string>(args[0]);
      if (!detail::valid_file_path(path)) {
        push_error(
            "file.write_bytes_atomic: path must be non-empty and contain no NUL byte");
        return;
      }
      const int64_t replace_value = std::get<int64_t>(args[2]);
      if (replace_value != 0 && replace_value != 1) {
        push_error("file.write_bytes_atomic: replace must be a bool");
        return;
      }

      const int64_t handle = std::get<ArrayHandle>(args[1]).address;
      const auto block = heap_.find(handle);
      if (block == heap_.end()) {
        push_error("file.write_bytes_atomic: invalid byte array");
        return;
      }
      std::vector<char> bytes;
      try {
        if (is_byte_heap_type(block->second.type_name)) {
          if (block->second.byte_data.size() > max_intrinsic_file_bytes) {
            push_error(
                "file.write_bytes_atomic: byte array exceeds the 512 MiB safety limit");
            return;
          }
          bytes = block->second.byte_data;
        } else if (block->second.type_name == "array") {
          if (block->second.data.size() > max_intrinsic_file_bytes) {
            push_error(
                "file.write_bytes_atomic: array exceeds the 512 MiB safety limit");
            return;
          }
          bytes.reserve(block->second.data.size());
          for (const Value &value : block->second.data) {
            const auto *integer = std::get_if<int64_t>(&value);
            if (integer == nullptr || *integer < 0 || *integer > 255) {
              push_error(
                  "file.write_bytes_atomic: array contains a value outside u8");
              return;
            }
            bytes.push_back(
                static_cast<char>(static_cast<unsigned char>(*integer)));
          }
        } else {
          push_error("file.write_bytes_atomic: value is not a byte array");
          return;
        }
      } catch (const std::bad_alloc &) {
        push_error("file.write_bytes_atomic: allocation failed");
        return;
      }

      detail::AtomicFileWrite written;
      try {
        written = detail::write_file_atomic(path, bytes, replace_value != 0);
      } catch (...) {
        push_error("file.write_bytes_atomic: host operation failed");
        return;
      }
      if (!written.ok) {
        push_error(written.error);
        return;
      }
      push_ok();
      return;
    }
    // file.append_text(path, content) -> result<bool, text>
    if (fn_name == "file.append_text") {
      auto push_ok_b = [&](bool b) {
        EnumValue ev; ev.variant_name = "Result::Ok"; ev.payloads.push_back((int64_t)(b ? 1 : 0)); pushStack(ev);
      };
      auto push_err_s = [&](const std::string &s) {
        EnumValue ev; ev.variant_name = "Result::Error"; ev.payloads.push_back(s); pushStack(ev);
      };
      if (args.size() != 2 ||
          !std::holds_alternative<std::string>(args[0]) ||
          !std::holds_alternative<std::string>(args[1])) {
        push_err_s("file.append_text: expected a text path and text content");
        return;
      }
      const std::string &path = std::get<std::string>(args[0]);
      const std::string &contents = std::get<std::string>(args[1]);
      if (contents.size() > max_intrinsic_file_bytes) {
        push_err_s("file.append_text: content exceeds the 512 MiB safety limit");
        return;
      }
      std::ofstream output(path, std::ios::binary | std::ios::app);
      if (!output) {
        push_err_s("Cannot append: " + path);
        return;
      }
      if (!contents.empty())
        output.write(contents.data(),
                     static_cast<std::streamsize>(contents.size()));
      output.flush();
      if (!output) {
        push_err_s("Append failed: " + path);
        return;
      }
      push_ok_b(true);
      return;
    }
    // file.size(path) -> result<i64, text>
    if (fn_name == "file.size") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        std::error_code ec;
        auto sz = fs::file_size(std::get<std::string>(args[0]), ec);
        if (!ec && sz <= static_cast<std::uintmax_t>(
                             std::numeric_limits<int64_t>::max())) {
          EnumValue ev; ev.variant_name = "Result::Ok"; ev.payloads.push_back((int64_t)sz); pushStack(ev);
        } else {
          EnumValue ev; ev.variant_name = "Result::Error";
          ev.payloads.push_back(ec ? ec.message()
                                   : std::string("file.size: size exceeds i64"));
          pushStack(ev);
        }
      } else { EnumValue ev; ev.variant_name = "Result::Error"; ev.payloads.push_back(std::string("file.size: expected one text path")); pushStack(ev); }
      return;
    }
    if (fn_name == "file.delete") {
      if (args.size() == 1 &&
          std::holds_alternative<std::string>(args[0])) {
        std::error_code ec;
        const bool removed = fs::remove(std::get<std::string>(args[0]), ec);
        EnumValue result;
        result.variant_name = ec ? "Result::Error" : "Result::Ok";
        if (ec)
          result.payloads.push_back(ec.message());
        else
          result.payloads.push_back(int64_t(removed ? 1 : 0));
        pushStack(result);
      } else {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(std::string("file.delete: expected one text path"));
        pushStack(result);
      }
      return;
    }
    if (fn_name == "file.move") {
      if (args.size() == 2 &&
          std::holds_alternative<std::string>(args[0]) &&
          std::holds_alternative<std::string>(args[1])) {
        std::error_code ec;
        fs::rename(std::get<std::string>(args[0]),
                   std::get<std::string>(args[1]), ec);
        EnumValue result;
        result.variant_name = ec ? "Result::Error" : "Result::Ok";
        if (ec)
          result.payloads.push_back(ec.message());
        else
          result.payloads.push_back(int64_t(1));
        pushStack(result);
      } else {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(
            std::string("file.move: expected source and destination paths"));
        pushStack(result);
      }
      return;
    }
    if (fn_name == "file.copy") {
      if (args.size() == 2 &&
          std::holds_alternative<std::string>(args[0]) &&
          std::holds_alternative<std::string>(args[1])) {
        std::error_code ec;
        const bool copied = fs::copy_file(
            std::get<std::string>(args[0]),
            std::get<std::string>(args[1]),
            fs::copy_options::overwrite_existing, ec);
        EnumValue result;
        result.variant_name = ec ? "Result::Error" : "Result::Ok";
        if (ec)
          result.payloads.push_back(ec.message());
        else
          result.payloads.push_back(int64_t(copied ? 1 : 0));
        pushStack(result);
      } else {
        EnumValue result;
        result.variant_name = "Result::Error";
        result.payloads.push_back(
            std::string("file.copy: expected source and destination paths"));
        pushStack(result);
      }
      return;
    }

    if (lib_name == "intrinsic" && fn_name.rfind("db2.", 0) == 0) {
      auto text_arg = [&](std::size_t i) -> const std::string & {
        if (i >= args.size() || !std::holds_alternative<std::string>(args[i]))
          throw std::runtime_error("database: expected text argument");
        return std::get<std::string>(args[i]);
      };
      auto word_arg = [&](std::size_t i) -> int64_t {
        if (i >= args.size() || !std::holds_alternative<int64_t>(args[i]))
          throw std::runtime_error("database: expected i64 argument");
        return std::get<int64_t>(args[i]);
      };
      auto arity = [&](std::size_t count) {
        if (args.size() != count) throw std::runtime_error("database: invalid argument count");
      };
      if (fn_name == "db2.available") {
        arity(1); pushStack(int64_t(database::available(text_arg(0)))); return;
      }
      database::Result result;
      bool text_result = false;
      if (fn_name == "db2.open") {
        arity(3); result = databases_.open(text_arg(0), text_arg(1), word_arg(2));
      } else if (fn_name == "db2.execute") {
        arity(5);
        auto array_arg = [&](std::size_t i, const char *kind) -> const HeapBlock & {
          const auto *handle = std::get_if<ArrayHandle>(&args[i]);
          if (!handle) throw std::runtime_error("database: expected typed array bindings");
          auto found = heap_.find(handle->address);
          if (found == heap_.end() || found->second.type_name != "array" ||
              found->second.element_type != kind ||
              found->second.data.size() != found->second.element_count)
            throw std::runtime_error("database: invalid binding array");
          return found->second;
        };
        const auto &values = array_arg(2, "text");
        const auto &flags = array_arg(3, "bool");
        if (values.element_count != flags.element_count || values.element_count > 256) {
          result.error = "database: parameter and NULL arrays must have equal lengths up to 256";
        } else {
          std::vector<std::string> parameters; std::vector<bool> nulls;
          for (const auto &value : values.data) {
            if (!std::holds_alternative<std::string>(value)) throw std::runtime_error("database: invalid text binding");
            parameters.push_back(std::get<std::string>(value));
          }
          for (const auto &value : flags.data) {
            if (!std::holds_alternative<int64_t>(value)) throw std::runtime_error("database: invalid NULL binding flag");
            nulls.push_back(std::get<int64_t>(value) != 0);
          }
          result = databases_.execute(word_arg(0), text_arg(1), parameters, nulls, word_arg(4));
        }
      } else {
        database::Read operation;
        int64_t row = 0, column = 0;
        if (fn_name == "db2.rows") operation = database::Read::rows;
        else if (fn_name == "db2.columns") operation = database::Read::columns;
        else if (fn_name == "db2.affected") operation = database::Read::affected;
        else if (fn_name == "db2.release") operation = database::Read::release;
        else if (fn_name == "db2.close") operation = database::Read::close;
        else if (fn_name == "db2.column_name") { operation = database::Read::column_name; text_result = true; }
        else if (fn_name == "db2.cell") { operation = database::Read::cell; text_result = true; }
        else if (fn_name == "db2.is_null") operation = database::Read::is_null;
        else throw std::runtime_error("database: unknown intrinsic");
        if (operation == database::Read::column_name) { arity(2); column = word_arg(1); }
        else if (operation == database::Read::cell || operation == database::Read::is_null) { arity(3); row = word_arg(1); column = word_arg(2); }
        else arity(1);
        result = databases_.read(operation, word_arg(0), row, column);
      }
      EnumValue value;
      value.variant_name = result ? "Result::Ok" : "Result::Error";
      if (!result) value.payloads.push_back(result.error);
      else if (text_result) value.payloads.push_back(result.text);
      else value.payloads.push_back(result.number);
      pushStack(value); return;
    }

    // --- PostgreSQL Database Intrinsics ---
#ifdef NY_HAS_POSTGRESQL
    if (lib_name == "intrinsic" && fn_name.size() > 3 &&
        fn_name.compare(0, 3, "db.") == 0) {
      // Static registries for connection/result handles
      static std::unordered_map<int64_t, PGconn *> db_conns;
      static std::unordered_map<int64_t, PGresult *> db_results;
      static int64_t db_next_handle = 1;

      // Extract handle from int64 or StructValue._handle
      auto get_handle = [&](const Value &v) -> int64_t {
        if (std::holds_alternative<int64_t>(v))
          return std::get<int64_t>(v);
        if (std::holds_alternative<StructValue>(v)) {
          const auto &sv = std::get<StructValue>(v);
          auto it = sv.fields().find("_handle");
          if (it != sv.fields().end())
            return it->second;
        }
        return 0;
      };

      // Push result::ok(int64)
      auto push_ok = [&](int64_t val) {
        EnumValue ev;
        ev.variant_name = "Result::Ok";
        ev.payloads.push_back(val);
        pushStack(ev);
      };

      // Push result::error(string)
      auto push_dberr = [&](const std::string &msg) {
        EnumValue ev;
        ev.variant_name = "Result::Error";
        ev.payloads.push_back(msg);
        pushStack(ev);
      };

      // Extract strings from a vec<text> StructValue
      auto extract_vec_text = [&](const Value &v) -> std::vector<std::string> {
        std::vector<std::string> result;
        if (!std::holds_alternative<StructValue>(v))
          return result;
        const auto &sv = std::get<StructValue>(v);
        auto it_data = sv.fields().find("data");
        auto it_array = sv.array_fields().find("data");
        auto it_len = sv.fields().find("len");
        if ((it_data == sv.fields().end() && it_array == sv.array_fields().end()) ||
            it_len == sv.fields().end())
          return result;
        int64_t len = it_len->second;
        int64_t data_handle = it_array != sv.array_fields().end()
                                  ? it_array->second.address : it_data->second;
        if (data_handle == 0 || len <= 0)
          return result;
        auto heap_it = heap_.find(data_handle);
        if (heap_it == heap_.end())
          return result;
        for (int64_t i = 0; i < len &&
             i < static_cast<int64_t>(heap_it->second.data.size()); ++i) {
          const auto &elem = heap_it->second.data[i];
          if (std::holds_alternative<std::string>(elem)) {
            result.push_back(std::get<std::string>(elem));
          } else if (std::holds_alternative<int64_t>(elem)) {
            // might be a pointer to a heap-allocated string block
            int64_t ptr = std::get<int64_t>(elem);
            auto sit = heap_.find(ptr);
            if (sit != heap_.end() && !sit->second.byte_data.empty())
              result.push_back(std::string(sit->second.byte_data.data()));
            else
              result.push_back("");
          } else {
            result.push_back("");
          }
        }
        return result;
      };

      if (fn_name == "db.connect") {
        std::string conninfo = args.empty() ? "" : get_str(args[0]);
        PGconn *conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
          std::string err = PQerrorMessage(conn);
          PQfinish(conn);
          push_dberr(err);
        } else {
          int64_t h = db_next_handle++;
          db_conns[h] = conn;
          push_ok(h);
        }
        return;
      }

      if (fn_name == "db.is_valid") {
        int64_t h = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_conns.find(h);
        bool valid = (it != db_conns.end()) &&
                     PQstatus(it->second) == CONNECTION_OK;
        pushStack((int64_t)(valid ? 1 : 0));
        return;
      }

      if (fn_name == "db.status_message") {
        int64_t h = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_conns.find(h);
        pushStack(std::string(it != db_conns.end()
                                  ? PQerrorMessage(it->second)
                                  : "invalid connection"));
        return;
      }

      if (fn_name == "db.close") {
        int64_t h = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_conns.find(h);
        if (it != db_conns.end()) {
          PQfinish(it->second);
          db_conns.erase(it);
        }
        pushStack((int64_t)0);
        return;
      }

      if (fn_name == "db.exec") {
        int64_t h =
            get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        std::string query = args.size() > 1 ? get_str(args[1]) : "";
        auto cit = db_conns.find(h);
        if (cit == db_conns.end()) {
          push_dberr("db.exec: invalid connection handle");
          return;
        }
        PGresult *res = PQexec(cit->second, query.c_str());
        ExecStatusType st = PQresultStatus(res);
        if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) {
          int64_t rh = db_next_handle++;
          db_results[rh] = res;
          push_ok(rh);
        } else {
          std::string err = PQresultErrorMessage(res);
          PQclear(res);
          push_dberr(err);
        }
        return;
      }

      if (fn_name == "db.exec_params") {
        int64_t h =
            get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        std::string query = args.size() > 1 ? get_str(args[1]) : "";
        std::vector<std::string> pstrs =
            args.size() > 2 ? extract_vec_text(args[2])
                            : std::vector<std::string>{};
        auto cit = db_conns.find(h);
        if (cit == db_conns.end()) {
          push_dberr("db.exec_params: invalid connection handle");
          return;
        }
        std::vector<const char *> pvals;
        pvals.reserve(pstrs.size());
        for (const auto &s : pstrs)
          pvals.push_back(s.c_str());
        PGresult *res =
            PQexecParams(cit->second, query.c_str(), (int)pvals.size(),
                         nullptr, pvals.empty() ? nullptr : pvals.data(),
                         nullptr, nullptr, 0);
        ExecStatusType st = PQresultStatus(res);
        if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) {
          int64_t rh = db_next_handle++;
          db_results[rh] = res;
          push_ok(rh);
        } else {
          std::string err = PQresultErrorMessage(res);
          PQclear(res);
          push_dberr(err);
        }
        return;
      }

      if (fn_name == "db.ntuples") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        pushStack((int64_t)(it != db_results.end()
                                ? PQntuples(it->second)
                                : 0));
        return;
      }

      if (fn_name == "db.nfields") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        pushStack((int64_t)(it != db_results.end()
                                ? PQnfields(it->second)
                                : 0));
        return;
      }

      if (fn_name == "db.fname") {
        int64_t rh = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        int col = args.size() > 1 && std::holds_alternative<int64_t>(args[1])
                      ? (int)std::get<int64_t>(args[1])
                      : 0;
        auto it = db_results.find(rh);
        if (it != db_results.end()) {
          const char *name = PQfname(it->second, col);
          pushStack(std::string(name ? name : ""));
        } else {
          pushStack(std::string(""));
        }
        return;
      }

      if (fn_name == "db.ftype") {
        int64_t rh = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        int col = args.size() > 1 && std::holds_alternative<int64_t>(args[1])
                      ? (int)std::get<int64_t>(args[1])
                      : 0;
        auto it = db_results.find(rh);
        pushStack((int64_t)(it != db_results.end()
                                ? (int64_t)PQftype(it->second, col)
                                : 0));
        return;
      }

      if (fn_name == "db.get_value") {
        int64_t rh = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        int row = args.size() > 1 && std::holds_alternative<int64_t>(args[1])
                      ? (int)std::get<int64_t>(args[1])
                      : 0;
        int col = args.size() > 2 && std::holds_alternative<int64_t>(args[2])
                      ? (int)std::get<int64_t>(args[2])
                      : 0;
        auto it = db_results.find(rh);
        if (it != db_results.end()) {
          if (PQgetisnull(it->second, row, col))
            pushStack(std::string(""));
          else
            pushStack(std::string(PQgetvalue(it->second, row, col)));
        } else {
          pushStack(std::string(""));
        }
        return;
      }

      if (fn_name == "db.get_is_null") {
        int64_t rh = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        int row = args.size() > 1 && std::holds_alternative<int64_t>(args[1])
                      ? (int)std::get<int64_t>(args[1])
                      : 0;
        int col = args.size() > 2 && std::holds_alternative<int64_t>(args[2])
                      ? (int)std::get<int64_t>(args[2])
                      : 0;
        auto it = db_results.find(rh);
        pushStack((int64_t)(it != db_results.end()
                                ? (PQgetisnull(it->second, row, col) ? 1 : 0)
                                : 1));
        return;
      }

      if (fn_name == "db.affected_rows") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        int64_t count = 0;
        if (it != db_results.end()) {
          const char *s = PQcmdTuples(it->second);
          if (s && s[0] != '\0') {
            try { count = std::stoll(s); } catch (...) {}
          }
        }
        pushStack(count);
        return;
      }

      if (fn_name == "db.res_status") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        pushStack(std::string(it != db_results.end()
                                  ? PQresStatus(PQresultStatus(it->second))
                                  : "INVALID"));
        return;
      }

      if (fn_name == "db.error_message") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        pushStack(std::string(it != db_results.end()
                                  ? PQresultErrorMessage(it->second)
                                  : "invalid result handle"));
        return;
      }

      if (fn_name == "db.clear") {
        int64_t rh = get_handle(args.empty() ? Value((int64_t)0) : args[0]);
        auto it = db_results.find(rh);
        if (it != db_results.end()) {
          PQclear(it->second);
          db_results.erase(it);
        }
        pushStack((int64_t)0);
        return;
      }

      if (fn_name == "db.escape_literal") {
        int64_t h = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        std::string s = args.size() > 1 ? get_str(args[1]) : "";
        auto cit = db_conns.find(h);
        if (cit != db_conns.end()) {
          char *esc = PQescapeLiteral(cit->second, s.c_str(), s.size());
          std::string r = esc ? std::string(esc) : "''";
          if (esc) PQfreemem(esc);
          pushStack(r);
        } else {
          pushStack(std::string("''"));
        }
        return;
      }

      if (fn_name == "db.escape_identifier") {
        int64_t h = get_handle(args.size() > 0 ? args[0] : Value((int64_t)0));
        std::string s = args.size() > 1 ? get_str(args[1]) : "";
        auto cit = db_conns.find(h);
        if (cit != db_conns.end()) {
          char *esc = PQescapeIdentifier(cit->second, s.c_str(), s.size());
          std::string r = esc ? std::string(esc) : "\"\"";
          if (esc) PQfreemem(esc);
          pushStack(r);
        } else {
          pushStack(std::string("\"\""));
        }
        return;
      }

      std::cerr << "[DB] Unknown db intrinsic: " << fn_name << std::endl;
      pushStack((int64_t)0);
      return;
    }
#endif // NY_HAS_POSTGRESQL

    // Legacy Fallback (vm__*)
    // We already popped args! Legacy code below EXPECTS args on VM stack
    // relative to something? No, legacy code below (lines 334+) constructs
    // `vm_args` AGAIN by popping stack. IF I popped stack above, legacy code
    // will fail (empty stack). So if NOT handled above, I must RE-PUSH args or
    // adjust legacy code?

    // ADJUSTMENT: The legacy code below (which I didn't delete yet) handles
    // `std_env`. It does: `std::vector<Value> vm_args; for(...) push(pop())`.
    // Since I ALREADY popped, I should inject my logic BEFORE popping?
    // OR I should start a new block?

    // Strategy: My intrinsics RETURN.
    // If we fall through, we must RESTORE stack?
    // Or simpler: Don't pop until I know it's my function?
    // But `dir.exists` needs args.

    // Better: Since the legacy code is localized in the `std_env` block below,
    // I can just replace the logic inside `INTEROP_CALL` to use my `args`
    // vector for EVERYTHING and delete the legacy arg parsing?

    // Let's look at legacy code.
    // It is `if (lib_name == "std_env") { ... std::vector<Value> vm_args; ...
    // }` If I use my `args` vector, I can just replace `vm_args` with `args`.

    // So:
    // 1. Parse args into `args`.
    // 2. My intrinsics (use `args`).
    // 3. Fallthrough to Legacy (std_env).
    // 4. Update legacy code to use `args` instead of popping again.

    // BUT replace_file_content logic needs to match exact block.
    // I am targeting lines 326-334 + ...
    // Internal Modules
    if (lib_name == "std_env") {
      // Use args already collected above — do NOT re-pop from stack.
      std::vector<Value> vm_args = args;

      if (fn_name == "vm__alloc_bytes") {
        // arg: size (int)
        // ret: handle (access to byte_data)
        if (vm_args.size() != 1 ||
            !std::holds_alternative<int64_t>(vm_args[0])) {
          pushStack((int64_t)0);
          return;
        }
        int64_t size = std::get<int64_t>(vm_args[0]);
        int64_t handle = ++next_ptr_;

        HeapBlock block;
        block.type_name = "bytes"; // Special type
        block.element_count = size;
        block.byte_data.resize(size);
        std::memset(block.byte_data.data(), 0, size);

        storeHeapBlock(handle, std::move(block));
        pushStack(handle);
        return;
      }

      if (fn_name == "vm__write_bytes") {
        // args: handle, offset, data (string)
        if (vm_args.size() != 3) {
          pushStack((int64_t)0);
          return;
        }
        // Extract args
        // Arg order in vm_args is reversed? No, vm_args is [arg0, arg1...]
        int64_t handle = 0;
        int64_t offset = 0;
        std::string data;

        if (std::holds_alternative<int64_t>(vm_args[0]))
          handle = std::get<int64_t>(vm_args[0]);
        if (std::holds_alternative<int64_t>(vm_args[1]))
          offset = std::get<int64_t>(vm_args[1]);
        if (std::holds_alternative<std::string>(vm_args[2]))
          data = std::get<std::string>(vm_args[2]);

        auto it = heap_.find(handle);
        if (it != heap_.end()) {
          if (offset >= 0 &&
              offset + data.size() <= it->second.byte_data.size()) {
            std::memcpy(it->second.byte_data.data() + offset, data.c_str(),
                        data.size());
            pushStack((int64_t)1); // Success
          } else {
            pushStack((int64_t)0); // Out of bounds
          }
        } else {
          pushStack((int64_t)0); // Invalid handle
        }
        return;
      }

      if (fn_name == "vm__get_address") {
        if (vm_args.size() != 1) {
          pushStack((int64_t)0);
          return;
        }
        Value v = vm_args[0];
        int64_t result = 0;
        if (std::holds_alternative<int64_t>(v) || std::holds_alternative<ArrayHandle>(v)) {
          const auto *array = std::get_if<ArrayHandle>(&v);
          int64_t handle = array ? array->address : std::get<int64_t>(v);
          if (handle != 0) {
            auto it = heap_.find(handle);
            if (it != heap_.end() && (array || it->second.type_name != "array")) {
              // Check for byte_data first (strings/bytes)
              if (!it->second.byte_data.empty()) {
                result = reinterpret_cast<int64_t>(it->second.byte_data.data());
              } else {
                result = reinterpret_cast<int64_t>(it->second.data.data());
              }
            } else {
              result = handle;
            }
          }
        }
        pushStack(result);
        return;
      }

      if (fn_name == "vm__get_cstr") {
        // Convert a Ny text value to a null-terminated C-string pointer
        // Returns a heap pointer to the string data
        if (vm_args.size() != 1) {
          pushStack((int64_t)0);
          return;
        }
        Value v = vm_args[0];
        int64_t result = 0;

        if (std::holds_alternative<std::string>(v)) {
          std::string s = std::get<std::string>(v);
          size_t bytes = s.size() + 1; // +1 for null terminator

          // Allocate heap block for the C-string
          int64_t handle = ++next_ptr_;
          HeapBlock block;
          block.type_name = "cstr";
          block.element_count = bytes;
          block.byte_data.resize(bytes);
          std::memcpy(block.byte_data.data(), s.c_str(), bytes);
          storeHeapBlock(handle, std::move(block));

          // Return pointer to the data
          result = reinterpret_cast<int64_t>(heap_[handle].byte_data.data());
        } else if (std::holds_alternative<int64_t>(v)) {
          // Might be a heap handle to an existing string
          int64_t handle = std::get<int64_t>(v);
          if (handle != 0) {
            auto it = heap_.find(handle);
            if (it != heap_.end() && !it->second.byte_data.empty()) {
              result = reinterpret_cast<int64_t>(it->second.byte_data.data());
            }
          }
        }
        pushStack(result);
        return;
      }
      if (fn_name == "get_arg_count") {
        pushStack((int64_t)args_.size());
        return;
      } else if (fn_name == "get_arg") {
        Value idx_val = vm_args[0]; // Already popped and reversed
        if (std::holds_alternative<int64_t>(idx_val)) {
          int64_t idx = std::get<int64_t>(idx_val);
          if (idx >= 0 && idx < (int64_t)args_.size()) {
            pushStack(args_[idx]);
          } else {
            pushStack(std::string("")); // out of bounds
          }
        } else {
          pushStack(std::string(""));
        }
        return;
      }
    }

    // First, allow engine-registered NyForeignFunc globals to intercept the
    // interop call. This enables engine modules (like the Ny Godot bridge)
    // to register adapter functions as globals rather than relying on FFI.
    auto git = globals_.find(fn_name);
    if (git != globals_.end()) {
      const Value &gv = git->second;
      if (std::holds_alternative<ny::NyForeignFunc>(gv)) {
        const ny::NyForeignFunc &ff = std::get<ny::NyForeignFunc>(gv);
        try {
          Value ret = callNyForeignFunc(ff, args, this);
          pushStack(ret);
          return;
        } catch (...) {
          // If the foreign function failed, fall through to legacy FFI
        }
      }
    }

    // 1. Determine Return Type
    std::string ret_type = (instr.args.size() > 3) ? instr.args[3] : "i64";

    // 2. Resolve Native Function Pointer
    std::string actual_lib = lib_name;
    std::string actual_fn = fn_name;

    void *fn_ptr = ffi::get_function(actual_lib, actual_fn);
    if (!fn_ptr) {
      throw std::runtime_error("INTEROP_CALL: Failed to resolve " + actual_lib +
                               "::" + fn_name);
    }

    // 3. Prepare Arguments - use args already populated at line 371
    // (Previously this was popping again which caused stack underflow)
    std::vector<Value> vm_args = args;

    std::vector<int64_t> native_args;

    // Helper to resolve VM handles to native pointers
    auto resolve = [&](int64_t val, bool array_identity = false) -> int64_t {
      if (val == 0)
        return 0;
      auto it = heap_.find(val);
      if (it != heap_.end()) {
        // Raw-pointer compatibility is separate from semantic array identity.
        // An ordinary integer cannot designate a live array by coincidence.
        if (!array_identity && it->second.type_name == "array")
          return val;
        // Prefer byte_data if available (e.g. strings)
        if (!it->second.byte_data.empty()) {
          return reinterpret_cast<int64_t>(it->second.byte_data.data());
        }
        // Return pointer to int64_t array (HeapBlock.data is vector<int64_t>)
        return reinterpret_cast<int64_t>(it->second.data.data());
      }
      return val;
    };

    // 4. Handle Large Struct Return (Hidden Pointer ABI)
    bool hidden_ptr_ret = false;
    int64_t hidden_handle = 0;

    if (ret_type.rfind("struct:", 0) == 0) {
      int size = std::stoi(ret_type.substr(7));
      if (size > 8) {
        hidden_ptr_ret = true;
        // Allocate buffer for return value
        hidden_handle = ++next_ptr_; // Use the VM's next pointer tracker

        HeapBlock block;
        block.type_name = "struct_bytes";
        block.element_count = size;
        block.byte_data.resize(size);
        // Zero initialize
        std::memset(block.byte_data.data(), 0, size);

        storeHeapBlock(hidden_handle, std::move(block));

        // Pass pointer as first argument (RCX on Windows x64)
        native_args.push_back(
            reinterpret_cast<int64_t>(heap_[hidden_handle].byte_data.data()));
      }
    }

    // 5. Convert VM Args to Native Args
    // Keep string copies alive until after the call
    std::vector<std::string> string_copies;
    string_copies.reserve(vm_args.size());
    int type_mask = 0;

    // Parse parameter types from the NIL annotation if available (arg 4)
    std::vector<std::string> param_types;
    if (instr.args.size() > 4) {
      // Parse comma-separated types: "f32,i32,f64,i64" etc
      std::string types_str = instr.args[4];
      std::istringstream iss(types_str);
      std::string type;
      while (std::getline(iss, type, ',')) {
        param_types.push_back(type);
      }
    }

    for (size_t i = 0; i < vm_args.size(); ++i) {
      const auto &arg = vm_args[i];

      // Determine if this arg should use float register based on type
      // annotation
      bool is_float_param = false;
      if (i < param_types.size()) {
        const std::string &pt = param_types[i];
        is_float_param =
            (pt == "f32" || pt == "f64" || pt == "float" || pt == "double");
      }

      if (std::holds_alternative<int64_t>(arg)) {
        int64_t val = std::get<int64_t>(arg);
        // If annotated as float, interpret the int64 as float bits
        if (is_float_param && i < 4) {
          // The value might be an int that needs conversion to float
          // For now, treat as-is but mark for XMM register
          type_mask |= (1 << i);
        }
        native_args.push_back(resolve(val));
      } else if (const auto *array = std::get_if<ArrayHandle>(&arg)) {
        native_args.push_back(resolve(array->address, true));
      } else if (std::holds_alternative<std::string>(arg)) {
        string_copies.push_back(std::get<std::string>(arg));
        native_args.push_back((int64_t)string_copies.back().c_str());
      } else if (std::holds_alternative<double>(arg)) {
        double d = std::get<double>(arg);
        int64_t bits;
        std::memcpy(&bits, &d, 8);
        native_args.push_back(bits);
        // Use type annotation to decide register - if f32, still use XMM but
        // value is double
        if (i < 4)
          type_mask |= (1 << i);
      } else {
        std::cerr << "[nilinterpreter:ffi] argument " << i << " has an unknown type\n";
        native_args.push_back(0); // Unknown type?
      }
    }

    // Pad to 12 arguments to satisfy the "God Cast" signature safely
    while (native_args.size() < 12)
      native_args.push_back(0);

    // 6. Invoke Native Function with correct register dispatch
    int64_t result = 0;
    switch (type_mask & 0xF) { // Limit to 4 bits
    case 0:
      result = invoke_native<0>(fn_ptr, native_args);
      break;
    case 1:
      result = invoke_native<1>(fn_ptr, native_args);
      break;
    case 2:
      result = invoke_native<2>(fn_ptr, native_args);
      break;
    case 3:
      result = invoke_native<3>(fn_ptr, native_args);
      break;
    case 4:
      result = invoke_native<4>(fn_ptr, native_args);
      break;
    case 5:
      result = invoke_native<5>(fn_ptr, native_args);
      break;
    case 6:
      result = invoke_native<6>(fn_ptr, native_args);
      break;
    case 7:
      result = invoke_native<7>(fn_ptr, native_args);
      break;
    case 8:
      result = invoke_native<8>(fn_ptr, native_args);
      break;
    case 9:
      result = invoke_native<9>(fn_ptr, native_args);
      break;
    case 10:
      result = invoke_native<10>(fn_ptr, native_args);
      break;
    case 11:
      result = invoke_native<11>(fn_ptr, native_args);
      break;
    case 12:
      result = invoke_native<12>(fn_ptr, native_args);
      break;
    case 13:
      result = invoke_native<13>(fn_ptr, native_args);
      break;
    case 14:
      result = invoke_native<14>(fn_ptr, native_args);
      break;
    case 15:
      result = invoke_native<15>(fn_ptr, native_args);
      break;
    }

    // 7. Handle Return Value
    if (hidden_ptr_ret) {
      // If it's a known struct, unpack it into a StructValue.
      if (ret_type.find("core::process::process_result") != std::string::npos) {
          StructValue sv;
          sv.type_name = "core::process::process_result";
          const char* base = reinterpret_cast<const char*>(heap_[hidden_handle].byte_data.data());

          // Layout: stdout (8 bytes), stderr (8 bytes), exit_code (4 bytes)
          const char* stdout_ptr = *reinterpret_cast<const char* const*>(base);
          const char* stderr_ptr = *reinterpret_cast<const char* const*>(base + 8);
          int32_t exit_code = *reinterpret_cast<const int32_t*>(base + 16);

          sv.string_fields()["stdout"] = stdout_ptr ? std::string(stdout_ptr) : "";
          sv.string_fields()["stderr"] = stderr_ptr ? std::string(stderr_ptr) : "";
          sv.fields()["exit_code"] = (int64_t)exit_code;

          // Free the C strings allocated by strdup in FFI
          if (stdout_ptr) std::free((void*)stdout_ptr);
          if (stderr_ptr) std::free((void*)stderr_ptr);

          pushStack(sv);
      } else {
          // Return validity handle to the struct buffer
          pushStack(hidden_handle);
      }
    } else if (ret_type == "text") {
      // The native ABI returned const char*; copy it into a Ny string.
      const char *cstr = reinterpret_cast<const char *>(result);
      pushStack(cstr ? std::string(cstr) : std::string(""));
    } else if (ret_type != "void") {
      pushStack(result); // Return register value
    }
    // If void, push nothing.
    } while (false);
    };
    dispatch_interop();
    // Explicit process termination has no return result. Three-operand legacy
    // interop has no declared result contract and keeps historical behavior.
    if (requested_exit_code_ || instr.args.size() < 4)
      break;
    if (stack_.size() < result_base || stack_.size() > result_base + 1 ||
        (!returns_void && stack_.size() != result_base + 1))
      throw std::runtime_error("invalid INTEROP_CALL result stack for " +
                               lib_name + "::" + fn_name);
    if (returns_void)
      stack_.resize(result_base);
    else if (auto *result = std::get_if<EnumValue>(&stack_.back());
             result && result->type_name.empty()) {
      result->type_name = canonical_array_type(instr.args[3]);
      // Older intrinsic wrappers encoded all enum results using their i64
      // storage representation. Only the matching annotated wrapper may
      // supply its declared concrete enum identity for that legacy encoding.
      if (lib_name == "intrinsic" && result->type_name == "i64" &&
          class_metadata_ && !call_stack_.empty()) {
        const auto &frame = call_stack_.back();
        if (frame.entry_pc < program_.size()) {
          const auto &annotations = program_[frame.entry_pc].function_annotations;
          const auto annotation = "@intrinsic(\"" + fn_name + "\")";
          if (std::find(annotations.begin(), annotations.end(), annotation) != annotations.end() &&
              resolve_storage_schema(class_metadata_->enums, frame.return_type))
            result->type_name = canonical_array_type(frame.return_type);
        }
      }
    }
  } break;

  // Math Opcodes
  case Opcode::MATH_SIN: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::sin(std::get<double>(v)));
    else if (std::holds_alternative<int64_t>(v))
      pushStack(std::sin((double)std::get<int64_t>(v)));
    else
      throw std::runtime_error("MATH_SIN expects number");
    break;
  }
  case Opcode::MATH_COS: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::cos(std::get<double>(v)));
    else if (std::holds_alternative<int64_t>(v))
      pushStack(std::cos((double)std::get<int64_t>(v)));
    else
      throw std::runtime_error("MATH_COS expects number");
    break;
  }
  case Opcode::MATH_TAN: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::tan(std::get<double>(v)));
    else if (std::holds_alternative<int64_t>(v))
      pushStack(std::tan((double)std::get<int64_t>(v)));
    else
      throw std::runtime_error("MATH_TAN expects number");
    break;
  }
  case Opcode::MATH_ASIN: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::asin(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_ASIN expects number");
    break;
  }
  case Opcode::MATH_ACOS: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::acos(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_ACOS expects number");
    break;
  }
  case Opcode::MATH_ATAN: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::atan(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_ATAN expects number");
    break;
  }
  case Opcode::MATH_ATAN2: {
    Value x = popStack();
    Value y = popStack();
    double dx = std::holds_alternative<double>(x)
                    ? std::get<double>(x)
                    : (double)std::get<int64_t>(x);
    double dy = std::holds_alternative<double>(y)
                    ? std::get<double>(y)
                    : (double)std::get<int64_t>(y);
    pushStack(std::atan2(dy, dx));
    break;
  }
  case Opcode::MATH_SQRT: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::sqrt(std::get<double>(v)));
    else if (std::holds_alternative<int64_t>(v))
      pushStack(std::sqrt((double)std::get<int64_t>(v)));
    else
      throw std::runtime_error("MATH_SQRT expects number");
    break;
  }
  case Opcode::MATH_POW: {
    Value y = popStack();
    Value x = popStack();
    double dy = std::holds_alternative<double>(y)
                    ? std::get<double>(y)
                    : (double)std::get<int64_t>(y);
    double dx = std::holds_alternative<double>(x)
                    ? std::get<double>(x)
                    : (double)std::get<int64_t>(x);
    pushStack(std::pow(dx, dy));
    break;
  }
  case Opcode::MATH_EXP: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::exp(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_EXP expects number");
    break;
  }
  case Opcode::MATH_LOG: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::log(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_LOG expects number");
    break;
  }
  case Opcode::MATH_LOG10: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::log10(std::get<double>(v)));
    else
      throw std::runtime_error("MATH_LOG10 expects number");
    break;
  }
  case Opcode::MATH_ABS: {
    Value v = popStack();
    if (std::holds_alternative<double>(v))
      pushStack(std::abs(std::get<double>(v)));
    else if (std::holds_alternative<int64_t>(v))
      pushStack(std::abs(std::get<int64_t>(v)));
    else
      throw std::runtime_error("MATH_ABS expects number");
    break;
  }
  case Opcode::MATH_FLOOR: {
    Value v = popStack();
    double d = std::holds_alternative<double>(v) ? std::get<double>(v)
                                                 : (double)std::get<int64_t>(v);
    pushStack(std::floor(d));
    break;
  }
  case Opcode::MATH_CEIL: {
    Value v = popStack();
    double d = std::holds_alternative<double>(v) ? std::get<double>(v)
                                                 : (double)std::get<int64_t>(v);
    pushStack(std::ceil(d));
    break;
  }
  case Opcode::MATH_ROUND: {
    Value v = popStack();
    double d = std::holds_alternative<double>(v) ? std::get<double>(v)
                                                 : (double)std::get<int64_t>(v);
    pushStack(std::round(d));
    break;
  }
  case Opcode::MATH_TRUNC: {
    Value v = popStack();
    double d = std::holds_alternative<double>(v) ? std::get<double>(v)
                                                 : (double)std::get<int64_t>(v);
    pushStack(std::trunc(d));
    break;
  }
  case Opcode::MATH_CBRT: {
    Value v = popStack();
    double d = std::holds_alternative<double>(v) ? std::get<double>(v)
                                                 : (double)std::get<int64_t>(v);
    pushStack(std::cbrt(d));
    break;
  }
  case Opcode::MATH_MIN: {
    Value b = popStack();
    Value a = popStack();
    double da = std::holds_alternative<double>(a)
                    ? std::get<double>(a)
                    : (double)std::get<int64_t>(a);
    double db = std::holds_alternative<double>(b)
                    ? std::get<double>(b)
                    : (double)std::get<int64_t>(b);
    pushStack(std::fmin(da, db));
    break;
  }
  case Opcode::MATH_MAX: {
    Value b = popStack();
    Value a = popStack();
    double da = std::holds_alternative<double>(a)
                    ? std::get<double>(a)
                    : (double)std::get<int64_t>(a);
    double db = std::holds_alternative<double>(b)
                    ? std::get<double>(b)
                    : (double)std::get<int64_t>(b);
    pushStack(std::fmax(da, db));
    break;
  }

  // Console Intrinsics
  case Opcode::CONSOLE_WRITE: {
    auto &output = outputStream();
    Value v = popStack();
    if (std::holds_alternative<std::string>(v)) {
      output << std::get<std::string>(v);
    } else if (std::holds_alternative<int64_t>(v)) {
      output << std::get<int64_t>(v);
    } else if (std::holds_alternative<double>(v)) {
      output << std::get<double>(v);
    }
    output.flush();
    break;
  }
  case Opcode::CONSOLE_WRITELN: {
    auto &output = outputStream();
    Value v = popStack();
    if (std::holds_alternative<std::string>(v)) {
      output << std::get<std::string>(v) << std::endl;
    } else if (std::holds_alternative<int64_t>(v)) {
      output << std::get<int64_t>(v) << std::endl;
    } else if (std::holds_alternative<double>(v)) {
      output << std::get<double>(v) << std::endl;
    }
    break;
  }

  // Time Intrinsics
  case Opcode::TIME_NOW: {
    pushStack(time_support::now_ms());
    break;
  }
  case Opcode::TIME_SLEEP: {
    Value v = popStack();
    if (!std::holds_alternative<int64_t>(v))
      throw std::runtime_error("TIME_SLEEP: expected an integer duration");
    time_support::sleep_ms(std::get<int64_t>(v));
    break;
  }

  case Opcode::CMP_EQ: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_EQ: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left == right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a == numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) == std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_EQ: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::CMP_NE: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_NE: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left != right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a != numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) != std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_NE: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::CMP_LT: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_LT: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left < right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a < numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) < std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_LT: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::CMP_LE: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_LE: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left <= right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a <= numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) <= std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_LE: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::CMP_GT: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_GT: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left > right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a > numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) > std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_GT: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::CMP_GE: {
    Value b = popStack();
    Value a = popStack();
    if (is_array_value(a) || is_array_value(b))
      throw std::runtime_error("CMP_GE: comparison is not defined for array values");
    bool res = false;
    double numeric_a = 0.0;
    double numeric_b = 0.0;
    if (std::holds_alternative<int64_t>(a) &&
        std::holds_alternative<int64_t>(b)) {
      const std::int64_t left = std::get<int64_t>(a);
      const std::int64_t right = std::get<int64_t>(b);
      res = left >= right;
    } else if (numeric_value(a, numeric_a) && numeric_value(b, numeric_b)) {
      res = numeric_a >= numeric_b;
    } else if (std::holds_alternative<std::string>(a) &&
               std::holds_alternative<std::string>(b)) {
      res = std::get<std::string>(a) >= std::get<std::string>(b);
    } else {
      throw std::runtime_error(
          "CMP_GE: comparison is not defined for these runtime values");
    }
    pushStack(res ? 1 : 0);
  } break;
  case Opcode::JMP_IF_NOT: {
    Value cond = popStack();
    if (std::holds_alternative<int64_t>(cond) && std::get<int64_t>(cond) == 0) {
      pc_ = labels_.at(instr.args[0]) - 1; // since ++pc_ after
    }
  } break;
  case Opcode::JMP_IF: {
    Value cond = popStack();
    if (std::holds_alternative<int64_t>(cond) && std::get<int64_t>(cond) != 0) {
      pc_ = labels_.at(instr.args[0]) - 1;
    }
  } break;
  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::MUL:
  case Opcode::DIV:
  case Opcode::MOD:
  case Opcode::NEG:
  case Opcode::BIT_AND:
  case Opcode::BIT_OR:
  case Opcode::BIT_XOR:
  case Opcode::SHL:
  case Opcode::SHR: {
    Value b = instr.op == Opcode::NEG ? Value(int64_t(0)) : popStack();
    Value a = popStack();
    // Only untyped historical ADD can denote array concatenation. Typed
    // integer arithmetic must never mistake an integer for a heap handle.
    if (instr.op == Opcode::ADD && instr.args.empty() &&
        std::holds_alternative<ArrayHandle>(a) &&
        std::holds_alternative<ArrayHandle>(b)) {
      const auto left = heap_.find(std::get<ArrayHandle>(a).address);
      const auto right = heap_.find(std::get<ArrayHandle>(b).address);
      if (left != heap_.end() && right != heap_.end() &&
          left->second.type_name == "array" && right->second.type_name == "array") {
        HeapBlock result;
        result.type_name = "array";
        result.element_type = left->second.element_type == right->second.element_type
                                  ? left->second.element_type : std::string{};
        result.data = left->second.data;
        result.data.insert(result.data.end(), right->second.data.begin(),
                           right->second.data.end());
        result.element_count = result.data.size();
        const auto pointer = next_ptr_;
        next_ptr_ += result.element_count == 0 ? 8 : result.element_count * 8;
        storeHeapBlock(pointer, std::move(result));
        pushStack(ArrayHandle{pointer});
        break;
      }
    }
    pushStack(scalar_arithmetic(instr, a, b));
  } break;
  case Opcode::JMP:
    pc_ = labels_.at(instr.args[0]) - 1;
    break;
  case Opcode::LABEL:
    if (instr.is_function_end && !call_stack_.empty() &&
        !call_stack_.back().return_type.empty())
      throw std::runtime_error("function '" + call_stack_.back().fn_name +
                               "' falls through without RET");
    break;
  // Arithmetic
  case Opcode::PTR_ADD:
  case Opcode::PTR_SUB: {
    Value offset_value = popStack();
    Value pointer_value = popStack();
    if (!std::holds_alternative<int64_t>(pointer_value) ||
        !std::holds_alternative<int64_t>(offset_value)) {
      throw std::runtime_error(
          instr.op == Opcode::PTR_ADD
              ? "PTR_ADD requires an integer pointer and offset"
              : "PTR_SUB requires an integer pointer and offset");
    }

    const auto pointer = std::get<int64_t>(pointer_value);
    const auto offset = std::get<int64_t>(offset_value);
    const auto location = findHeapPointer(pointer, true);
    if (!location) {
      pushStack(int64_t(0));
      break;
    }

    const auto moved =
        moveHeapPointer(*location, offset, instr.op == Opcode::PTR_SUB);
    pushStack(moved.value_or(0));
  } break;
  case Opcode::NOT: {
    Value a = popStack();
    if (std::holds_alternative<int64_t>(a)) {
      pushStack(std::get<int64_t>(a) == 0 ? (int64_t)1 : (int64_t)0);
    } else {
      throw std::runtime_error("NOT requires bool/int operand");
    }
  } break;
  case Opcode::REF_LOCAL: {
    // REF_LOCAL var_name - Create a reference to a local variable
    const std::string var_name = instr.args.empty() ? "" : instr.args[0];
    // Resolve like LOAD_LOCAL: lexical local or module global, never a local
    // in an unrelated caller frame. This retains existing shared StructValue
    // behavior; primitive references are still value copies, not typed loans.
    if (!hasLocal(var_name) && !hasGlobal(var_name))
      throw std::runtime_error(
          "REF_LOCAL: variable not found in current frame or globals: " +
          var_name);
    auto value = getLocal(var_name);
    if (auto *object = std::get_if<StructValue>(&value); object && class_metadata_ &&
        find_nil_class(*class_metadata_, object->static_class_view))
      object->static_class_view = "ref<" + object->static_class_view + ">";
    pushStack(value);
  } break;
  case Opcode::PUSH_FN_PTR:
    // Stub: push function name as string
    pushStack(instr.args[0]);
    break;
  // Struct operations
  case Opcode::NEW_STRUCT: {
    StructValue sv;
    sv.type_name = instr.args[0];
    if (class_metadata_) if (const auto *klass = find_nil_class(*class_metadata_, sv.type_name)) {
      if (klass->is_abstract) throw std::runtime_error("NEW_STRUCT cannot construct an abstract class");
      sv.static_class_view = sv.type_name;
    }
    pushStack(sv);
  } break;
  case Opcode::GET_FIELD: {
    std::string field_name = instr.args[0];
    Value sv = popStack();
    if (std::holds_alternative<StructValue>(sv)) {
      auto &s = std::get<StructValue>(sv);
      (void)classFieldType(s, field_name);
      if (const auto array = s.array_fields().find(field_name);
          array != s.array_fields().end()) {
        pushStack(array->second);
        break;
      }
      // Check local int fields
      auto it = s.fields().find(field_name);
      if (it != s.fields().end()) {
        pushStack(it->second);
      } else {
        // Check string fields
        auto it2 = s.string_fields().find(field_name);
        if (it2 != s.string_fields().end()) {
          pushStack(it2->second);
        } else {
          // Check f64 fields
          auto it3 = s.double_fields().find(field_name);
          if (it3 != s.double_fields().end()) {
            pushStack(it3->second);
          } else {
            // Check pointer fields
            auto it4 = s.struct_fields().find(field_name);
            if (it4 != s.struct_fields().end()) {
              pushStack(*(it4->second));
            } else {
              // Check enum fields (e.g. option<T>, result<T,E> stored in struct)
              auto it5 = s.enum_fields().find(field_name);
              if (it5 != s.enum_fields().end()) {
                pushStack(it5->second);
              } else {
                pushStack((int64_t)0); // default value
              }
            }
          }
        }
      }
    } else {
      // Debug: print call stack and stack state before throwing
      std::cerr << "[DEBUG] GET_FIELD failed. Field: " << field_name
                << ", Type Index: " << sv.index() << "\n";
      std::cerr << "[DEBUG] Call stack (" << call_stack_.size() << " frames):\n";
      for (int _ci = (int)call_stack_.size() - 1; _ci >= 0; --_ci) {
        std::cerr << "  [" << _ci << "] " << call_stack_[_ci].fn_name << "\n";
      }
      std::cerr << "[DEBUG] Stack size: " << stack_.size() << ". Top 15 values:\n";
      int _show = (int)stack_.size() < 15 ? (int)stack_.size() : 15;
      for (int _si = (int)stack_.size() - 1; _si >= (int)stack_.size() - _show; --_si) {
        const Value &_v = stack_[_si];
        std::cerr << "  [" << _si << "] type=" << _v.index();
        if (std::holds_alternative<int64_t>(_v)) std::cerr << " val=" << std::get<int64_t>(_v);
        else if (std::holds_alternative<std::string>(_v)) std::cerr << " val=\"" << std::get<std::string>(_v) << "\"";
        else if (std::holds_alternative<StructValue>(_v)) std::cerr << " (StructValue:" << std::get<StructValue>(_v).type_name << ")";
        std::cerr << "\n";
      }
      throw std::runtime_error(
          "GET_FIELD on non-struct. Type Index: " + std::to_string(sv.index()) +
          ", Field: " + field_name);
    }
  } break;
  case Opcode::SET_FIELD: {
    std::string field_name = instr.args[0];
    Value val = popStack();
    if (availableStackValues() == 0)
      throw std::runtime_error("SET_FIELD: empty stack");
    Value &sv_ref = stack_.back();

    if (std::holds_alternative<StructValue>(sv_ref)) {
      auto &s = std::get<StructValue>(sv_ref);
      const auto field_type = classFieldType(s, field_name);
      if (!field_type.empty()) validateClassValue(val, field_type);
      if (!std::holds_alternative<ArrayHandle>(val))
        s.array_fields().erase(field_name);

      if (const auto *array = std::get_if<ArrayHandle>(&val)) {
        s.fields().erase(field_name);
        s.array_fields()[field_name] = *array;
      } else if (std::holds_alternative<int64_t>(val)) {
        s.array_fields().erase(field_name);
        s.fields()[field_name] = std::get<int64_t>(val);
      } else if (std::holds_alternative<std::string>(val)) {
        s.string_fields()[field_name] = std::get<std::string>(val);
      } else if (std::holds_alternative<double>(val)) {
        s.double_fields()[field_name] = std::get<double>(val);
      } else if (std::holds_alternative<StructValue>(val)) {
        s.struct_fields()[field_name] =
            std::make_shared<StructValue>(std::get<StructValue>(val));
      } else if (std::holds_alternative<EnumValue>(val)) {
        s.enum_fields()[field_name] = std::get<EnumValue>(val);
      } else {
        std::cerr << "[nilinterpreter:error] SET_FIELD unsupported value type: "
                  << val.index() << "\n";
        throw std::runtime_error("SET_FIELD: unsupported value type");
      }
    } else {
      throw std::runtime_error("SET_FIELD on non-struct: field=" + field_name +
                               " top_type=" + std::to_string(sv_ref.index()));
    }
  } break;
  case Opcode::SET_FIELD_PTR: {
    // SET_FIELD_PTR: stack = [..., ptr, val] — write val into (*ptr).field
    // ptr is an int64_t heap address pointing to a StructValue
    std::string field_name = instr.args[0];
    Value val = popStack();
    Value ptr_val = popStack();
    if (!std::holds_alternative<int64_t>(ptr_val)) {
      throw std::runtime_error("SET_FIELD_PTR: expected pointer (int64), got type " +
                               std::to_string(ptr_val.index()));
    }
    int64_t ptr = std::get<int64_t>(ptr_val);

    // Find the heap block containing this pointer
    bool found = false;
    for (auto &[base_ptr, block] : heap_) {
      const bool byte_block = is_byte_heap_type(block.type_name);
      int64_t end_ptr = base_ptr + static_cast<int64_t>(
                                       byte_block ? block.element_count
                                                  : block.element_count * 8U);
      if (ptr >= base_ptr && ptr < end_ptr) {
        size_t index = static_cast<size_t>((ptr - base_ptr) / 8);
        if (index < block.data.size()) {
          Value &slot = block.data[index];
          if (std::holds_alternative<StructValue>(slot)) {
            auto &s = std::get<StructValue>(slot);
            if (!std::holds_alternative<ArrayHandle>(val))
              s.array_fields().erase(field_name);
            if (const auto *array = std::get_if<ArrayHandle>(&val)) {
              s.fields().erase(field_name);
              s.array_fields()[field_name] = *array;
            } else if (std::holds_alternative<int64_t>(val)) {
              s.fields()[field_name] = std::get<int64_t>(val);
            } else if (std::holds_alternative<std::string>(val)) {
              s.string_fields()[field_name] = std::get<std::string>(val);
            } else if (std::holds_alternative<double>(val)) {
              s.double_fields()[field_name] = std::get<double>(val);
            } else if (std::holds_alternative<StructValue>(val)) {
              s.struct_fields()[field_name] =
                  std::make_shared<StructValue>(std::get<StructValue>(val));
            } else if (std::holds_alternative<EnumValue>(val)) {
              s.enum_fields()[field_name] = std::get<EnumValue>(val);
            } else {
              throw std::runtime_error("SET_FIELD_PTR: unsupported value type " +
                                       std::to_string(val.index()));
            }
          } else {
            throw std::runtime_error("SET_FIELD_PTR: heap slot is not a StructValue at ptr " +
                                     std::to_string(ptr));
          }
          found = true;
        }
        break;
      }
    }
    if (!found) {
      std::cerr << "[nilinterpreter] SET_FIELD_PTR: pointer " << ptr
                << " not in any heap block\n";
    }
  } break;
  // Array operations
  case Opcode::PARALLEL_MAP: {
    if (instr.args.size() != 3 || !is_parallel_scalar(instr.args[1]) ||
        !is_parallel_scalar(instr.args[2]))
      throw std::runtime_error("PARALLEL_MAP: invalid scalar worker contract");
    const auto source_value = popStack();
    const auto *source_handle = std::get_if<ArrayHandle>(&source_value);
    if (!source_handle) throw std::runtime_error("PARALLEL_MAP: expected typed input array");
    const auto source = heap_.find(source_handle->address);
    if (source == heap_.end() || source->second.type_name != "array" ||
        canonical_array_scalar(source->second.element_type) != instr.args[1])
      throw std::runtime_error("PARALLEL_MAP: input array type does not match worker");
    const auto count = source->second.data.size();
    (void)parallel_support::worker_count(count);
    // CPU availability can change between allocation and thread creation.
    // Reserve every permitted slot rather than trusting a second hardware
    // concurrency query to equal the first one.
    const auto slots = std::min(count, parallel_support::maximum_threads);
    const auto inputs = source->second.data;
    HeapBlock block;
    block.type_name = "array";
    block.element_type = instr.args[2];
    block.element_count = count;
    block.data.resize(count);
    std::vector<std::unique_ptr<VM>> workers;
    workers.reserve(slots);
    for (std::size_t index = 0; index < slots; ++index) {
      auto worker = std::make_unique<VM>();
      // A scalar worker is part of the parent invocation, never a separate
      // report owner. Worker implementation allocations are outside VM heap
      // block coverage; the parent records the resulting array below.
      worker->memory_profile_allowed_ = false;
      worker->setMemoryMode(memory_mode_);
      worker->setProcessExitTrapping(true);
      worker->loadProgram(program_, constants_);
      workers.push_back(std::move(worker));
    }
    parallel_support::for_each(count, [&](std::size_t slot, std::size_t index) {
      auto &worker = *workers[slot];
      worker.resetRuntimeState("<root>");
      const auto &input = inputs[index];
      if ((instr.args[1] == "f64") != std::holds_alternative<double>(input) ||
          (instr.args[1] != "f64" && !std::holds_alternative<int64_t>(input)))
        throw std::runtime_error("input does not contain the required scalar type");
      worker.pushValue(input);
      worker.callFunction(instr.args[0], 1);
      if (worker.stack_.size() != 1)
        throw std::runtime_error("worker did not return one scalar result");
      block.data[index] = canonical_pointer_value(block, worker.stack_.back(), "PARALLEL_MAP");
    });
    const auto extent = std::max<std::uint64_t>(count, 1) * 8U;
    if (next_ptr_ < 0 || extent > static_cast<std::uint64_t>(
            std::numeric_limits<int64_t>::max() - next_ptr_))
      throw std::runtime_error("PARALLEL_MAP: result allocation is too large");
    const int64_t handle = next_ptr_;
    next_ptr_ += static_cast<int64_t>(extent);
    storeHeapBlock(handle, std::move(block));
    pushStack(ArrayHandle{handle});
    break;
  }
  case Opcode::NEW_ARRAY: {
    int64_t size = 0;
    if (!instr.args.empty()) {
      size = std::stoll(instr.args[0]);
    } else if (availableStackValues() != 0 &&
               std::holds_alternative<int64_t>(stack_.back())) {
      // Size might be on stack
      size = std::get<int64_t>(popStack());
    }
    if (size < 0)
      throw std::runtime_error("NEW_ARRAY: size must not be negative");
    const auto count = static_cast<std::uint64_t>(size);
    const auto extent = std::max<std::uint64_t>(count, 1) * 8U;
    if (count > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()) / sizeof(Value) ||
        count > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) / 8U ||
        next_ptr_ < 0 || extent > static_cast<std::uint64_t>(
                                    std::numeric_limits<std::int64_t>::max() -
                                    next_ptr_))
      throw std::runtime_error("NEW_ARRAY: requested allocation is too large");
    if (memory_mode_ == MemoryMode::SAFE_GC && gc_critical_depth_ == 0 &&
        ++alloc_count_ >= gc_threshold_)
      gcCollect();
    // Match native NEW_ARRAY and source array literals: an array is a mutable
    // heap handle, not a value copy whose SET_INDEX update would be discarded.
    HeapBlock block;
    block.type_name = "array";
    block.element_type = "i64";
    block.element_count = static_cast<std::size_t>(size);
    block.data.resize(static_cast<std::size_t>(size), int64_t(0));
    const int64_t ptr = next_ptr_;
    next_ptr_ += static_cast<int64_t>(extent);
    storeHeapBlock(ptr, std::move(block));
    pushStack(ArrayHandle{ptr});
  } break;
  case Opcode::NEW_ARRAY_LITERAL: {
    // NEW_ARRAY_LITERAL count
    int64_t count = 0;
    if (!instr.args.empty())
      count = std::stol(instr.args[0]);

    // Auto-GC check
    if (memory_mode_ == MemoryMode::SAFE_GC && gc_critical_depth_ == 0) {
      alloc_count_++;
      if (alloc_count_ >= gc_threshold_) {
        gcCollect();
      }
    }

    HeapBlock block;
    block.type_name = "array";
    block.element_type = instr.args.size() >= 2 ? instr.args[1] : "any";
    if (is_array_tuple_type(block.element_type))
      (void)validate_tuple_schema(validate_tuple_schema, block.element_type, "NEW_ARRAY_LITERAL", 0);
    block.element_count = count;
    block.data.resize(count);

    // Pop items in reverse order
    for (int64_t i = count - 1; i >= 0; --i) {
      Value v = popStack();
      // Preserve Ny text and floating values, checking concrete element
      // metadata before they can enter mutable typed storage.
      block.data[i] = canonical_pointer_value(block, v, "NEW_ARRAY_LITERAL");
    }

    int64_t ptr = next_ptr_;
    // Always advance by at least 8 bytes so empty arrays get unique handles
    next_ptr_ += (count > 0 ? count : 1) * 8;
    storeHeapBlock(ptr, std::move(block));
    pushStack(ArrayHandle{ptr});
  } break;
  case Opcode::GET_INDEX: {
    Value idx_val = popStack();
    Value arr_val = popStack();
    if (std::holds_alternative<ArrayValue>(arr_val) &&
        std::holds_alternative<int64_t>(idx_val)) {
      auto &arr = std::get<ArrayValue>(arr_val);
      int64_t idx = std::get<int64_t>(idx_val);
      if (idx >= 0 && idx < (int64_t)arr.elements.size()) {
        pushStack(array_elem_to_value(arr.elements[idx]));
      } else {
        pushStack((int64_t)0); // out of bounds
      }
    } else if ((std::holds_alternative<ArrayHandle>(arr_val) ||
                std::holds_alternative<int64_t>(arr_val)) &&
               std::holds_alternative<int64_t>(idx_val)) {
      const auto *array = std::get_if<ArrayHandle>(&arr_val);
      int64_t ptr = array ? array->address : std::get<int64_t>(arr_val);
      int64_t idx = std::get<int64_t>(idx_val);

      Value value = (int64_t)0;

      const auto exact = heap_.find(ptr);
      if (array && exact != heap_.end() && exact->second.type_name == "array") {
        if (idx >= 0 &&
            static_cast<std::size_t>(idx) < exact->second.data.size())
          value = exact->second.data[static_cast<std::size_t>(idx)];
        else
          value = array_default_value(exact->second);
      } else if (const auto current = findHeapPointer(ptr, true)) {
        const auto moved = moveHeapPointer(*current, idx, false);
        if (moved) {
          const auto target_index = static_cast<std::size_t>(
              (static_cast<std::uint64_t>(*moved) -
               static_cast<std::uint64_t>(current->base)) /
              current->stride);
          HeapBlock &block = *current->block;
          if (is_byte_heap_type(block.type_name) &&
              target_index < block.byte_data.size()) {
            value = static_cast<int64_t>(
                static_cast<unsigned char>(block.byte_data[target_index]));
          } else if (target_index < block.data.size()) {
            value = block.data[target_index];
          }
        }
      }
      pushStack(value);
    } else {
      throw std::runtime_error("GET_INDEX type mismatch");
    }
  } break;
  case Opcode::SET_INDEX: {
    Value val = popStack();
    Value idx_val = popStack();
    // Index assignment consumes all three operands, just like native NIL.
    // Unlike SET_FIELD (which returns its updated value for a store-back),
    // SET_INDEX mutates an array/pointer handle and produces no result.
    Value arr_ref = popStack();
    if (std::holds_alternative<ArrayValue>(arr_ref)) {
      throw std::runtime_error(
          "SET_INDEX: legacy value-array writes are unsupported; use a heap "
          "array handle");
    } else if ((std::holds_alternative<ArrayHandle>(arr_ref) ||
                std::holds_alternative<int64_t>(arr_ref)) &&
               std::holds_alternative<int64_t>(idx_val)) {
      const auto *array = std::get_if<ArrayHandle>(&arr_ref);
      int64_t ptr = array ? array->address : std::get<int64_t>(arr_ref);
      int64_t idx = std::get<int64_t>(idx_val);

      const auto exact = heap_.find(ptr);
      if (array && exact != heap_.end() && exact->second.type_name == "array") {
        if (idx >= 0 &&
            static_cast<std::size_t>(idx) < exact->second.data.size())
          exact->second.data[static_cast<std::size_t>(idx)] =
              canonical_pointer_value(exact->second, val, "SET_INDEX");
      } else if (const auto current = findHeapPointer(ptr, true)) {
        const auto moved = moveHeapPointer(*current, idx, false);
        if (moved) {
          const auto target_index = static_cast<std::size_t>(
              (static_cast<std::uint64_t>(*moved) -
               static_cast<std::uint64_t>(current->base)) /
              current->stride);
          HeapBlock &block = *current->block;
          if (is_byte_heap_type(block.type_name) &&
              target_index < block.byte_data.size()) {
            const auto integer = std::get<int64_t>(
                canonical_pointer_value(block, val, "SET_INDEX"));
            block.byte_data[target_index] =
                static_cast<char>(static_cast<unsigned char>(integer));
          } else if (target_index < block.data.size()) {
            block.data[target_index] =
                canonical_pointer_value(block, val, "SET_INDEX");
          }
        }
      }
    } else {
      throw std::runtime_error("SET_INDEX type mismatch");
    }
  } break;
  case Opcode::LEN: {
    Value arr_val = popStack();
    if (std::holds_alternative<ArrayValue>(arr_val)) {
      pushStack((int64_t)std::get<ArrayValue>(arr_val).elements.size());
    } else if (std::holds_alternative<std::string>(arr_val)) {
      pushStack((int64_t)std::get<std::string>(arr_val).length());
    } else if (std::holds_alternative<ArrayHandle>(arr_val)) {
      int64_t ptr = std::get<ArrayHandle>(arr_val).address;
      int64_t size = 0;
      if (heap_.count(ptr)) {
        size = static_cast<int64_t>(heap_[ptr].element_count);
      }
      pushStack(size);
    } else {
      pushStack((int64_t)0);
    }
  } break;
  // Enum
  case Opcode::NEW_VARIANT: {
    int arg_count = std::stoi(instr.args[1]);
    EnumValue ev;
    ev.variant_name = canonical_variant_name(instr.args[0]);
    if (instr.args.size() >= 3)
      ev.type_name = canonical_array_type(instr.args[2]);

    // Pop payload arguments in reverse order and store them
    std::vector<EnumPayload> payloads;
    for (int i = 0; i < arg_count; ++i) {
      Value v = popStack();
      if (std::holds_alternative<std::string>(v)) {
        payloads.push_back(std::get<std::string>(v));
      } else if (std::holds_alternative<int64_t>(v)) {
        payloads.push_back(std::get<int64_t>(v));
      } else if (const auto *array = std::get_if<ArrayHandle>(&v)) {
        payloads.push_back(*array);
      } else if (std::holds_alternative<double>(v)) {
        payloads.push_back(std::get<double>(v));
      } else if (std::holds_alternative<StructValue>(v)) {
        payloads.push_back(std::get<StructValue>(v));
      } else if (std::holds_alternative<EnumValue>(v)) {
        payloads.push_back(
            std::make_shared<EnumValue>(std::get<EnumValue>(v)));
      } else {
        payloads.push_back((int64_t)0); // Default for unsupported types
      }
    }
    // Reverse to get correct order (args were popped in reverse)
    std::reverse(payloads.begin(), payloads.end());
    ev.payloads = std::move(payloads);

    pushStack(ev);
  } break;
  case Opcode::IS_VARIANT: {
    Value v = popStack();

    bool match = false;

    // Check EnumValue type first (new proper enum handling)
    if (std::holds_alternative<EnumValue>(v)) {
      match = canonical_variant_name(std::get<EnumValue>(v).variant_name) ==
              canonical_variant_name(instr.args[0]);
    }
    // Legacy: also check string values for backward compatibility
    else if (std::holds_alternative<std::string>(v)) {
      match = canonical_variant_name(std::get<std::string>(v)) ==
              canonical_variant_name(instr.args[0]);
    }

    pushStack(match ? 1 : 0);
  } break;
  case Opcode::UNWRAP_VARIANT: {
    // UNWRAP_VARIANT count - Pop enum value and push its payloads
    int payload_count = instr.args.empty() ? 0 : std::stoi(instr.args[0]);
    Value v = popStack();

    if (std::holds_alternative<EnumValue>(v)) {
      const auto &ev = std::get<EnumValue>(v);
      // Push payloads onto stack (in order, up to payload_count)
      for (int i = 0; i < payload_count && i < (int)ev.payloads.size(); ++i) {
        const auto &p = ev.payloads[i];
        if (std::holds_alternative<std::string>(p)) {
          pushStack(std::get<std::string>(p));
        } else if (std::holds_alternative<int64_t>(p)) {
          pushStack(std::get<int64_t>(p));
        } else if (const auto *array = std::get_if<ArrayHandle>(&p)) {
          pushStack(*array);
        } else if (std::holds_alternative<double>(p)) {
          pushStack(std::get<double>(p));
        } else if (std::holds_alternative<StructValue>(p)) {
          pushStack(std::get<StructValue>(p));
        } else if (std::holds_alternative<std::shared_ptr<EnumValue>>(p)) {
          const auto &nested = std::get<std::shared_ptr<EnumValue>>(p);
          if (!nested)
            throw std::runtime_error(
                "UNWRAP_VARIANT: nested enum payload is null");
          pushStack(*nested);
        }
      }
    }
    // If not EnumValue, do nothing (backward compatible with string-only enums)
  } break;
  // String
  case Opcode::STR_CONCAT: {
    Value b = popStack();
    Value a = popStack();
    std::string sa;
    if (std::holds_alternative<std::string>(a))
      sa = std::get<std::string>(a);
    else if (std::holds_alternative<double>(a))
      sa = std::to_string(std::get<double>(a));
    else if (std::holds_alternative<int64_t>(a))
      sa = std::to_string(std::get<int64_t>(a));
    else
      sa = "<?>";

    std::string sb;
    if (std::holds_alternative<std::string>(b))
      sb = std::get<std::string>(b);
    else if (std::holds_alternative<double>(b))
      sb = std::to_string(std::get<double>(b));
    else if (std::holds_alternative<int64_t>(b))
      sb = std::to_string(std::get<int64_t>(b));
    else
      sb = "<?>";
    pushStack(sa + sb);
  } break;
  case Opcode::TO_STRING: {
    Value a = popStack();
    if (instr.args.size() > 1 ||
        (!instr.args.empty() && !supports_scalar_text(instr.args[0])))
      throw std::runtime_error("TO_STRING requires a supported scalar source type");
    const auto type = instr.args.empty() ? "" : scalar_text_type(instr.args[0]);
    if (const auto *integer = std::get_if<int64_t>(&a);
        integer && (type.empty() || type == "bool" || type == "i32" ||
                    type == "i64" || type == "u8")) {
      pushStack(type == "bool" ? std::string(*integer ? "true" : "false")
                               : scalar_text(*integer));
    } else if (const auto *floating = std::get_if<double>(&a);
               floating && (type.empty() || type == "f64")) {
      pushStack(scalar_text(*floating));
    } else if (std::holds_alternative<std::string>(a) &&
               (type.empty() || type == "text")) {
      pushStack(a);
    } else
      throw std::runtime_error("TO_STRING operand does not match its scalar source type");
  } break;
  case Opcode::CAST: {
    if (instr.args.size() != 1 || instr.args[0].empty())
      throw std::runtime_error("CAST: exactly one nonempty target type is required");
    std::string target_type = instr.args[0];
    if (target_type == "int")
      target_type = "i32";
    Value v = popStack();
    if (class_metadata_ && (find_nil_class(*class_metadata_, target_type) ||
        (std::holds_alternative<StructValue>(v) &&
         find_nil_class(*class_metadata_, std::get<StructValue>(v).type_name))))
      throw std::runtime_error("CAST cannot fabricate or erase class provenance");

    if (target_type == "i8" || target_type == "i16" ||
        target_type == "u16" || target_type == "u32" ||
        target_type == "u64" || target_type == "isize" ||
        target_type == "usize" || target_type == "char" ||
        target_type == "f32" || target_type == "float" ||
        target_type == "double") {
      throw std::runtime_error("CAST: unsupported scalar target '" + target_type +
                               "'; supported targets are bool, i32, i64, u8, f64");
    }
    const auto floating_integer_word = [](double value) -> int64_t {
      // INT64_MAX rounds up to 2^63 as a double, so the upper comparison must
      // be exclusive. Ordered bounds also reject NaN and both infinities.
      constexpr double minimum = -9223372036854775808.0;
      constexpr double limit = 9223372036854775808.0;
      if (!std::isfinite(value) || value < minimum || value >= limit)
        throw std::runtime_error(
            "CAST: floating-point value is non-finite or outside i64 range");
      return static_cast<int64_t>(value); // finite, in range, truncates to zero
    };
    const auto signed_low_word = [](int64_t value) -> int64_t {
      const std::uint64_t low = static_cast<std::uint64_t>(value) & 0xffffffffULL;
      return low >= 0x80000000ULL ? static_cast<int64_t>(low) - 0x100000000LL
                                  : static_cast<int64_t>(low);
    };
    const auto integer_word = [&]() -> int64_t {
      if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
      if (std::holds_alternative<double>(v))
        return floating_integer_word(std::get<double>(v));
      if (std::holds_alternative<std::string>(v)) {
        const auto parsed = parse_signed_integer(std::get<std::string>(v), 10,
                                                 false);
        if (!parsed.ok)
          throw std::runtime_error(
              "CAST: text is not a complete decimal i64 value");
        return parsed.value;
      }
      throw std::runtime_error("CAST: value cannot be converted to '" +
                               target_type + "'");
    };

    // u8 is a value conversion, not a range assertion: retain the low eight
    // bits on every route before the value reaches an array, pointer, or file
    // boundary.
    if (target_type == "u8") {
      pushStack(static_cast<int64_t>(static_cast<std::uint8_t>(integer_word())));
    }
    // Explicit integer narrowing retains low bits, matching scalar storage.
    // Floating inputs first use the checked signed-i64 intermediate above.
    else if (target_type == "i32" || target_type == "i64") {
      const int64_t integer = integer_word();
      pushStack(target_type == "i32" ? signed_low_word(integer) : integer);
    }
    // f32 conversion remains unavailable until its rounding/ABI is coherent.
    else if (target_type == "f64") {
      if (std::holds_alternative<double>(v)) {
        pushStack(v); // Already double
      } else if (std::holds_alternative<int64_t>(v)) {
        pushStack((double)std::get<int64_t>(v));
      } else if (std::holds_alternative<std::string>(v)) {
        const auto parsed = parse_finite_f64(std::get<std::string>(v));
        if (!parsed)
          throw std::runtime_error(
              "CAST: text is not a complete finite f64 value");
        pushStack(*parsed);
      } else {
        throw std::runtime_error("CAST: value cannot be converted to 'f64'");
      }
    }
    // String/text cast
    else if (target_type == "text" || target_type == "string" ||
             target_type == "String") {
      if (std::holds_alternative<std::string>(v)) {
        pushStack(v);
      } else if (std::holds_alternative<int64_t>(v)) {
        pushStack(std::to_string(std::get<int64_t>(v)));
      } else if (std::holds_alternative<double>(v)) {
        pushStack(std::to_string(std::get<double>(v)));
      } else {
        throw std::runtime_error("CAST: value cannot be converted to '" +
                                 target_type + "'");
      }
    }
    // Bool cast - bools are represented as int64_t (0 or 1)
    else if (target_type == "bool") {
      if (std::holds_alternative<int64_t>(v)) {
        pushStack(std::get<int64_t>(v) != 0 ? (int64_t)1 : (int64_t)0);
      } else if (std::holds_alternative<double>(v)) {
        pushStack(std::get<double>(v) != 0.0 ? (int64_t)1 : (int64_t)0);
      } else if (std::holds_alternative<std::string>(v)) {
        const auto parsed = parse_bool_text(std::get<std::string>(v));
        if (!parsed)
          throw std::runtime_error("CAST: text is not a recognized bool value");
        pushStack(static_cast<int64_t>(*parsed));
      } else {
        throw std::runtime_error("CAST: value cannot be converted to 'bool'");
      }
    }
    else {
      // Source-level identity casts are proven and erased by the checker and
      // lowering. A raw CAST cannot establish pointer, array, enum, function,
      // or nominal identity from the VM's partially erased representation.
      throw std::runtime_error("CAST: unsupported target '" + target_type +
                               "'; non-scalar identity requires checked source");
    }
  } break;
  case Opcode::STRLEN: {
    Value a = popStack();
    if (std::holds_alternative<std::string>(a)) {
      pushStack((int64_t)std::get<std::string>(a).length());
    } else {
      pushStack((int64_t)0);
    }
  } break;
  // Memory opcodes
  case Opcode::OWN_ALLOC:
  case Opcode::ALLOC: {
    // ALLOC type_name: pop size from stack, allocate heap block, push pointer
    std::string type_name = instr.args.empty() ? "any" : instr.args[0];
    int64_t count = 1;
    if (availableStackValues() != 0 &&
        std::holds_alternative<int64_t>(stack_.back())) {
      count = std::get<int64_t>(popStack());
    }
    if (count < 1)
      count = 1;

    const auto stride = heap_element_stride(type_name);
    const auto unsigned_count = static_cast<std::uint64_t>(count);
    const auto maximum_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (unsigned_count > (maximum_size - 7U) / stride)
      throw std::runtime_error("ALLOC: requested allocation is too large");
    const auto raw_size = static_cast<std::size_t>(unsigned_count * stride);
    const auto aligned_size = (raw_size + 7U) & ~std::size_t(7U);
    if (next_ptr_ < 0)
      throw std::runtime_error("ALLOC: virtual address space exhausted");
    const auto available_address_bytes = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max() - next_ptr_);
    if (aligned_size > available_address_bytes ||
        heap_allocation_guard_bytes > available_address_bytes - aligned_size) {
      throw std::runtime_error("ALLOC: virtual address space exhausted");
    }
    const auto allocation_extent =
        static_cast<std::uint64_t>(aligned_size) + heap_allocation_guard_bytes;

    // Auto-GC: trigger collection if in safe mode and threshold exceeded
    if (memory_mode_ == MemoryMode::SAFE_GC && gc_critical_depth_ == 0) {
      alloc_count_++;
      if (alloc_count_ >= gc_threshold_) {
        gcCollect();
      }
    }

    HeapBlock block;
    block.type_name = type_name;
    block.element_count = static_cast<size_t>(count);
    if (is_byte_heap_type(type_name))
      block.byte_data.resize(static_cast<size_t>(count), 0);
    else
      block.data.resize(static_cast<size_t>(count), int64_t(0));

    int64_t ptr = next_ptr_;
    // The guard range is deliberately absent from heap_. It keeps every raw
    // allocation's logical one-past address distinct from all later exact
    // allocation bases without changing language-visible element strides.
    next_ptr_ += static_cast<int64_t>(allocation_extent);
    storeHeapBlock(ptr, std::move(block));

    pushStack(ptr);
  } break;
  case Opcode::FREE: {
    // FREE: pop pointer from stack, deallocate heap block
    Value ptr_val = popStack();
    if (std::holds_alternative<int64_t>(ptr_val)) {
      int64_t ptr = std::get<int64_t>(ptr_val);
      auto it = heap_.find(ptr);
      if (it != heap_.end()) {
        releaseHeapBlock(ptr);
      } else {
        std::cerr << "[nilinterpreter] FREE: invalid pointer " << ptr
                  << std::endl;
      }
    } else {
      throw std::runtime_error(
          "FREE: expected pointer (int64), got different type");
    }
  } break;
  case Opcode::READ_PTR: {
    // READ_PTR: pop pointer from stack, read value at pointer, push result
    Value ptr_val = popStack();
    if (std::holds_alternative<int64_t>(ptr_val)) {
      int64_t ptr = std::get<int64_t>(ptr_val);
      Value value = (int64_t)0;
      const auto location = findHeapPointer(ptr, false);
      if (location) {
        const auto index = static_cast<std::size_t>(location->element_index);
        HeapBlock &block = *location->block;
        if (is_byte_heap_type(block.type_name) &&
            index < block.byte_data.size()) {
          value = static_cast<int64_t>(
              static_cast<unsigned char>(block.byte_data[index]));
        } else if (index < block.data.size()) {
          value = block.data[index];
        }
      }
      if (!location) {
        std::cerr << "[nilinterpreter] READ_PTR: pointer " << ptr
                  << " not in any heap block" << std::endl;
      }
      pushStack(value);
    } else {
      throw std::runtime_error("READ_PTR: expected pointer (int64)");
    }
  } break;
  case Opcode::WRITE_PTR: {
    // WRITE_PTR: pop value, pop pointer, write value at pointer
    Value val = popStack();
    Value ptr_val = popStack();
    if (!std::holds_alternative<int64_t>(ptr_val)) {
      throw std::runtime_error("WRITE_PTR: expected pointer (int64)");
    }
    int64_t ptr = std::get<int64_t>(ptr_val);

    bool found = false;
    const auto location = findHeapPointer(ptr, false);
    if (location) {
      const auto index = static_cast<std::size_t>(location->element_index);
      HeapBlock &block = *location->block;
      if (is_byte_heap_type(block.type_name) &&
          index < block.byte_data.size()) {
        const auto integer = std::get<int64_t>(
            canonical_pointer_value(block, val, "WRITE_PTR"));
        block.byte_data[index] =
            static_cast<char>(static_cast<unsigned char>(integer));
        found = true;
      } else if (index < block.data.size()) {
        block.data[index] = canonical_pointer_value(block, val, "WRITE_PTR");
        found = true;
      }
    }
    if (!found) {
      std::cerr << "[nilinterpreter] WRITE_PTR: pointer " << ptr
                << " not in any heap block" << std::endl;
    }
  } break;
  default:
    throw std::runtime_error("Unknown opcode: " +
                             std::to_string(static_cast<int>(instr.op)));
  }
}

Value VM::popStack() {
  requireStackValues(1, "operand pop");
  Value v = stack_.back();
  stack_.pop_back();
  return v;
}

void VM::pushStack(const Value &val) { stack_.push_back(val); }

size_t VM::availableStackValues() const {
  const size_t base = call_stack_.empty() ? 0 : call_stack_.back().stack_base;
  if (stack_.size() < base)
    throw std::runtime_error("Operand stack crossed its call-frame boundary");
  return stack_.size() - base;
}

void VM::requireStackValues(size_t count, const std::string &operation) const {
  const size_t available = availableStackValues();
  if (available >= count)
    return;
  const std::string context =
      call_stack_.empty() ? "<root>" : call_stack_.back().fn_name;
  // Embedders may call with pc_ at or beyond the end of the loaded program.
  // Reporting underflow must not itself index an invalid instruction.
  throw std::runtime_error(
      "Stack underflow in " + operation + " at pc=" + std::to_string(pc_) +
      " fn=" + context + ": requires " + std::to_string(count) +
      " value(s) in current frame, found " + std::to_string(available));
}

void VM::validateInstructionLocation() const {
  if (call_stack_.empty() || requested_exit_code_)
    return;
  const auto &frame = call_stack_.back();
  if (!frame.return_type.empty() && !frame.returned &&
      (pc_ < frame.entry_pc || pc_ >= frame.end_pc))
    throw std::runtime_error("function '" + frame.fn_name +
                             "' falls through or escapes without RET");
}

// -------------------------------
// Per-frame (call-frame) helpers
// -------------------------------

void VM::pushFrame(const std::string &fn_name, size_t return_pc,
                   size_t argument_count) {
  requireStackValues(argument_count, "call to " + fn_name);
  CallFrame f;
  f.return_pc = return_pc;
  f.owned_depth = owned_scopes_.size();
  f.stack_base = stack_.size() - argument_count;
  if (const auto function = functions_.find(fn_name); function != functions_.end()) {
    for (std::size_t i = 0; i < argument_count && i < function->second.parameter_types.size(); ++i)
      validateClassValue(stack_[stack_.size() - argument_count + i], function->second.parameter_types[i]);
    f.return_type = function->second.return_type;
    f.entry_pc = function->second.entry_pc;
    f.end_pc = function->second.end_pc;
  }
  f.fn_name = fn_name;
  f.locals.clear();
  call_stack_.push_back(std::move(f));
}

void VM::popFrame() {
  if (!call_stack_.empty()) {
    unwindOwned(call_stack_.back().owned_depth);
    const auto depth = call_stack_.size();
    saved_faults_.erase(std::remove_if(saved_faults_.begin(), saved_faults_.end(),
                       [&](const auto &saved) { return saved.frame_depth >= depth; }),
                       saved_faults_.end());
    fault_regions_.erase(std::remove_if(fault_regions_.begin(), fault_regions_.end(),
                        [&](const auto &region) { return region.frame_depth >= depth; }),
                        fault_regions_.end());
    call_stack_.pop_back();
  }
}

VM::SavedFault &VM::savedFault(const std::string &slot) {
  const auto found = std::find_if(saved_faults_.begin(), saved_faults_.end(),
      [&](const auto &saved) {
        return saved.frame_depth == call_stack_.size() && saved.slot == slot;
      });
  if (found == saved_faults_.end())
    throw std::runtime_error("saved fault slot '" + slot + "' is absent or consumed");
  return *found;
}

void VM::propagateFault() {
  if (!pending_fault_)
    throw std::runtime_error("fault propagation requires a pending payload");
  if (fault_regions_.empty()) {
    unwindOwned(0);
    throw UncaughtFault(pending_fault_->type_name, pending_fault_->source_line);
  }
  const auto target = fault_regions_.back();
  fault_regions_.pop_back();
  if (target.frame_depth == 0 || target.frame_depth > call_stack_.size() ||
      target.stack_height > stack_.size())
    throw std::runtime_error("invalid fault unwind frame or operand prefix");
  while (call_stack_.size() > target.frame_depth)
    popFrame();
  unwindOwned(target.owned_depth);
  for (const auto &slot : target.saved_slots)
    (void)savedFault(slot);
  saved_faults_.erase(std::remove_if(saved_faults_.begin(), saved_faults_.end(),
      [&](const auto &saved) {
        return saved.frame_depth == target.frame_depth &&
               std::find(target.saved_slots.begin(), target.saved_slots.end(), saved.slot) ==
                   target.saved_slots.end();
      }), saved_faults_.end());
  stack_.resize(target.stack_height);
  // Both execute() and stepInstruction() advance once after this instruction.
  pc_ = target.dispatch_pc - 1;
}

bool VM::hasLocal(const std::string &name) const {
  if (call_stack_.empty())
    return false;
  const auto &locals = call_stack_.back().locals;
  return locals.find(name) != locals.end();
}

void VM::unwindOwned(std::size_t depth) {
  if (depth > owned_scopes_.size())
    throw std::runtime_error("invalid affine scope unwind depth");
  while (owned_scopes_.size() > depth) {
    const auto &scope = owned_scopes_.back();
    for (const auto &slot : scope.slots) {
      releaseHeapBlock(slot.second);
      if (scope.frame_depth > 0 && scope.frame_depth <= call_stack_.size())
        call_stack_[scope.frame_depth - 1].locals[slot.first] = int64_t(0);
    }
    owned_scopes_.pop_back();
  }
}

Value VM::getLocal(const std::string &name) const {
  if (call_stack_.empty()) {
    throw std::runtime_error("No call frame available for getLocal");
  }
  const auto &locals = call_stack_.back().locals;
  auto it = locals.find(name);
  if (it == locals.end()) {
    // Fall back to globals
    auto git = globals_.find(name);
    if (git != globals_.end()) {
      return git->second;
    }

    // Throw exception with locals dump on failure
    std::cerr << "NIL interpreter local lookup failed: '" << name << "'\n";
    std::cerr << "  Current Frame Depth: " << call_stack_.size() << "\n";
    if (!call_stack_.empty()) {
      std::cerr << "  Available Locals in current frame:\n";
      for (const auto &[k, v] : call_stack_.back().locals) {
        std::cerr << "    " << k << "\n";
      }
    } else {
      std::cerr << "  No active call frame.\n";
    }
    throw std::runtime_error("Local not found: " + name);
  }
  return it->second;
}

void VM::setLocal(const std::string &name, const Value &v) {
  if (call_stack_.empty()) {
    // Ensure there is a root frame so tests and simple code can set locals
    pushFrame("<root>", 0);
  }
  call_stack_.back().locals[name] = v;
}

void VM::callFunction(const std::string &name, size_t arg_count) {
  // External call entry point. Args must already be on the stack in forward
  // order (arg0 pushed first, argN pushed last). The function's STORE_LOCAL
  // preamble will pop them in reverse order, binding them to param names.
  //
  // Uses program_.size() as the sentinel return address: when the function's
  // RET fires, pc_ becomes program_.size() and execute(false) exits the loop.
  auto it = functions_.find(name);
  if (it == functions_.end()) {
    throw std::runtime_error("callFunction: unknown function '" + name + "'");
  }
  if (arg_count != it->second.param_count) {
    throw std::runtime_error("callFunction: function '" + name + "' expects " +
                             std::to_string(it->second.param_count) +
                             " arguments, got " + std::to_string(arg_count));
  }
  pushFrame(name, program_.size(), arg_count);
  pc_ = it->second.entry_pc;
  execute(false);
}

void VM::returnFromCall() {
  if (call_stack_.empty())
    return;
  size_t ret_pc = call_stack_.back().return_pc;
  popFrame();
  pc_ = ret_pc;
}

void VM::validateClassValue(const Value &value, const std::string &expected) const {
  if (!class_metadata_) return;
  auto nominal = expected;
  if (nominal.rfind("ref<", 0) == 0 && nominal.back() == '>') nominal = nominal.substr(4, nominal.size() - 5);
  const auto *object = std::get_if<StructValue>(&value);
  const bool expected_class = find_nil_class(*class_metadata_, nominal) != nullptr;
  const bool actual_class = object && find_nil_class(*class_metadata_, object->type_name);
  if (!expected_class && !actual_class) return;
  if (!expected_class || !actual_class || object->static_class_view != expected ||
      !nil_class_assignable(*class_metadata_, nominal, object->type_name))
    throw std::runtime_error("Class value does not have its required checked static view '" + expected + "'");
}

std::string VM::classFieldType(const StructValue &value, const std::string &field) const {
  if (!class_metadata_) return {};
  auto nominal = value.static_class_view.empty() ? value.type_name : value.static_class_view;
  if (nominal.rfind("ref<", 0) == 0 && nominal.back() == '>') nominal = nominal.substr(4, nominal.size() - 5);
  for (const auto &schema : class_metadata_->structs) if (schema.name == nominal)
    for (const auto &candidate : schema.fields) if (candidate.first == field) return candidate.second;
  if (find_nil_class(*class_metadata_, value.type_name))
    throw std::runtime_error("Class field is unavailable through its static receiver view");
  return {};
}

// ---------------------------
// Garbage Collection (Mark-Sweep)
// ---------------------------

std::optional<VM::HeapPointerLocation>
VM::findHeapPointer(int64_t pointer, bool allow_one_past) {
  // An exact allocation base always wins over another allocation's one-past
  // address. This makes lookup deterministic even when logical blocks are
  // adjacent in the VM's virtual address space.
  if (const auto exact = heap_.find(pointer); exact != heap_.end()) {
    HeapBlock &block = exact->second;
    if (block.type_name == "array" || block.type_name == "__iter" ||
        (!allow_one_past && block.element_count == 0))
      return std::nullopt;
    return HeapPointerLocation{pointer, &block, 0,
                               heap_element_stride(block.type_name)};
  }

  if (pointer < 0)
    return std::nullopt;
  const auto address = static_cast<std::uint64_t>(pointer);
  for (auto &[base_pointer, block] : heap_) {
    if (base_pointer < 0 || block.type_name == "array" ||
        block.type_name == "__iter")
      continue;

    const auto base = static_cast<std::uint64_t>(base_pointer);
    const auto stride = heap_element_stride(block.type_name);
    const auto count = static_cast<std::uint64_t>(block.element_count);
    if (count > (std::numeric_limits<std::uint64_t>::max() - base) / stride)
      continue;

    const auto end = base + count * stride;
    if (address <= base || address > end || (!allow_one_past && address == end))
      continue;

    const auto byte_offset = address - base;
    if (byte_offset % stride != 0)
      continue;

    return HeapPointerLocation{base_pointer, &block, byte_offset / stride,
                               stride};
  }
  return std::nullopt;
}

std::optional<int64_t> VM::moveHeapPointer(const HeapPointerLocation &location,
                                           int64_t amount,
                                           bool subtract) const {
  const auto magnitude = amount < 0
                             ? static_cast<std::uint64_t>(-(amount + 1)) + 1U
                             : static_cast<std::uint64_t>(amount);
  const bool move_backward = subtract != (amount < 0);
  const auto count = static_cast<std::uint64_t>(location.block->element_count);

  std::uint64_t target = 0;
  if (move_backward) {
    if (magnitude > location.element_index)
      return std::nullopt;
    target = location.element_index - magnitude;
  } else {
    if (location.element_index > count ||
        magnitude > count - location.element_index)
      return std::nullopt;
    target = location.element_index + magnitude;
  }

  const auto base = static_cast<std::uint64_t>(location.base);
  if (target >
      (static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max()) - base) /
          location.stride)
    return std::nullopt;
  return static_cast<int64_t>(base + target * location.stride);
}

std::uint64_t VM::heapBlockBytes(const HeapBlock &block) const {
  const std::uint64_t value_bytes =
      static_cast<std::uint64_t>(block.data.size()) *
      heap_element_stride(block.type_name);
  const std::uint64_t byte_bytes =
      static_cast<std::uint64_t>(block.byte_data.size());
  if (value_bytes > std::numeric_limits<std::uint64_t>::max() - byte_bytes)
    return std::numeric_limits<std::uint64_t>::max();
  return value_bytes + byte_bytes;
}

std::uint64_t VM::currentHeapBytes() const {
  const std::uint64_t observable_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  std::uint64_t total = 0;
  for (const auto &[pointer, block] : heap_) {
    (void)pointer;
    const std::uint64_t bytes =
        std::min(heapBlockBytes(block), observable_limit);
    if (total > observable_limit - bytes)
      return observable_limit;
    total += bytes;
  }
  return total;
}

std::uint64_t VM::profileHeapBlockBytes(const HeapBlock &block) const {
  // These are language-visible payload bytes, not the host's sizeof(Value),
  // vector capacity, allocator overhead, or transitive string/record storage.
  // Keep profiling independent of the historical gc.allocated_bytes metric.
  auto stride = heap_element_stride(block.type_name);
  if (block.type_name == "array") {
    const auto element = canonical_array_type(block.element_type);
    stride = element == "i32" || element == "f32" ? 4
             : element == "bool" || element == "u8" ? 1
                                                     : 8;
  }
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto bytes = static_cast<std::uint64_t>(block.byte_data.size());
  const auto count = static_cast<std::uint64_t>(block.data.size());
  if (count > (maximum - bytes) / stride)
    return maximum;
  return count * stride + bytes;
}

std::uint64_t VM::profileHeapBytes() const {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t total = 0;
  for (const auto &[pointer, block] : heap_) {
    (void)pointer;
    const auto bytes = profileHeapBlockBytes(block);
    if (bytes > maximum - total)
      return maximum;
    total += bytes;
  }
  return total;
}

void VM::releaseHeapBlock(int64_t pointer) {
  if (heap_.erase(pointer))
    memory_profile_.release(
        reinterpret_cast<const void *>(static_cast<std::uintptr_t>(pointer)));
}

void VM::storeHeapBlock(int64_t pointer, HeapBlock block) {
  const std::uint64_t bytes = heapBlockBytes(block);
  account_heap_allocation(total_allocated_bytes_, bytes);
  heap_[pointer] = std::move(block);
  if (memory_profile_.enabled()) {
    const auto &stored = heap_.at(pointer);
    const auto kind = stored.type_name == "array"
        ? "array<" + stored.element_type + ">"
        : stored.type_name == "__iter" ? std::string("iterator")
        : (pc_ < program_.size() && program_[pc_].op == Opcode::OWN_ALLOC
               ? "owned_buffer<" : "buffer<") + stored.type_name + ">";
    memory_profile_.allocate(
        reinterpret_cast<const void *>(static_cast<std::uintptr_t>(pointer)),
        profileHeapBlockBytes(stored), kind);
  }
}

void VM::gcCollect() {
  if (memory_mode_ != MemoryMode::SAFE_GC) {
    return; // Only run GC in safe mode
  }

  const bool profiling = memory_profile_.enabled();
  const auto profile_bytes_before = profiling ? profileHeapBytes() : 0;
  const auto profile_objects_before = heap_.size();
  const auto profile_start = profiling ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};

  // Reset all marks
  for (auto &[ptr, block] : heap_) {
    (void)ptr;
    block.marked = false;
  }
  gc_marked_structs_.clear();

  // Mark phase: mark all reachable objects from roots
  gcMarkRoots();

  // Sweep phase: free unmarked objects
  gcSweep();

  // Reset allocation counter
  alloc_count_ = 0;
  if (total_collections_ <
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    ++total_collections_;
  if (profiling) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - profile_start).count();
    memory_profile_.collection(
        elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0,
        profile_bytes_before - std::min(profile_bytes_before, profileHeapBytes()),
        profile_objects_before - heap_.size());
  }
}

void VM::gcMarkRoots() {
  for (const auto &scope : owned_scopes_)
    for (const auto &slot : scope.slots) gcMarkValue(slot.second);
  if (pending_fault_)
    gcMarkValue(pending_fault_->payload);
  for (const auto &saved : saved_faults_)
    gcMarkValue(saved.fault.payload);
  // Mark from stack
  for (const auto &val : stack_) {
    gcMarkValue(val);
  }

  // Mark from call frame locals
  for (const auto &frame : call_stack_) {
    for (const auto &[name, val] : frame.locals) {
      (void)name;
      gcMarkValue(val);
    }
  }

  // Globals survive function returns and therefore must be roots. Omitting
  // them allowed a forced collection to invalidate a live global array.
  for (const auto &[name, value] : globals_) {
    (void)name;
    gcMarkValue(value);
  }

}

void VM::gcMarkValue(const Value &val) {
  if (std::holds_alternative<int64_t>(val) || std::holds_alternative<ArrayHandle>(val)) {
    const auto *array = std::get_if<ArrayHandle>(&val);
    int64_t ptr = array ? array->address : std::get<int64_t>(val);
    auto it = heap_.find(ptr);
    if (it == heap_.end()) {
      // A typed raw pointer can be the sole surviving reference to an aligned
      // interior element or its logical one-past address. Raw allocations leave
      // a non-addressable guard range, so one-past identity is unambiguous and
      // must retain the allocation until pointer arithmetic walks back into it.
      if (const auto interior = findHeapPointer(ptr, true))
        it = heap_.find(interior->base);
    }
    if (it != heap_.end() && !it->second.marked) {
      it->second.marked = true;
      // Recursively mark any pointers stored in this block
      // HeapBlock.data is vector<Value>
      for (const auto &stored_val : it->second.data) {
        gcMarkValue(stored_val);
      }
    }
  } else if (std::holds_alternative<StructValue>(val)) {
    // Structs have shared reference semantics and can be cyclic. Visit each
    // backing StructData once per collection.
    const auto &sv = std::get<StructValue>(val);
    if (!sv.data || !gc_marked_structs_.insert(sv.data.get()).second)
      return;
    for (const auto &[name, field_val] : sv.fields()) {
      (void)name;
      gcMarkValue(field_val);
    }
    for (const auto &[name, field_val] : sv.array_fields()) {
      (void)name;
      gcMarkValue(field_val);
    }
    for (const auto &[name, field_value] : sv.struct_fields()) {
      (void)name;
      if (field_value)
        gcMarkValue(*field_value);
    }
    for (const auto &[name, field_value] : sv.enum_fields()) {
      (void)name;
      gcMarkValue(field_value);
    }
  } else if (std::holds_alternative<ArrayValue>(val)) {
    // Mark any pointer elements in arrays
    const auto &av = std::get<ArrayValue>(val);
    for (const auto &elem : av.elements) {
      gcMarkValue(array_elem_to_value(elem));
    }
  } else if (std::holds_alternative<EnumValue>(val)) {
    const auto &enum_value = std::get<EnumValue>(val);
    for (const auto &payload : enum_value.payloads) {
      std::visit(
          [&](const auto &value) {
            using Payload = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Payload,
                                         std::shared_ptr<EnumValue>>) {
              if (value)
                gcMarkValue(*value);
            } else {
              gcMarkValue(Value(value));
            }
          },
          payload);
    }
  }
}

void VM::gcSweep() {
  std::vector<int64_t> to_free;
  for (const auto &[ptr, block] : heap_) {
    if (!block.marked) {
      to_free.push_back(ptr);
    }
  }

  for (int64_t ptr : to_free) {
    releaseHeapBlock(ptr);
  }
}

} // namespace ny
