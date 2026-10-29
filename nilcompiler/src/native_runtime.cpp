#include "native_runtime.hpp"
#include "dynamic_type.hpp"
#include "database_runtime.hpp"
#include "ui_runtime.hpp"
#include "integer_conversion.hpp"
#include "memory_profile.hpp"
#include "pattern_engine.hpp"
#include "nildata.hpp"
#include "scalar_text.hpp"

#include "secure_file_io.hpp"
#include "time_support.hpp"
#include "parallel_support.hpp"
#include "owned_runtime.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t nyrt_array_magic = 0x4e59525441525231ULL;
constexpr std::uint64_t nyrt_buffer_magic = 0x4e59525442554631ULL;
constexpr std::uint64_t maximum_file_bytes = 512ULL * 1024ULL * 1024ULL;

struct nyrt_array {
  std::uint64_t magic = nyrt_array_magic;
  std::uint32_t version = 1;
  std::uint32_t kind = 0;
  std::uint64_t stride = 0;
  std::uint64_t length = 0;
  std::uint64_t capacity = 0;
  std::size_t allocated_bytes = 0;
  void *data = nullptr;

  ~nyrt_array() noexcept {
    std::free(data);
    data = nullptr;
    magic = 0;
  }
};

struct nyrt_buffer {
  std::uint64_t magic = nyrt_buffer_magic;
  std::uint32_t version = 1;
  std::uint32_t kind = 0;
  std::uint64_t stride = 0;
  std::uint64_t length = 0;
  std::size_t allocated_bytes = 0; // Includes the hidden guard element.
  void *data = nullptr;
  bool active = true;

  ~nyrt_buffer() noexcept {
    std::free(data);
    data = nullptr;
    magic = 0;
  }
};

struct nyrt_any_box {
  std::uint64_t payload;
  std::uint64_t type_length;
  // The exact descriptor follows this header in the same managed allocation.
};

struct runtime_context {
  ny::memory_profile::Session profile;
  ny::database::Context databases;
  ny::ui::Context ui;
  struct allocation {
    void *data;
    std::size_t size;
    bool dynamic_box = false;
  };
  struct roots {
    const void *const *slots;
    std::uint64_t count;
  };
  struct fault {
    std::string type;
    void *payload = nullptr;
    std::int64_t line = 0;
  };
  struct saved_fault {
    std::uint64_t slot;
    fault value;
  };
  struct fault_frame {
    std::uint64_t identity;
    std::vector<saved_fault> saved;
  };
  std::vector<std::string> arguments;
  std::vector<std::unique_ptr<nyrt_array>> arrays;
  std::vector<std::unique_ptr<nyrt_buffer>> buffers;
  std::vector<allocation> allocations;
  std::vector<roots> frames;
  std::optional<fault> pending_fault;
  std::vector<fault_frame> fault_frames;
  std::uint64_t next_fault_frame = 1;
  std::uint64_t allocated_since_collection = 0;
};

thread_local runtime_context context;
thread_local const char *parallel_failure = nullptr;

[[noreturn]] void invalid_fault_protocol() noexcept {
  nyrt_fail("NIL native runtime error: invalid fault protocol state\n");
}

runtime_context::fault_frame &current_fault_frame() noexcept {
  if (context.fault_frames.empty()) invalid_fault_protocol();
  return context.fault_frames.back();
}

std::string_view fault_type_span(const char *type, std::uint64_t length) noexcept {
  if (type == nullptr || length == 0 || length > 1024ULL * 1024ULL)
    invalid_fault_protocol();
  return std::string_view(type, static_cast<std::size_t>(length));
}

runtime_context::saved_fault &find_saved_fault(std::uint64_t slot) noexcept {
  auto &frame = current_fault_frame();
  for (auto &saved : frame.saved)
    if (saved.slot == slot) return saved;
  invalid_fault_protocol();
}

void account_managed_bytes(std::size_t bytes) noexcept {
  context.allocated_since_collection += std::min<std::uint64_t>(bytes,
      std::numeric_limits<std::uint64_t>::max() - context.allocated_since_collection);
}

std::uint64_t stride_for_kind(std::uint32_t kind) noexcept {
  switch (kind) {
  case NYRT_ARRAY_I32:
    return sizeof(std::int32_t);
  case NYRT_ARRAY_I64:
  case NYRT_ARRAY_F64:
  case NYRT_ARRAY_TEXT:
  case NYRT_ARRAY_TUPLE:
  case NYRT_ARRAY_ANYTHING:
    return sizeof(std::int64_t);
  case NYRT_ARRAY_BOOL:
  case NYRT_ARRAY_U8:
    return sizeof(std::uint8_t);
  default:
    return 0;
  }
}

bool allocation_size(std::uint64_t count, std::uint64_t stride,
                     std::size_t &size_out) noexcept {
  if (stride == 0)
    return false;

  std::uint64_t size_limit = std::numeric_limits<std::uint64_t>::max();
  if constexpr (sizeof(std::size_t) <= sizeof(std::uint64_t)) {
    size_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  }
  const std::uint64_t maximum_count = std::min(
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
      size_limit / stride);
  if (count > maximum_count)
    return false;

  size_out = static_cast<std::size_t>(count * stride);
  return true;
}

bool buffer_allocation_size(std::uint64_t count, std::uint64_t stride,
                            std::size_t &logical_size_out,
                            std::size_t &storage_size_out) noexcept {
  if (!allocation_size(count, stride, logical_size_out) ||
      logical_size_out > std::numeric_limits<std::size_t>::max() - stride) {
    return false;
  }

  // Keep one complete guard element after the language-visible buffer. A
  // logical one-past pointer is therefore still inside this allocation and
  // cannot have the same address as another live buffer's exact base.
  storage_size_out = logical_size_out + static_cast<std::size_t>(stride);
  return true;
}

std::uint64_t maximum_array_capacity(std::uint64_t stride) noexcept {
  if (stride == 0)
    return 0;
  const std::uint64_t abi_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
    return std::min(abi_limit,
                    std::numeric_limits<std::uint64_t>::max() / stride);
  }
  const std::uint64_t size_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  return std::min(abi_limit, size_limit / stride);
}

nyrt_array *checked_array(void *value, std::uint32_t kind) noexcept {
  if (value == nullptr)
    return nullptr;

  // An opaque handle must name a descriptor owned by the current runtime
  // context before any descriptor memory is read. This makes forged and stale
  // handles fail closed instead of turning validation itself into an invalid
  // memory access.
  nyrt_array *array = nullptr;
  for (const auto &candidate : context.arrays) {
    if (candidate.get() == value) {
      array = candidate.get();
      break;
    }
  }
  if (array == nullptr)
    return nullptr;

  const std::uint64_t expected_stride = stride_for_kind(kind);
  std::size_t expected_bytes = 0;
  if (!allocation_size(array->capacity, expected_stride, expected_bytes) ||
      array->magic != nyrt_array_magic || array->version != 1 ||
      array->kind != kind || array->stride != expected_stride ||
      array->length > array->capacity ||
      array->allocated_bytes != expected_bytes ||
      ((expected_bytes == 0) != (array->data == nullptr))) {
    return nullptr;
  }
  return array;
}

const nyrt_array *checked_array(const void *value,
                                std::uint32_t kind) noexcept {
  return checked_array(const_cast<void *>(value), kind);
}

nyrt_buffer *checked_buffer(void *value, std::uint32_t kind,
                            std::uint64_t *element_offset = nullptr) noexcept {
  if (value == nullptr)
    return nullptr;

  const std::uint64_t expected_stride = stride_for_kind(kind);
  if (expected_stride == 0)
    return nullptr;

  const auto address = reinterpret_cast<std::uintptr_t>(value);
  for (const auto &candidate : context.buffers) {
    std::size_t expected_bytes = 0;
    std::size_t expected_storage_bytes = 0;
    if (!candidate->active || candidate->magic != nyrt_buffer_magic ||
        candidate->version != 1 || candidate->kind != kind ||
        candidate->stride != expected_stride || candidate->data == nullptr ||
        !buffer_allocation_size(candidate->length, expected_stride,
                                expected_bytes, expected_storage_bytes) ||
        candidate->allocated_bytes != expected_storage_bytes) {
      continue;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(candidate->data);
    if (base > std::numeric_limits<std::uintptr_t>::max() - expected_bytes)
      continue;
    const auto end = base + expected_bytes;
    if (address < base || address > end)
      continue;

    const auto byte_offset = address - base;
    if (byte_offset % expected_stride != 0)
      return nullptr;
    const auto offset = byte_offset / expected_stride;
    if (offset > candidate->length)
      return nullptr;
    if (element_offset != nullptr)
      *element_offset = offset;
    return candidate.get();
  }
  return nullptr;
}

const nyrt_buffer *checked_buffer(const void *value,
                                  std::uint32_t kind,
                                  std::uint64_t *element_offset = nullptr) noexcept {
  return checked_buffer(const_cast<void *>(value), kind, element_offset);
}

bool resolve_buffer_index(std::uint64_t current, std::uint64_t length,
                          std::int64_t index,
                          std::uint64_t &resolved) noexcept {
  if (index < 0) {
    const auto magnitude =
        static_cast<std::uint64_t>(-(index + 1)) + std::uint64_t{1};
    if (magnitude > current)
      return false;
    resolved = current - magnitude;
  } else {
    const auto magnitude = static_cast<std::uint64_t>(index);
    if (magnitude > length - current)
      return false;
    resolved = current + magnitude;
  }
  return resolved < length;
}

bool move_buffer_offset(std::uint64_t current, std::uint64_t length,
                        std::int64_t amount, bool subtract,
                        std::uint64_t &resolved) noexcept {
  const bool negative = amount < 0;
  const auto magnitude = negative
                             ? static_cast<std::uint64_t>(-(amount + 1)) +
                                   std::uint64_t{1}
                             : static_cast<std::uint64_t>(amount);
  const bool move_backward = subtract ? !negative : negative;
  if (move_backward) {
    if (magnitude > current)
      return false;
    resolved = current - magnitude;
  } else {
    if (magnitude > length - current)
      return false;
    resolved = current + magnitude;
  }
  return true;
}

nyrt_array *make_array(std::uint32_t kind, std::uint64_t length) noexcept {
  const std::uint64_t stride = stride_for_kind(kind);
  std::size_t byte_count = 0;
  if (!allocation_size(length, stride, byte_count))
    return nullptr;

  std::unique_ptr<nyrt_array> array(new (std::nothrow) nyrt_array());
  if (!array)
    return nullptr;
  array->kind = kind;
  array->stride = stride;
  array->length = length;
  array->capacity = length;
  array->allocated_bytes = byte_count;
  if (byte_count != 0) {
    array->data = std::calloc(1, byte_count);
    if (array->data == nullptr)
      return nullptr;
  }

  nyrt_array *result = array.get();
  try {
    context.arrays.push_back(std::move(array));
    account_managed_bytes(byte_count);
    account_managed_bytes(sizeof(nyrt_array));
    context.profile.allocate(result, sizeof(nyrt_array) + byte_count, "array");
  } catch (...) {
    return nullptr;
  }
  return result;
}

void *make_buffer(std::uint32_t kind, std::uint64_t length) noexcept {
  // New array element kinds do not implicitly expand the raw-pointer ABI.
  if (kind != NYRT_ARRAY_I32 && kind != NYRT_ARRAY_I64 &&
      kind != NYRT_ARRAY_BOOL && kind != NYRT_ARRAY_U8)
    return nullptr;
  const std::uint64_t stride = stride_for_kind(kind);
  std::size_t byte_count = 0;
  std::size_t storage_bytes = 0;
  if (!buffer_allocation_size(length, stride, byte_count, storage_bytes))
    return nullptr;

  std::unique_ptr<nyrt_buffer> buffer(new (std::nothrow) nyrt_buffer());
  if (!buffer)
    return nullptr;
  buffer->kind = kind;
  buffer->stride = stride;
  buffer->length = length;
  buffer->allocated_bytes = storage_bytes;
  buffer->data = std::calloc(1, storage_bytes);
  if (buffer->data == nullptr)
    return nullptr;

  void *result = buffer->data;
  try {
    context.buffers.push_back(std::move(buffer));
    context.profile.allocate(context.buffers.back().get(), sizeof(nyrt_buffer) + storage_bytes, "buffer");
  } catch (...) {
    return nullptr;
  }
  return result;
}

void *allocate_owned(std::uint64_t requested_size,
                     std::string_view kind = "managed") noexcept {
  std::uint64_t size_limit = std::numeric_limits<std::uint64_t>::max();
  if constexpr (sizeof(std::size_t) <= sizeof(std::uint64_t)) {
    size_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
  }
  if (requested_size > size_limit)
    return nullptr;

  const std::size_t size = static_cast<std::size_t>(
      requested_size == 0 ? std::uint64_t{1} : requested_size);
  void *storage = std::calloc(1, size);
  if (storage == nullptr)
    return nullptr;
  try {
    context.allocations.push_back({storage, size, kind == "anything"});
    account_managed_bytes(size);
    context.profile.allocate(storage, size, kind);
  } catch (...) {
    std::free(storage);
    return nullptr;
  }
  return storage;
}

const char *own_string(std::string_view value) noexcept {
  if (value.size() == std::numeric_limits<std::size_t>::max())
    return nullptr;
  auto *storage = static_cast<char *>(
      allocate_owned(static_cast<std::uint64_t>(value.size()) + 1U, "text"));
  if (storage == nullptr)
    return nullptr;
  if (!value.empty())
    std::memcpy(storage, value.data(), value.size());
  storage[value.size()] = '\0';
  return storage;
}

std::string checked_dynamic_descriptor(const char *type, std::uint64_t length) {
  if (!type || !length || length > 1024U * 1024U)
    nyrt_fail("NIL native runtime error: invalid anything type descriptor\n");
  std::string descriptor(type, static_cast<std::size_t>(length));
  if (descriptor.find('\0') != std::string::npos ||
      ny::canonical_dynamic_type(descriptor) != descriptor ||
      !ny::dynamic_type_syntax_supported(descriptor))
    nyrt_fail("NIL native runtime error: invalid anything type descriptor\n");
  return descriptor;
}

const nyrt_any_box &checked_dynamic_box(const void *box) noexcept {
  for (const auto &allocation : context.allocations) {
    if (allocation.data != box || !allocation.dynamic_box) continue;
    if (allocation.size < sizeof(nyrt_any_box)) break;
    const auto &value = *static_cast<const nyrt_any_box *>(box);
    if (!value.type_length || value.type_length != allocation.size - sizeof(value)) break;
    return value;
  }
  nyrt_fail("NIL native runtime error: invalid, absent, or foreign anything box\n");
}

void validate_array_value(std::uint32_t kind, std::uint64_t value) noexcept {
  if (kind == NYRT_ARRAY_BOOL && value > 1)
    nyrt_fail("NIL native runtime error: boolean requires 0 or 1\n");
  if (kind == NYRT_ARRAY_ANYTHING)
    (void)checked_dynamic_box(reinterpret_cast<const void *>(static_cast<std::uintptr_t>(value)));
}

void set_error(const char **error_out, std::string_view message) noexcept {
  if (error_out != nullptr) {
    *error_out = own_string(message);
    if (*error_out == nullptr)
      *error_out = "NIL native runtime: allocation failed";
  }
}

template <typename Integer>
std::int32_t try_decimal_integer(const char *text, Integer *value_out,
                                const char **error_out,
                                const char *operation) noexcept {
  if (value_out != nullptr) *value_out = 0;
  if (error_out != nullptr) *error_out = nullptr;
  const auto fail = [&](const char *reason) {
    // The operation and reason are fixed internal strings, never user input.
    // Format without host allocation before copying into managed error storage.
    char message[160] = {};
    std::snprintf(message, sizeof(message), "%s: %s", operation, reason);
    set_error(error_out, message);
    return std::int32_t{0};
  };
  if (text == nullptr || value_out == nullptr)
    return fail("invalid arguments");
  const auto parsed = ny::detail::parse_signed_integer(text, 10, false);
  if (!parsed.ok) return fail(parsed.error);
  if (parsed.value < std::numeric_limits<Integer>::min() ||
      parsed.value > std::numeric_limits<Integer>::max())
    return fail("value is outside the i32 range");
  *value_out = static_cast<Integer>(parsed.value);
  return 1;
}

std::uint64_t load_value(const nyrt_array &array,
                         std::uint64_t index) noexcept {
  switch (array.kind) {
  case NYRT_ARRAY_I32:
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(
        static_cast<const std::int32_t *>(array.data)[index]));
  case NYRT_ARRAY_I64:
    return static_cast<std::uint64_t>(
        static_cast<const std::int64_t *>(array.data)[index]);
  case NYRT_ARRAY_F64:
  case NYRT_ARRAY_TEXT:
  case NYRT_ARRAY_TUPLE:
  case NYRT_ARRAY_ANYTHING:
    return static_cast<const std::uint64_t *>(array.data)[index];
  case NYRT_ARRAY_BOOL:
  case NYRT_ARRAY_U8:
    return static_cast<const std::uint8_t *>(array.data)[index];
  default:
    return 0;
  }
}

void store_value(nyrt_array &array, std::uint64_t index,
                 std::uint64_t value) noexcept {
  switch (array.kind) {
  case NYRT_ARRAY_I32: {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    std::int32_t stored = 0;
    static_assert(sizeof(bits) == sizeof(stored));
    std::memcpy(&stored, &bits, sizeof(stored));
    static_cast<std::int32_t *>(array.data)[index] = stored;
    break;
  }
  case NYRT_ARRAY_I64: {
    std::int64_t stored = 0;
    static_assert(sizeof(value) == sizeof(stored));
    std::memcpy(&stored, &value, sizeof(stored));
    static_cast<std::int64_t *>(array.data)[index] = stored;
    break;
  }
  case NYRT_ARRAY_BOOL:
    validate_array_value(array.kind, value);
    static_cast<std::uint8_t *>(array.data)[index] = static_cast<std::uint8_t>(value);
    break;
  case NYRT_ARRAY_F64:
  case NYRT_ARRAY_TEXT:
  case NYRT_ARRAY_TUPLE:
  case NYRT_ARRAY_ANYTHING:
    // f64 is an IEEE bit pattern; text is a context/static-lifetime address.
    // Neither is an integer conversion, nor does array growth own/free text.
    static_cast<std::uint64_t *>(array.data)[index] = value;
    break;
  case NYRT_ARRAY_U8:
    static_cast<std::uint8_t *>(array.data)[index] =
        static_cast<std::uint8_t>(value);
    break;
  default:
    break;
  }
}

bool reserve(nyrt_array &array, std::uint64_t capacity) noexcept {
  if (capacity <= array.capacity)
    return true;
  std::size_t byte_count = 0;
  if (!allocation_size(capacity, array.stride, byte_count))
    return false;
  void *data = std::realloc(array.data, byte_count);
  if (data == nullptr)
    return false;
  array.data = data;
  array.capacity = capacity;
  account_managed_bytes(byte_count - array.allocated_bytes);
  array.allocated_bytes = byte_count;
  context.profile.resize(&array, sizeof(nyrt_array) + byte_count);
  return true;
}

template <typename Integer>
const char *integer_to_text(Integer value) noexcept {
  char buffer[std::numeric_limits<Integer>::digits10 + 3] = {};
  const auto conversion = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (conversion.ec != std::errc{})
    return nullptr;
  return own_string(std::string_view(
      buffer, static_cast<std::size_t>(conversion.ptr - buffer)));
}

} // namespace

namespace {
template <typename Invoke>
std::int32_t ui_number_result(std::int64_t *value, const char **error,
                             Invoke invoke) noexcept {
  if (!value || !error) return 0;
  *value = 0; *error = nullptr;
  try {
    auto result = invoke();
    if (!result) { set_error(error, result.error); return 0; }
    *value = result.number;
    return 1;
  } catch (const std::invalid_argument &invalid) { set_error(error, invalid.what()); return 0; }
  catch (...) { *error = "ui: host allocation failed"; return 0; }
}
template <typename Invoke>
std::int32_t ui_text_result(const char **value, const char **error,
                           Invoke invoke) noexcept {
  if (!value || !error) return 0;
  *value = nullptr; *error = nullptr;
  try {
    auto result = invoke();
    if (!result) { set_error(error, result.error); return 0; }
    // The snapshot may be released immediately after this call. Never return
    // provider storage, even when its lifetime presently spans the caller.
    *value = own_string(result.text);
    if (!*value) { *error = "ui: host allocation failed"; return 0; }
    return 1;
  } catch (const std::invalid_argument &invalid) { set_error(error, invalid.what()); return 0; }
  catch (...) { *error = "ui: host allocation failed"; return 0; }
}
const char *ui_text_argument(const char *text) {
  if (!text) throw std::invalid_argument("ui: expected text argument");
  return text;
}
} // namespace

extern "C" {

std::int64_t nyrt_ui_v1_available() noexcept { return ny::ui::available() ? 1 : 0; }
std::int32_t nyrt_ui_v1_window(const char *title, std::int64_t width, std::int64_t height,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.window(ui_text_argument(title), width, height); });
}
std::int32_t nyrt_ui_v1_widget(const char *kind, std::int64_t parent, const char *label,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.widget(ui_text_argument(kind), parent, ui_text_argument(label)); });
}
std::int32_t nyrt_ui_v1_grid_attach(std::int64_t grid, std::int64_t child, std::int64_t column, std::int64_t row, std::int64_t column_span, std::int64_t row_span,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.grid_attach(grid, child, column, row, column_span, row_span); });
}
std::int32_t nyrt_ui_v1_set_text(std::int64_t handle, const char *text,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.set_text(handle, ui_text_argument(text)); });
}
std::int32_t nyrt_ui_v1_read_text(std::int64_t handle,
    const char **value, const char **error) noexcept {
  return ui_text_result(value, error, [&] { return context.ui.read_text(handle); });
}
std::int32_t nyrt_ui_v1_set_number(std::int64_t handle, const char *property, std::int64_t number,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.set_number(handle, ui_text_argument(property), number); });
}
std::int32_t nyrt_ui_v1_read_number(std::int64_t handle, const char *property,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.read_number(handle, ui_text_argument(property)); });
}
std::int32_t nyrt_ui_v1_activate(std::int64_t handle,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.activate(handle); });
}
std::int32_t nyrt_ui_v1_poll(std::int64_t window,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.poll(window); });
}
std::int32_t nyrt_ui_v1_event_number(std::int64_t snapshot, const char *field,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.event_number(snapshot, ui_text_argument(field)); });
}
std::int32_t nyrt_ui_v1_event_text(std::int64_t snapshot,
    const char **value, const char **error) noexcept {
  return ui_text_result(value, error, [&] { return context.ui.event_text(snapshot); });
}
std::int32_t nyrt_ui_v1_release_event(std::int64_t snapshot,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.release_event(snapshot); });
}
std::int32_t nyrt_ui_v1_destroy_widget(std::int64_t handle,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.destroy_widget(handle); });
}
std::int32_t nyrt_ui_v1_close(std::int64_t window,
    std::int64_t *value, const char **error) noexcept {
  return ui_number_result(value, error, [&] { return context.ui.close(window); });
}



std::int64_t nyrt_db2_available(const char *driver) noexcept {
  try { return driver && ny::database::available(driver) ? 1 : 0; }
  catch (...) { return 0; }
}
std::int32_t nyrt_db2_open(const char *driver, const char *address,
    std::int64_t timeout_ms, std::int64_t *value, const char **error) noexcept {
  if (!value || !error) return 0;
  *value = 0; *error = nullptr;
  try {
    auto result = context.databases.open(driver ? driver : "", address ? address : "", timeout_ms);
    if (!result) { set_error(error, result.error); return 0; }
    *value = result.number; return 1;
  } catch (...) { *error = "database: host allocation failed"; return 0; }
}
std::int32_t nyrt_db2_execute(std::int64_t connection, const char *sql,
    const void *parameters, const void *nulls, std::int64_t timeout_ms,
    std::int64_t *value, const char **error) noexcept {
  if (!value || !error) return 0;
  *value = 0; *error = nullptr;
  try {
    auto count = nyrt_array_length(parameters, NYRT_ARRAY_TEXT);
    auto flag_count = nyrt_array_length(nulls, NYRT_ARRAY_BOOL);
    if (count < 0 || flag_count < 0) {
      *error = "database: expected array<text> and array<bool> bindings"; return 0;
    }
    if (count > 256 || count != flag_count) {
      *error = "database: parameter and NULL arrays must have equal lengths up to 256"; return 0;
    }
    std::vector<std::string> values;
    std::vector<bool> flags;
    for (std::int64_t i = 0; i < count; ++i) {
      const char *text = reinterpret_cast<const char *>(nyrt_array_get(parameters, NYRT_ARRAY_TEXT, i));
      values.emplace_back(text ? text : "");
      flags.push_back(nyrt_array_get(nulls, NYRT_ARRAY_BOOL, i) != 0);
    }
    auto result = context.databases.execute(connection, sql ? sql : "", values, flags, timeout_ms);
    if (!result) { set_error(error, result.error); return 0; }
    *value = result.number; return 1;
  } catch (...) { *error = "database: host allocation failed"; return 0; }
}
std::int32_t nyrt_db2_read_i64(std::int64_t operation, std::int64_t handle,
    std::int64_t row, std::int64_t column, std::int64_t *value,
    const char **error) noexcept {
  if (!value || !error) return 0;
  *value = 0; *error = nullptr;
  try {
    if (operation < 1 || operation > 6) { *error = "database: invalid integer operation"; return 0; }
    auto result = context.databases.read(static_cast<ny::database::Read>(operation), handle, row, column);
    if (!result) { set_error(error, result.error); return 0; }
    *value = result.number; return 1;
  } catch (...) { *error = "database: host allocation failed"; return 0; }
}
std::int32_t nyrt_db2_read_text(std::int64_t operation, std::int64_t handle,
    std::int64_t row, std::int64_t column, const char **value,
    const char **error) noexcept {
  if (!value || !error) return 0;
  *value = nullptr; *error = nullptr;
  try {
    if (operation != 7 && operation != 8) { *error = "database: invalid text operation"; return 0; }
    auto result = context.databases.read(static_cast<ny::database::Read>(operation), handle, row, column);
    if (!result) { set_error(error, result.error); return 0; }
    *value = own_string(result.text);
    if (!*value) { *error = "database: host allocation failed"; return 0; }
    return 1;
  } catch (...) { *error = "database: host allocation failed"; return 0; }
}

std::uint32_t nyrt_abi_version() noexcept { return 1; }

std::int64_t nyrt_time_now_ms() noexcept {
  return ny::time_support::now_ms();
}

void nyrt_sleep_ms(std::int64_t milliseconds) noexcept {
  try {
    ny::time_support::sleep_ms(milliseconds);
  } catch (...) {
    nyrt_fail("NIL native runtime error: TIME_SLEEP host wait failed\n");
  }
}

void nyrt_parallel_fail_v1(const char *message) noexcept {
  if (parallel_failure == nullptr)
    parallel_failure = message == nullptr ? "native scalar worker failed" : message;
}

std::int32_t nyrt_parallel_failed_v1() noexcept {
  return parallel_failure == nullptr ? 0 : 1;
}

void *nyrt_parallel_map_v1(const void *inputs, std::uint32_t input_kind,
                          std::uint32_t output_kind,
                          nyrt_parallel_worker_v1 worker) noexcept {
  try {
    const auto scalar_kind = [](std::uint32_t kind) {
      return kind >= NYRT_ARRAY_I32 && kind <= NYRT_ARRAY_F64;
    };
    const auto *source = checked_array(inputs, input_kind);
    if (!source || !scalar_kind(input_kind) || !scalar_kind(output_kind) || !worker)
      throw std::runtime_error("PARALLEL_MAP: invalid scalar array or worker");
    const auto count = static_cast<std::size_t>(source->length);
    (void)ny::parallel_support::worker_count(count);
    std::vector<std::uint64_t> copied_inputs(count), results(count);
    for (std::size_t index = 0; index < count; ++index)
      copied_inputs[index] = nyrt_array_get(inputs, input_kind, static_cast<std::int64_t>(index));
    ny::parallel_support::for_each(count, [&](std::size_t, std::size_t index) {
      nyrt_context_begin(0, nullptr);
      struct cleanup {
        ~cleanup() { nyrt_context_end(); parallel_failure = nullptr; }
      } cleanup;
      parallel_failure = nullptr;
      results[index] = worker(copied_inputs[index]);
      if (parallel_failure != nullptr)
        throw std::runtime_error(parallel_failure);
    });
    void *result = nyrt_array_new(output_kind, static_cast<std::int64_t>(count));
    if (!result) throw std::runtime_error("PARALLEL_MAP: could not allocate results");
    for (std::size_t index = 0; index < count; ++index)
      if (!nyrt_array_set(result, output_kind, static_cast<std::int64_t>(index), results[index]))
        throw std::runtime_error("PARALLEL_MAP: could not store scalar result");
    return result;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "NIL native runtime error: %s\n", error.what());
  } catch (...) {
    std::fputs("NIL native runtime error: PARALLEL_MAP failed\n", stderr);
  }
  nyrt_fail(nullptr);
}

void nyrt_context_begin(int argc, const char *const *argv) noexcept {
  nyrt_context_end();
  if (!context.profile.begin("native"))
    nyrt_fail("NIL native runtime error: could not start memory capture\n");
  if (argc < 0 || (argc > 0 && argv == nullptr))
    nyrt_fail("NIL native runtime error: invalid process argument vector\n");
  if (argc == 0)
    return;
  try {
    context.arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
      context.arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]);
  } catch (...) {
    (void)context.profile.finish("failed", false);
    nyrt_context_end();
    nyrt_fail("NIL native runtime error: could not initialize process "
              "arguments\n");
  }
}

void nyrt_context_end() noexcept {
  // Capture application retention before invocation-owned storage is torn down.
  if (!context.profile.finish())
    nyrt_fail("NIL native runtime error: memory capture failed\n");
  context.databases.clear();
  context.ui.clear();
  nyrt_owned_v1_unwind(0);
  context.pending_fault.reset();
  std::vector<runtime_context::fault_frame>().swap(context.fault_frames);
  context.next_fault_frame = 1;
  std::vector<std::unique_ptr<nyrt_array>>().swap(context.arrays);
  std::vector<std::unique_ptr<nyrt_buffer>>().swap(context.buffers);
  for (const auto &allocation : context.allocations)
    std::free(allocation.data);
  std::vector<runtime_context::allocation>().swap(context.allocations);
  std::vector<runtime_context::roots>().swap(context.frames);
  context.allocated_since_collection = 0;
  std::vector<std::string>().swap(context.arguments);
}

void nyrt_profile_site(const char *file, const char *function,
                       std::uint64_t line) noexcept {
  context.profile.site(file ? file : "", function ? function : "", line);
}

[[noreturn]] void nyrt_fail(const char *message) noexcept {
  context.ui.clear();
  (void)context.profile.finish("failed", false);
  if (message != nullptr)
    std::fputs(message, stderr);
  std::fflush(nullptr);
  std::exit(1);
}

const char *nyrt_text_substr(const char *text, std::int64_t start,
                           std::int64_t count) noexcept {
  if (text == nullptr)
    return nullptr;
  const std::string_view value(text);
  const auto begin = static_cast<std::uint64_t>(std::max<std::int64_t>(start, 0));
  if (begin >= value.size() || count <= 0)
    return own_string("");
  // Subtract before clamping; start + count must never overflow signed i64.
  const auto length = std::min<std::uint64_t>(
      static_cast<std::uint64_t>(count), value.size() - begin);
  return own_string(value.substr(static_cast<std::size_t>(begin),
                                static_cast<std::size_t>(length)));
}

const char *nyrt_text_i64(std::int64_t value) noexcept {
  try {
    if (const auto *result = own_string(ny::scalar_text(value))) return result;
  } catch (...) {}
  nyrt_fail("NIL native runtime error: integer formatting failed\n");
}

const char *nyrt_text_f64(double value) noexcept {
  try {
    if (const auto *result = own_string(ny::scalar_text(value))) return result;
  } catch (...) {}
  nyrt_fail("NIL native runtime error: floating-point formatting failed\n");
}

const char *nyrt_text_bool(std::int64_t value) noexcept {
  return nyrt_bool_i64(value) == 0 ? "false" : "true";
}

std::int64_t nyrt_bool_i64(std::int64_t value) noexcept {
  if (value != 0 && value != 1)
    nyrt_fail("NIL native runtime error: boolean requires 0 or 1\n");
  return value;
}

std::int64_t nyrt_bool_f64(double value) noexcept {
  if (value != 0.0 && value != 1.0)
    nyrt_fail("NIL native runtime error: boolean requires 0 or 1\n");
  return value == 1.0 ? 1 : 0;
}

std::int64_t nyrt_bool_text(const char *value) noexcept {
  if (value) {
    char normalized[6] = {};
    std::size_t length = 0;
    while (length < sizeof(normalized) - 1 && value[length]) {
      normalized[length] = static_cast<char>(std::tolower(
          static_cast<unsigned char>(value[length])));
      ++length;
    }
    if (value[length] == '\0') {
      const std::string_view text(normalized, length);
      if (text == "true" || text == "yes" || text == "1" || text == "on") return 1;
      if (text == "false" || text == "no" || text == "0" || text == "off") return 0;
    }
  }
  nyrt_fail("NIL native runtime error: text is not a recognized bool value; expected true/false, yes/no, 1/0, or on/off (case-insensitive, no surrounding whitespace)\n");
}

void *nyrt_any_v1_box(const char *type, std::uint64_t length,
                      std::uint64_t payload) noexcept {
  try {
    const auto descriptor = checked_dynamic_descriptor(type, length);
    if (descriptor == "bool") (void)nyrt_bool_i64(static_cast<std::int64_t>(payload));
    if ((descriptor == "u8" && payload > 255) ||
        (descriptor == "i32" &&
         (static_cast<std::int64_t>(payload) < std::numeric_limits<std::int32_t>::min() ||
          static_cast<std::int64_t>(payload) > std::numeric_limits<std::int32_t>::max())))
      nyrt_fail("NIL native runtime error: anything scalar payload does not match its declared type\n");
    const bool scalar = descriptor == "bool" || descriptor == "i32" ||
        descriptor == "i64" || descriptor == "u8" || descriptor == "f64";
    if (!scalar && descriptor.rfind("ptr<", 0) != 0 && payload == 0)
      nyrt_fail("NIL native runtime error: anything requires an initialized value; absent handles cannot be boxed\n");
    if (descriptor.rfind("fn(", 0) == 0)
      nyrt_fail("NIL native runtime error: native function values cannot be boxed\n");
    auto *box = static_cast<nyrt_any_box *>(allocate_owned(sizeof(nyrt_any_box) + length, "anything"));
    if (!box) nyrt_fail("NIL native runtime error: could not allocate anything box\n");
    box->payload = payload;
    box->type_length = length;
    std::memcpy(box + 1, descriptor.data(), descriptor.size());
    return box;
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not allocate anything descriptor\n");
  }
}

std::int32_t nyrt_any_v1_is_type(const void *box, const char *type,
                                std::uint64_t length) noexcept {
  try {
    const auto &value = checked_dynamic_box(box);
    const auto descriptor = checked_dynamic_descriptor(type, length);
    return value.type_length == length &&
           std::memcmp(&value + 1, descriptor.data(), descriptor.size()) == 0;
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not inspect anything descriptor\n");
  }
}

std::uint64_t nyrt_any_v1_unbox(const void *box, const char *type,
                                std::uint64_t length) noexcept {
  if (!nyrt_any_v1_is_type(box, type, length)) {
    try {
      const auto &value = checked_dynamic_box(box);
      const std::string actual(reinterpret_cast<const char *>(&value + 1), value.type_length);
      const std::string expected(type, length);
      const auto message = "NIL native runtime error: anything type mismatch: expected '" +
          expected + "', got '" + actual + "'\n";
      nyrt_fail(message.c_str());
    } catch (...) {
      nyrt_fail("NIL native runtime error: anything type mismatch\n");
    }
  }
  return checked_dynamic_box(box).payload;
}

[[noreturn]] void nyrt_panic(const char *message) noexcept {
  std::fputs("NIL native runtime error: panic: ", stderr);
  if (message != nullptr)
    std::fputs(message, stderr);
  std::fputc('\n', stderr);
  nyrt_fail(nullptr);
}

void *nyrt_alloc(std::uint64_t size) noexcept { return allocate_owned(size); }

std::int32_t nyrt_free(void *pointer) noexcept {
  if (pointer == nullptr)
    return 1;
  const auto allocation = std::find_if(context.allocations.begin(),
      context.allocations.end(), [pointer](const auto &item) { return item.data == pointer; });
  if (allocation == context.allocations.end())
    return 0;
  context.profile.release(allocation->data);
  std::free(allocation->data);
  context.allocations.erase(allocation);
  return 1;
}

void nyrt_roots_enter(const void *const *slots, std::uint64_t count) noexcept {
  if (slots == nullptr || count > std::numeric_limits<std::size_t>::max())
    nyrt_fail("NIL native runtime error: invalid managed root frame\n");
  try {
    context.frames.push_back({slots, count});
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not register managed roots\n");
  }
}

void nyrt_roots_leave(const void *const *slots) noexcept {
  if (context.frames.empty() || context.frames.back().slots != slots)
    nyrt_fail("NIL native runtime error: unbalanced managed root frame\n");
  context.frames.pop_back();
}

std::uint64_t nyrt_fault_v1_frame_enter() noexcept {
  if (context.pending_fault || context.next_fault_frame == 0)
    invalid_fault_protocol();
  try {
    const auto identity = context.next_fault_frame++;
    context.fault_frames.push_back({identity, {}});
    return identity;
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not register fault frame\n");
  }
}

void nyrt_fault_v1_frame_leave(std::uint64_t frame) noexcept {
  const auto &current = current_fault_frame();
  if (current.identity != frame || (!context.pending_fault && !current.saved.empty()))
    invalid_fault_protocol();
  context.fault_frames.pop_back();
}

std::uint64_t nyrt_fault_v1_saved_depth() noexcept {
  return current_fault_frame().saved.size();
}

void nyrt_fault_v1_unwind(const std::uint64_t *saved_slots,
                         std::uint64_t saved_count) noexcept {
  auto &saved = current_fault_frame().saved;
  if (!context.pending_fault || saved_count > saved.size() ||
      (saved_count != 0 && saved_slots == nullptr))
    invalid_fault_protocol();
  for (std::uint64_t index = 0; index < saved_count; ++index) {
    if (std::none_of(saved.begin(), saved.end(), [&](const auto &item) {
          return item.slot == saved_slots[index]; })) invalid_fault_protocol();
    for (std::uint64_t earlier = 0; earlier < index; ++earlier)
      if (saved_slots[earlier] == saved_slots[index]) invalid_fault_protocol();
  }
  saved.erase(std::remove_if(saved.begin(), saved.end(), [&](const auto &item) {
    for (std::uint64_t index = 0; index < saved_count; ++index)
      if (item.slot == saved_slots[index]) return false;
    return true;
  }), saved.end());
}

std::int32_t nyrt_fault_v1_pending() noexcept {
  return context.pending_fault.has_value() ? 1 : 0;
}

void nyrt_fault_v1_raise(const char *type, std::uint64_t type_length,
                         void *payload, std::int64_t line) noexcept {
  (void)current_fault_frame();
  if (context.pending_fault || payload == nullptr || line < 0)
    invalid_fault_protocol();
  const auto identity = fault_type_span(type, type_length);
  try {
    context.pending_fault.emplace(runtime_context::fault{
        std::string(identity), payload, line});
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not retain fault payload\n");
  }
}

void nyrt_fault_v1_save(std::uint64_t slot) noexcept {
  auto &saved = current_fault_frame().saved;
  if (!context.pending_fault) invalid_fault_protocol();
  for (const auto &existing : saved)
    if (existing.slot == slot) invalid_fault_protocol();
  try {
    saved.push_back({slot, std::move(*context.pending_fault)});
    context.pending_fault.reset();
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not suspend fault payload\n");
  }
}

std::int32_t nyrt_fault_v1_match(std::uint64_t slot, const char *type,
                                std::uint64_t type_length) noexcept {
  if (context.pending_fault) invalid_fault_protocol();
  return find_saved_fault(slot).value.type == fault_type_span(type, type_length) ? 1 : 0;
}

void *nyrt_fault_v1_bind(std::uint64_t slot, const char *type,
                        std::uint64_t type_length) noexcept {
  if (!nyrt_fault_v1_match(slot, type, type_length)) invalid_fault_protocol();
  return find_saved_fault(slot).value.payload;
}

void nyrt_fault_v1_drop(std::uint64_t slot) noexcept {
  auto &saved = current_fault_frame().saved;
  if (context.pending_fault) invalid_fault_protocol();
  const auto found = std::find_if(saved.begin(), saved.end(),
      [&](const auto &item) { return item.slot == slot; });
  if (found == saved.end()) invalid_fault_protocol();
  saved.erase(found);
}

void nyrt_fault_v1_resume(std::uint64_t slot, std::int32_t consume) noexcept {
  if (context.pending_fault || (consume != 0 && consume != 1))
    invalid_fault_protocol();
  auto &saved = find_saved_fault(slot);
  try {
    context.pending_fault = consume ? std::move(saved.value) : saved.value;
    if (consume) {
      auto &slots = current_fault_frame().saved;
      slots.erase(std::find_if(slots.begin(), slots.end(),
          [&](const auto &item) { return item.slot == slot; }));
    }
  } catch (...) {
    nyrt_fail("NIL native runtime error: could not resume fault payload\n");
  }
}

void nyrt_fault_v1_report_uncaught() noexcept {
  if (!context.pending_fault || !context.fault_frames.empty())
    invalid_fault_protocol();
  const auto &fault = *context.pending_fault;
  std::fputs("Ny uncaught fault: ", stderr);
  for (const auto byte : fault.type) {
    const auto value = static_cast<unsigned char>(byte);
    if (value < 0x20 || value == 0x7f)
      std::fprintf(stderr, "\\x%02x", static_cast<unsigned>(value));
    else
      std::fputc(value, stderr);
  }
  std::fprintf(stderr, " at line %lld\n", static_cast<long long>(fault.line));
  std::fflush(stderr);
  if (!context.profile.finish("failed", false))
    nyrt_fail("NIL native runtime error: memory capture failed\n");
}

std::uint64_t nyrt_managed_bytes() noexcept {
  std::uint64_t total = 0;
  for (const auto &item : context.allocations) total += item.size;
  for (const auto &item : context.arrays)
    total += sizeof(nyrt_array) + item->allocated_bytes;
  return total;
}

std::uint64_t nyrt_managed_objects() noexcept {
  return context.allocations.size() + context.arrays.size();
}

void nyrt_collect() noexcept {
  // Uninstrumented embedding and older SDK objects keep their historical
  // invocation lifetime. Never guess at arbitrary C/C++ machine-stack roots.
  if (context.frames.empty()) return;
  const bool profiling = context.profile.enabled();
  const auto collection_start = profiling ? std::chrono::steady_clock::now()
      : std::chrono::steady_clock::time_point{};
  const auto bytes_before = profiling ? nyrt_managed_bytes() : 0;
  const auto objects_before = profiling ? nyrt_managed_objects() : 0;
  try {
    struct object {
      void *data;
      std::size_t size;
      nyrt_array *array;
      bool marked = false;
    };
    std::vector<object> objects;
    objects.reserve(context.allocations.size() + context.arrays.size());
    std::map<std::uintptr_t, std::size_t> addresses;
    for (const auto &item : context.allocations) {
      addresses.emplace(reinterpret_cast<std::uintptr_t>(item.data), objects.size());
      objects.push_back({item.data, item.size, nullptr});
    }
    for (const auto &item : context.arrays) {
      addresses.emplace(reinterpret_cast<std::uintptr_t>(item.get()), objects.size());
      objects.push_back({item.get(), sizeof(nyrt_array), item.get()});
    }
    std::vector<std::size_t> pending;
    const auto mark = [&](std::uint64_t word) {
      const auto address = static_cast<std::uintptr_t>(word);
      auto found = addresses.upper_bound(address);
      if (found == addresses.begin()) return;
      --found;
      auto &item = objects[found->second];
      if (address - found->first >= item.size || item.marked) return;
      item.marked = true;
      pending.push_back(found->second);
    };
    const auto scan = [&](const void *data, std::size_t size) {
      const auto *bytes = static_cast<const unsigned char *>(data);
      for (std::size_t offset = 0; size >= sizeof(std::uint64_t) &&
           offset <= size - sizeof(std::uint64_t); offset += sizeof(std::uint64_t)) {
        std::uint64_t word;
        std::memcpy(&word, bytes + offset, sizeof(word));
        mark(word);
      }
    };
    for (const auto &frame : context.frames)
      for (std::uint64_t index = 0; index < frame.count; ++index) {
        if (frame.slots[index] == nullptr)
          nyrt_fail("NIL native runtime error: missing managed root slot\n");
        scan(frame.slots[index], sizeof(std::uint64_t));
      }
    if (context.pending_fault)
      mark(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(context.pending_fault->payload)));
    for (const auto &frame : context.fault_frames)
      for (const auto &saved : frame.saved)
        mark(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(saved.value.payload)));
    while (!pending.empty()) {
      const auto index = pending.back();
      pending.pop_back();
      const auto &item = objects[index];
      if (item.array) {
        if (item.array->kind == NYRT_ARRAY_TEXT ||
            item.array->kind == NYRT_ARRAY_TUPLE ||
            item.array->kind == NYRT_ARRAY_ANYTHING)
          scan(item.array->data, item.array->length * item.array->stride);
      } else {
        // Object/enum layouts use aligned word slots. Conservatively retaining
        // a numeric word that happens to equal an owned address is safe; only
        // known owned ranges are ever followed or dereferenced.
        scan(item.data, item.size);
      }
    }
    const auto live = [&](const void *data) {
      return objects[addresses.at(reinterpret_cast<std::uintptr_t>(data))].marked;
    };
    context.allocations.erase(std::remove_if(context.allocations.begin(),
        context.allocations.end(), [&](const auto &item) {
          if (live(item.data)) return false;
          context.profile.release(item.data);
          std::free(item.data);
          return true;
        }), context.allocations.end());
    context.arrays.erase(std::remove_if(context.arrays.begin(), context.arrays.end(),
        [&](const auto &item) {
          if (live(item.get())) return false;
          context.profile.release(item.get());
          return true;
        }), context.arrays.end());
    context.allocated_since_collection = 0;
    if (profiling) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - collection_start).count();
      context.profile.collection(static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed, 0)),
          bytes_before - nyrt_managed_bytes(), objects_before - nyrt_managed_objects());
    }
  } catch (...) {
    // All tracing allocations precede sweep. An exhausted tracing workspace
    // must preserve the heap, never turn a partial mark into reclamation.
    nyrt_fail("NIL native runtime error: could not trace managed heap\n");
  }
}

void nyrt_safepoint() noexcept {
  if (context.allocated_since_collection >= 4ULL * 1024ULL * 1024ULL)
    nyrt_collect();
}

std::int64_t nyrt_arg_count() noexcept {
  if (context.arguments.size() >
      static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    return 0;
  return static_cast<std::int64_t>(context.arguments.size());
}

const char *nyrt_arg(std::int64_t index) noexcept {
  if (index < 0 ||
      static_cast<std::uint64_t>(index) >= context.arguments.size())
    return nullptr;
  return context.arguments[static_cast<std::size_t>(index)].c_str();
}

[[noreturn]] void nyrt_exit(std::int64_t code) noexcept {
  if (code < 0 || code > 255)
    nyrt_fail("NIL native runtime error: std_sys.exit: code must be between 0 "
              "and 255\n");
  if (!context.profile.finish("exited", true))
    nyrt_fail("NIL native runtime error: memory capture failed\n");
  std::fflush(nullptr);
  std::exit(static_cast<int>(code));
}

void nyrt_write_stdout(const char *text, std::int32_t newline) noexcept {
  if (text != nullptr)
    std::fputs(text, stdout);
  if (newline != 0)
    std::fputc('\n', stdout);
}

void nyrt_write_stderr(const char *text, std::int32_t newline) noexcept {
  if (text != nullptr)
    std::fputs(text, stderr);
  if (newline != 0)
    std::fputc('\n', stderr);
}

const char *nyrt_read_line() noexcept {
  // Console output uses C stdio. Publish pending prompts even if an embedder
  // has disabled C++ stream synchronization or changed std::cin's tie.
  std::fflush(stdout);
  try {
    std::string line;
    if (!std::getline(std::cin, line))
      nyrt_fail("NIL native runtime error: console.read_line: input is unavailable or reached EOF\n");
    // Native text is a NUL-terminated ABI. Never turn a valid input prefix
    // into an apparently successful read by silently dropping the suffix.
    if (line.find('\0') != std::string::npos)
      nyrt_fail("NIL native runtime error: console.read_line: embedded NUL is unsupported by native text\n");
    if (const char *result = own_string(line))
      return result;
  } catch (const std::ios_base::failure &) {
    nyrt_fail("NIL native runtime error: console.read_line: input is unavailable or reached EOF\n");
  } catch (...) {
    // Allocation failures also stay inside the C ABI boundary.
  }
  nyrt_fail("NIL native runtime error: console.read_line: could not allocate input text\n");
}

void nyrt_flush_stdout() noexcept { std::fflush(stdout); }

void *nyrt_nildata_v1_request(const char *operation, const char *source,
                              const char *argument, std::int64_t index) noexcept {
  try {
    const auto result = nil::data::request(operation ? operation : "",
        source ? source : "", argument ? argument : "", index);
    auto *array = nyrt_array_new(NYRT_ARRAY_TEXT,
                                 static_cast<std::int64_t>(result.size()));
    if (!array) nyrt_fail("NIL native runtime error: NILData result allocation failed\n");
    for (std::size_t i = 0; i < result.size(); ++i) {
      const auto *text = own_string(result[i]);
      if (!text || !nyrt_array_set(array, NYRT_ARRAY_TEXT, static_cast<std::int64_t>(i),
                                   reinterpret_cast<std::uintptr_t>(text)))
        nyrt_fail("NIL native runtime error: NILData result allocation failed\n");
    }
    return array;
  } catch (...) {}
  nyrt_fail("NIL native runtime error: NILData request failed\n");
}

const char *nyrt_regex_validate(const char *pattern) noexcept {
  try {
    const auto error = ny::patterns::validate(pattern ? pattern : "");
    if (error.empty()) return "";
    if (const auto *owned = own_string(error)) return owned;
  } catch (...) {}
  nyrt_fail("NIL native runtime error: regex validation failed\n");
}

void *nyrt_regex_search(const char *pattern, const char *input,
                       std::int64_t start, std::int64_t full) noexcept {
  try {
    const auto found = ny::patterns::search(pattern ? pattern : "", input ? input : "", start, full != 0);
    const auto count = 1 + found.spans.size() * 2;
    auto *array = nyrt_array_new(NYRT_ARRAY_I64, static_cast<std::int64_t>(count));
    if (!array) nyrt_fail("NIL native runtime error: regex result allocation failed\n");
    nyrt_array_set(array, NYRT_ARRAY_I64, 0, static_cast<std::uint64_t>(found.code));
    std::int64_t index = 1;
    for (const auto &[start, length] : found.spans) {
      nyrt_array_set(array, NYRT_ARRAY_I64, index++, static_cast<std::uint64_t>(start));
      nyrt_array_set(array, NYRT_ARRAY_I64, index++, static_cast<std::uint64_t>(length));
    }
    return array;
  } catch (...) {}
  nyrt_fail("NIL native runtime error: regex search failed\n");
}

std::int64_t nyrt_regex_group_index(const char *pattern, const char *name) noexcept {
  try { return ny::patterns::group_index(pattern ? pattern : "", name ? name : ""); }
  catch (...) { return -1; }
}

void *nyrt_array_new(std::uint32_t kind, std::int64_t length) noexcept {
  if (length < 0)
    return nullptr;
  if (kind == NYRT_ARRAY_ANYTHING && length > 0)
    nyrt_fail("NIL native runtime error: nonempty array<anything> requires an initial value\n");
  return make_array(kind, static_cast<std::uint64_t>(length));
}

void *nyrt_array_new_with_value(std::uint32_t kind, std::int64_t length,
                                std::uint64_t value) noexcept {
  validate_array_value(kind, value);
  if (length < 0) return nullptr;
  auto *array = make_array(kind, static_cast<std::uint64_t>(length));
  if (array == nullptr)
    return nullptr;
  for (std::uint64_t index = 0; index < array->length; ++index)
    store_value(*array, index, value);
  return array;
}

std::int64_t nyrt_array_length(const void *value,
                               std::uint32_t expected_kind) noexcept {
  const auto *array = checked_array(value, expected_kind);
  if (array == nullptr ||
      array->length >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return -1;
  return static_cast<std::int64_t>(array->length);
}

std::uint64_t nyrt_array_get(const void *value, std::uint32_t expected_kind,
                             std::int64_t index) noexcept {
  const auto *array = checked_array(value, expected_kind);
  if (array == nullptr || index < 0 ||
      static_cast<std::uint64_t>(index) >= array->length) {
    if (expected_kind == NYRT_ARRAY_ANYTHING)
      nyrt_fail("NIL native runtime error: anything array read is out of bounds or has an invalid handle\n");
    if (expected_kind == NYRT_ARRAY_TUPLE)
      nyrt_fail("NIL native runtime error: tuple array read is out of bounds or has an invalid handle\n");
    return 0;
  }
  const auto result = load_value(*array, static_cast<std::uint64_t>(index));
  validate_array_value(expected_kind, result);
  if (expected_kind == NYRT_ARRAY_TUPLE && result == 0)
    nyrt_fail("NIL native runtime error: tuple array read encountered an uninitialized element\n");
  return result;
}

std::int32_t nyrt_array_set(void *value, std::uint32_t expected_kind,
                            std::int64_t index, std::uint64_t item) noexcept {
  validate_array_value(expected_kind, item);
  auto *array = checked_array(value, expected_kind);
  if (array == nullptr)
    return 0;
  if (index < 0 || static_cast<std::uint64_t>(index) >= array->length)
    return 0;
  store_value(*array, static_cast<std::uint64_t>(index), item);
  return 1;
}

std::int32_t nyrt_array_push(void *value, std::uint32_t expected_kind,
                             std::uint64_t item) noexcept {
  validate_array_value(expected_kind, item);
  auto *array = checked_array(value, expected_kind);
  if (array == nullptr ||
      array->length == std::numeric_limits<std::uint64_t>::max())
    return 0;
  if (array->length == array->capacity) {
    const std::uint64_t maximum = maximum_array_capacity(array->stride);
    if (array->capacity >= maximum)
      return 0;
    const std::uint64_t doubled =
        array->capacity == 0
            ? std::min<std::uint64_t>(4, maximum)
            : (array->capacity > maximum / 2 ? maximum : array->capacity * 2);
    if (doubled <= array->capacity || !reserve(*array, doubled))
      return 0;
  }
  store_value(*array, array->length, item);
  ++array->length;
  return 1;
}

void *nyrt_buffer_new(std::uint32_t kind, std::int64_t length) noexcept {
  if (length < 0)
    return nullptr;
  return make_buffer(kind, static_cast<std::uint64_t>(length));
}

std::int32_t nyrt_buffer_free(void *value) noexcept {
  if (value == nullptr)
    return 0;
  for (const auto &candidate : context.buffers) {
    if (candidate->data != value || !candidate->active ||
        checked_buffer(value, candidate->kind) != candidate.get()) {
      continue;
    }
    if (candidate->allocated_bytes != 0)
      std::memset(candidate->data, 0, candidate->allocated_bytes);
    // Retain the allocation as an inactive tombstone until context teardown.
    // Its address therefore cannot be reused to make a stale pointer valid.
    candidate->active = false;
    context.profile.release(candidate.get());
    return 1;
  }
  return 0;
}

std::int32_t nyrt_owned_buffer_release(void *value) noexcept {
  if (value == nullptr) return 0;
  for (auto item = context.buffers.begin(); item != context.buffers.end(); ++item) {
    const auto &candidate = *item;
    if (candidate->data == value && candidate->active &&
        checked_buffer(value, candidate->kind) == candidate.get()) {
      context.profile.release(candidate.get());
      context.buffers.erase(item);
      return 1;
    }
  }
  return 0;
}

void *nyrt_buffer_offset(void *value, std::uint32_t expected_kind,
                         std::int64_t amount,
                         std::int32_t subtract) noexcept {
  std::uint64_t current = 0;
  auto *buffer = checked_buffer(value, expected_kind, &current);
  if (buffer == nullptr)
    return nullptr;

  std::uint64_t resolved = 0;
  if (!move_buffer_offset(current, buffer->length, amount, subtract != 0,
                          resolved)) {
    return nullptr;
  }
  return static_cast<unsigned char *>(buffer->data) +
         resolved * buffer->stride;
}

std::uint64_t nyrt_buffer_get(const void *value, std::uint32_t expected_kind,
                              std::int64_t index) noexcept {
  std::uint64_t current = 0;
  const auto *buffer = checked_buffer(value, expected_kind, &current);
  std::uint64_t resolved = 0;
  if (buffer == nullptr ||
      !resolve_buffer_index(current, buffer->length, index, resolved))
    return 0;

  nyrt_array view;
  view.kind = buffer->kind;
  view.data = buffer->data;
  const std::uint64_t result =
      load_value(view, resolved);
  // Prevent the non-owning view from freeing buffer storage.
  view.data = nullptr;
  validate_array_value(expected_kind, result);
  return result;
}

std::int32_t nyrt_buffer_set(void *value, std::uint32_t expected_kind,
                             std::int64_t index, std::uint64_t item) noexcept {
  validate_array_value(expected_kind, item);
  std::uint64_t current = 0;
  auto *buffer = checked_buffer(value, expected_kind, &current);
  std::uint64_t resolved = 0;
  if (buffer == nullptr ||
      !resolve_buffer_index(current, buffer->length, index, resolved))
    return 0;

  nyrt_array view;
  view.kind = buffer->kind;
  view.data = buffer->data;
  store_value(view, resolved, item);
  view.data = nullptr;
  return 1;
}

std::int32_t nyrt_read_regular_bytes(const char *path,
                                     std::int64_t maximum_bytes,
                                     void **bytes_out,
                                     const char **error_out) noexcept {
  if (bytes_out != nullptr)
    *bytes_out = nullptr;
  if (error_out != nullptr)
    *error_out = nullptr;
  if (path == nullptr || bytes_out == nullptr || maximum_bytes < 0 ||
      static_cast<std::uint64_t>(maximum_bytes) > maximum_file_bytes) {
    set_error(error_out, "file.read_regular_bytes: invalid arguments");
    return 0;
  }
  try {
    const std::string path_value(path);
    if (!ny::detail::valid_file_path(path_value)) {
      set_error(error_out, "file.read_regular_bytes: invalid arguments");
      return 0;
    }
    auto result = ny::detail::read_regular_file_bounded(
        path_value, static_cast<std::uint64_t>(maximum_bytes));
    if (!result.ok) {
      set_error(error_out, result.error);
      return 0;
    }
    auto *array = make_array(NYRT_ARRAY_U8, result.bytes.size());
    if (array == nullptr) {
      set_error(error_out, "file.read_regular_bytes: allocation failed");
      return 0;
    }
    if (!result.bytes.empty())
      std::memcpy(array->data, result.bytes.data(), result.bytes.size());
    if (bytes_out != nullptr)
      *bytes_out = array;
    return 1;
  } catch (...) {
    set_error(error_out, "file.read_regular_bytes: host operation failed");
    return 0;
  }
}

std::int32_t nyrt_write_bytes_atomic(const char *path, const void *value,
                                     std::int32_t replace,
                                     const char **error_out) noexcept {
  if (error_out != nullptr)
    *error_out = nullptr;
  const auto *array = checked_array(value, NYRT_ARRAY_U8);
  if (path == nullptr || array == nullptr ||
      array->length > maximum_file_bytes) {
    set_error(error_out, "file.write_bytes_atomic: invalid arguments");
    return 0;
  }
  try {
    const std::string path_value(path);
    if (!ny::detail::valid_file_path(path_value)) {
      set_error(error_out, "file.write_bytes_atomic: invalid arguments");
      return 0;
    }
    const auto *data = static_cast<const char *>(array->data);
    std::vector<char> bytes;
    if (array->length != 0)
      bytes.assign(data, data + static_cast<std::size_t>(array->length));
    auto result =
        ny::detail::write_file_atomic(path_value, bytes, replace != 0);
    if (!result.ok) {
      set_error(error_out, result.error);
      return 0;
    }
    return 1;
  } catch (...) {
    set_error(error_out, "file.write_bytes_atomic: host operation failed");
    return 0;
  }
}

std::int32_t nyrt_try_i32(const char *text, std::int32_t *value_out,
                          const char **error_out) noexcept {
  return try_decimal_integer(text, value_out, error_out, "convert.try_to_i32");
}

std::int32_t nyrt_try_i64(const char *text, std::int64_t *value_out,
                          const char **error_out) noexcept {
  return try_decimal_integer(text, value_out, error_out, "convert.try_to_i64");
}

const char *nyrt_i32_to_text(std::int32_t value) noexcept {
  return integer_to_text(value);
}

const char *nyrt_i64_to_text(std::int64_t value) noexcept {
  return integer_to_text(value);
}

} // extern "C"
