// Diagnostic only: no cache-eligibility decision is made here.
// SPI signatures: Apple's dyld_introspection.h, macOS12/13 availability.
#include <mach-o/dyld.h>
#include <libproc.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr std::size_t limit = 8192;
struct callback_state {
  std::mutex mutex;
  std::vector<const mach_header*> images;
  std::uint64_t generation = 0;
  bool overflow = false;
};
// Public dyld callbacks cannot be unregistered. Keep this bounded state alive
// until process teardown, including any callbacks after C++ global destructors.
callback_state& observed = *new callback_state;
void added(const mach_header* header, intptr_t) {
  std::lock_guard<std::mutex> lock(observed.mutex);
  ++observed.generation;
  if (observed.images.size() == limit) { observed.overflow = true; return; }
  observed.images.push_back(header);
}
void removed(const mach_header* header, intptr_t) {
  std::lock_guard<std::mutex> lock(observed.mutex);
  ++observed.generation;
  for (auto& entry : observed.images) if (entry == header) entry = nullptr;
}
std::string quoted(const char* text) {
  if (!text) return "null";
  const auto size = strnlen(text, 32768);
  if (size == 32768) return "null";
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < size; ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == '\"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += static_cast<char>(c);
  }
  return out + "\"";
}
std::string uuid(const unsigned char* data) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (unsigned i = 0; i < 16; ++i) { result += hex[data[i] >> 4]; result += hex[data[i] & 15]; }
  return result;
}
template <class Function> Function symbol(const char* name, bool& available) {
  dlerror();
  const auto pointer = dlsym(RTLD_DEFAULT, name);
  const auto error = dlerror();
  const bool found = pointer && !error;
  std::cout << "{\"event\":\"symbol\",\"name\":" << quoted(name)
            << ",\"available\":" << (found ? "true" : "false")
            << ",\"error\":" << quoted(error) << "}\n";
  available = available && found;
  return reinterpret_cast<Function>(pointer);
}
void public_snapshot(int round) {
  std::vector<const mach_header*> captured;
  std::uint64_t before;
  bool exceeded;
  { std::lock_guard<std::mutex> lock(observed.mutex); captured = observed.images; before = observed.generation; exceeded = observed.overflow; }
  for (const auto* header : captured) {
    if (!header) continue;
    Dl_info info{};
    const bool found = dladdr(header, &info) != 0;
    char backing[4096]{};
    errno = 0;
    const auto count = proc_regionfilename(getpid(), reinterpret_cast<std::uint64_t>(header), backing, sizeof(backing));
    const int error = errno;
    const auto* name = found ? info.dli_fname : nullptr;
    std::cout << "{\"event\":\"public-image\",\"round\":" << round
              << ",\"path\":" << quoted(name) << ",\"shared_cache\":"
              << (name && _dyld_shared_cache_contains_path(name) ? "true" : "false")
              << ",\"region_path\":" << quoted(count > 0 && count < static_cast<int>(sizeof(backing)) ? backing : nullptr)
              << ",\"region_result\":" << count << ",\"region_errno\":" << error << "}\n";
  }
  std::uint64_t after;
  { std::lock_guard<std::mutex> lock(observed.mutex); after = observed.generation; }
  std::cout << "{\"event\":\"public-generation\",\"round\":" << round
            << ",\"before\":" << before << ",\"after\":" << after
            << ",\"overflow\":" << (exceeded ? "true" : "false") << "}\n";
}
}
int main() {
  observed.images.reserve(limit);
  _dyld_register_func_for_remove_image(removed);
  _dyld_register_func_for_add_image(added);
  bool available = true;
  using opaque = void*;
  const auto create = symbol<opaque(*)()>("dyld_process_create_for_current_task", available);
  const auto dispose = symbol<void(*)(opaque)>("dyld_process_dispose", available);
  const auto snapshot_create = symbol<opaque(*)(opaque, int*)>("dyld_process_snapshot_create_for_process", available);
  const auto snapshot_dispose = symbol<void(*)(opaque)>("dyld_process_snapshot_dispose", available);
  const auto each_image = symbol<void(*)(opaque, void(^)(opaque))>("dyld_process_snapshot_for_each_image", available);
  const auto image_path = symbol<const char*(*)(opaque)>("dyld_image_get_file_path", available);
  const auto image_uuid = symbol<bool(*)(opaque, unsigned char(*)[16])>("dyld_image_copy_uuid", available);
  const auto get_cache = symbol<opaque(*)(opaque)>("dyld_process_snapshot_get_shared_cache", available);
  const auto cache_uuid = symbol<void(*)(opaque, unsigned char(*)[16])>("dyld_shared_cache_copy_uuid", available);
  const auto each_file = symbol<void(*)(opaque, void(^)(const char*))>("dyld_shared_cache_for_each_file", available);
  public_snapshot(0);
  if (!available) { std::cout << "{\"event\":\"incomplete\",\"reason\":\"SPI unavailable\"}\n"; return 2; }
  auto process = create();
  if (!process) { std::cout << "{\"event\":\"incomplete\",\"reason\":\"process snapshot owner unavailable\"}\n"; return 2; }
  bool complete = true;
  for (int round = 0; round != 2; ++round) {
    int error = -1;
    auto snapshot = snapshot_create(process, &error);
    std::cout << "{\"event\":\"snapshot\",\"round\":" << round << ",\"error\":" << error << ",\"available\":" << (snapshot ? "true" : "false") << "}\n";
    if (!snapshot || error) { complete = false; if (snapshot) snapshot_dispose(snapshot); continue; }
    __block std::size_t image_count = 0;
    __block bool bounded = true;
    each_image(snapshot, ^(opaque image) {
      if (++image_count > limit) { bounded = false; return; }
      unsigned char bytes[16]{};
      const bool has_uuid = image_uuid(image, &bytes);
      const char* path = image_path(image);
      std::cout << "{\"event\":\"spi-image\",\"round\":" << round << ",\"path\":" << quoted(path)
                << ",\"uuid\":" << (has_uuid ? quoted(uuid(bytes).c_str()) : "null")
                << ",\"shared_cache\":" << (path && _dyld_shared_cache_contains_path(path) ? "true" : "false") << "}\n";
      if (!path) bounded = false;
    });
    auto cache = get_cache(snapshot);
    if (cache) {
      unsigned char bytes[16]{}; cache_uuid(cache, &bytes);
      std::cout << "{\"event\":\"shared-cache\",\"round\":" << round << ",\"uuid\":" << quoted(uuid(bytes).c_str()) << "}\n";
      __block std::size_t file_count = 0;
      each_file(cache, ^(const char* path) {
        if (++file_count > 128) { bounded = false; return; }
        std::cout << "{\"event\":\"cache-file\",\"round\":" << round << ",\"path\":" << quoted(path) << "}\n";
        if (!path) bounded = false;
      });
      if (!file_count) bounded = false;
    } else { complete = false; std::cout << "{\"event\":\"incomplete\",\"reason\":\"shared cache unavailable\"}\n"; }
    complete = complete && bounded && image_count;
    snapshot_dispose(snapshot);
  }
  dispose(process);
  public_snapshot(1);
  std::cout << "{\"event\":\"collection-ended\",\"spi_complete\":" << (complete ? "true" : "false") << "}\n";
  return complete ? 0 : 2;
}
