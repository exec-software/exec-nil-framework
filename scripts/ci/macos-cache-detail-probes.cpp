// Host-only observations. Never authorizes compiler or native artifact reuse.
// SPI signatures: Apple dyld/include/mach-o/dyld_introspection.h (macOS12/13).
#include <mach-o/dyld.h>
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../common/content_digest.hpp"

namespace {
constexpr std::size_t limit = 16384;
using clock_type = std::chrono::steady_clock;
std::string quote(const char* text) {
  if (!text) return "null";
  const auto size = strnlen(text, 4096);
  if (size == 4096) return "null";
  std::string result = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < size; ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == '\"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += static_cast<char>(c);
  }
  return result + "\"";
}
std::string hex(const unsigned char* data, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (std::size_t i = 0; i < size; ++i) { result += digits[data[i] >> 4]; result += digits[data[i] & 15]; }
  return result;
}
bool bounded_name(const char* name) { return name && strnlen(name, 4096) < 4096; }
struct public_state {
  std::mutex mutex;
  std::vector<const mach_header*> images;
  std::uint64_t generation = 0;
  bool overflow = false;
};
// dyld callbacks cannot be unregistered: retain state through process teardown.
public_state& observed = *new public_state;
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
void public_snapshot(int round) {
  std::vector<const mach_header*> captured;
  std::uint64_t before;
  bool overflow;
  { std::lock_guard<std::mutex> lock(observed.mutex); captured = observed.images; before = observed.generation; overflow = observed.overflow; }
  for (const auto* header : captured) {
    if (!header) continue;
    Dl_info info{};
    const char* name = dladdr(header, &info) ? info.dli_fname : nullptr;
    std::cout << "{\"event\":\"public-image\",\"round\":" << round
              << ",\"path\":" << quote(name) << ",\"contains_path\":"
              << (bounded_name(name) && _dyld_shared_cache_contains_path(name) ? "true" : "false") << "}\n";
  }
  std::uint64_t after;
  { std::lock_guard<std::mutex> lock(observed.mutex); after = observed.generation; }
  std::cout << "{\"event\":\"public-generation\",\"round\":" << round << ",\"before\":" << before
            << ",\"after\":" << after << ",\"overflow\":" << (overflow ? "true" : "false") << "}\n";
}
template<class Function> Function symbol(const char* name, bool& available) {
  dlerror(); const auto pointer = dlsym(RTLD_DEFAULT, name); const auto error = dlerror();
  const bool found = pointer && !error;
  std::cout << "{\"event\":\"symbol\",\"name\":" << quote(name) << ",\"available\":"
            << (found ? "true" : "false") << ",\"error\":" << quote(error) << "}\n";
  available = available && found;
  return reinterpret_cast<Function>(pointer);
}
bool same_file(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
         a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
         a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}
int hash_prefix(const char* path) {
  if (path[0] != '/') throw std::runtime_error("prefix path must be absolute");
  constexpr std::size_t bytes = 128 * 1024 * 1024;
  const auto started = clock_type::now();
  const auto deadline = started + std::chrono::seconds(55);
  const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) throw std::runtime_error("prefix open failed: " + std::to_string(errno));
  struct close_fd { int fd; ~close_fd() { close(fd); } } guard{fd};
  struct stat before{}, after{}, named{};
  if (fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < static_cast<off_t>(bytes))
    throw std::runtime_error("prefix requires a regular file of at least 128MiB");
  std::array<char, 65536> buffer{};
  std::vector<std::string> digests;
  for (int pass = 0; pass < 2; ++pass) {
    const auto begin = clock_type::now();
    nil::detail::sha256 scalar;
    CC_SHA256_CTX platform{};
    if (pass && CC_SHA256_Init(&platform) != 1) throw std::runtime_error("CommonCrypto init failed");
    std::size_t offset = 0;
    while (offset < bytes) {
      if (clock_type::now() >= deadline) throw std::runtime_error("prefix hash deadline exceeded");
      const auto got = pread(fd, buffer.data(), std::min(buffer.size(), bytes - offset), static_cast<off_t>(offset));
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) throw std::runtime_error("prefix read failed or shortened");
      if (pass) {
        if (CC_SHA256_Update(&platform, buffer.data(), static_cast<CC_LONG>(got)) != 1)
          throw std::runtime_error("CommonCrypto update failed");
      } else scalar.update(buffer.data(), static_cast<std::size_t>(got));
      offset += static_cast<std::size_t>(got);
    }
    unsigned char result[CC_SHA256_DIGEST_LENGTH]{};
    if (pass && CC_SHA256_Final(result, &platform) != 1) throw std::runtime_error("CommonCrypto final failed");
    digests.push_back(pass ? hex(result, sizeof(result)) : scalar.finish());
    std::cout << "{\"event\":\"prefix-hash\",\"algorithm\":\"SHA256\",\"implementation\":\""
              << (pass ? "CommonCrypto" : "common/content_digest.hpp") << "\",\"order\":" << pass
              << ",\"bytes\":" << bytes << ",\"seconds_including_pread\":"
              << std::chrono::duration<double>(clock_type::now() - begin).count()
              << ",\"sha256\":" << quote(digests.back().c_str()) << "}\n";
  }
  if (fstat(fd, &after) || stat(path, &named) || !same_file(before, after) || !same_file(after, named))
    throw std::runtime_error("prefix backing identity changed");
  if (digests[0] != digests[1]) throw std::runtime_error("scalar/platform prefix digests differ");
  std::cout << "{\"event\":\"prefix-complete\",\"path\":" << quote(path)
            << ",\"device\":" << before.st_dev << ",\"inode\":" << before.st_ino
            << ",\"file_bytes\":" << before.st_size << ",\"identity_unchanged\":true,\"digests_equal\":true}\n";
  return 0;
}
int images() {
  observed.images.reserve(limit);
  _dyld_register_func_for_remove_image(removed);
  _dyld_register_func_for_add_image(added);
  public_snapshot(0);
  bool available = true;
  using opaque = void*;
  const auto create = symbol<opaque(*)()>("dyld_process_create_for_current_task", available);
  const auto dispose = symbol<void(*)(opaque)>("dyld_process_dispose", available);
  const auto snapshot_create = symbol<opaque(*)(opaque, int*)>("dyld_process_snapshot_create_for_process", available);
  const auto snapshot_dispose = symbol<void(*)(opaque)>("dyld_process_snapshot_dispose", available);
  const auto each_image = symbol<void(*)(opaque, void(^)(opaque))>("dyld_process_snapshot_for_each_image", available);
  const auto image_path = symbol<const char*(*)(opaque)>("dyld_image_get_file_path", available);
  const auto install_name = symbol<const char*(*)(opaque)>("dyld_image_get_installname", available);
  const auto image_uuid = symbol<bool(*)(opaque, unsigned char(*)[16])>("dyld_image_copy_uuid", available);
  const auto each_segment = symbol<bool(*)(opaque, void(^)(const char*, std::uint64_t, std::uint64_t, int))>("dyld_image_for_each_segment_info", available);
  const auto get_cache = symbol<opaque(*)(opaque)>("dyld_process_snapshot_get_shared_cache", available);
  const auto cache_uuid = symbol<void(*)(opaque, unsigned char(*)[16])>("dyld_shared_cache_copy_uuid", available);
  const auto cache_base = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_base_address", available);
  const auto cache_size = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_mapped_size", available);
  const auto each_file = symbol<void(*)(opaque, void(^)(const char*))>("dyld_shared_cache_for_each_file", available);
  const auto cache_images = symbol<void(*)(opaque, void(^)(opaque))>("dyld_shared_cache_for_each_image", available);
  if (!available) throw std::runtime_error("required diagnostic SPI unavailable");
  auto process = create();
  if (!process) throw std::runtime_error("process snapshot owner unavailable");
  std::unique_ptr<void, void(*)(void*)> process_owner(process, dispose);
  bool complete = true;
  for (int round = 0; round < 2; ++round) {
    int error = -1;
    auto snapshot = snapshot_create(process, &error);
    std::unique_ptr<void, void(*)(void*)> snapshot_owner(snapshot, snapshot_dispose);
    std::cout << "{\"event\":\"snapshot\",\"round\":" << round << ",\"error\":" << error
              << ",\"available\":" << (snapshot ? "true" : "false") << "}\n";
    if (!snapshot || error) { complete = false; continue; }
    auto cache = get_cache(snapshot);
    if (!cache) { complete = false; continue; }
    unsigned char cache_id[16]{}; cache_uuid(cache, &cache_id);
    std::cout << "{\"event\":\"shared-cache\",\"round\":" << round << ",\"uuid\":" << quote(hex(cache_id, 16).c_str())
              << ",\"base\":" << cache_base(cache) << ",\"mapped_size\":" << cache_size(cache) << "}\n";
    __block bool bounded = true;
    __block std::size_t files = 0;
    each_file(cache, ^(const char* path) {
      if (++files > 128) { bounded = false; return; }
      if (!bounded_name(path)) bounded = false;
      std::cout << "{\"event\":\"cache-file\",\"round\":" << round << ",\"path\":" << quote(path) << "}\n";
    });
    __block std::set<std::pair<std::string, std::string>> inventory;
    __block std::size_t entries = 0, name_bytes = 0;
    cache_images(cache, ^(opaque image) {
      if (++entries > limit) { bounded = false; return; }
      const char* name = install_name(image);
      unsigned char id[16]{};
      if (!bounded_name(name) || !image_uuid(image, &id)) { bounded = false; return; }
      name_bytes += std::strlen(name);
      if (name_bytes > 4 * 1024 * 1024) { bounded = false; return; }
      if (!inventory.emplace(name, hex(id, 16)).second) bounded = false;
    });
    nil::detail::sha256 inventory_digest;
    for (const auto& item : inventory) {
      inventory_digest.update(item.first.c_str(), item.first.size() + 1);
      inventory_digest.update(item.second.c_str(), item.second.size() + 1);
    }
    std::cout << "{\"event\":\"cache-inventory\",\"round\":" << round << ",\"entries\":" << entries
              << ",\"unique_name_uuid_pairs\":" << inventory.size() << ",\"sorted_nul_pairs_sha256\":"
              << quote(inventory_digest.finish().c_str()) << ",\"scope\":\"all cached images, including unloaded images\"}\n";
    __block std::size_t count = 0;
    each_image(snapshot, ^(opaque image) {
      if (++count > limit) { bounded = false; return; }
      const char* path = image_path(image);
      const char* name = install_name(image);
      unsigned char id[16]{}; const bool has_id = image_uuid(image, &id);
      const auto id_hex = hex(id, 16);
      const bool member = bounded_name(name) && _dyld_shared_cache_contains_path(name);
      const bool matched = bounded_name(name) && has_id && inventory.count({name, id_hex});
      std::cout << "{\"event\":\"loaded-image\",\"round\":" << round << ",\"index\":" << count
                << ",\"path\":" << quote(path) << ",\"install_name\":" << quote(name)
                << ",\"uuid\":" << (has_id ? quote(id_hex.c_str()) : "null")
                << ",\"contains_install_name\":" << (member ? "true" : "false")
                << ",\"contains_file_path\":" << (bounded_name(path) && _dyld_shared_cache_contains_path(path) ? "true" : "false")
                << ",\"inventory_name_uuid_match\":" << (matched ? "true" : "false") << "}\n";
      // Null file paths alone never establish cache inventory correspondence.
      // Even matching names/UUIDs do not prove this loaded image's backing:
      // a copied or overridden image could preserve both. No reuse decision.
      if (!bounded_name(path) && !(member && matched)) bounded = false;
      if (member != matched) bounded = false;
      // Only selected images need detailed segment ranges; no segment content is
      // materialized, and file/zero-fill/fixup coverage remains unresolved.
      const bool selected = member && (std::strcmp(name, "/usr/lib/libc++.1.dylib") == 0 ||
                                       std::strcmp(name, "/usr/lib/system/libsystem_kernel.dylib") == 0);
      if (selected) {
        __block std::size_t segments = 0;
        const bool got = each_segment(image, ^(const char* segment, std::uint64_t address, std::uint64_t size, int permissions) {
          if (++segments > 64) { bounded = false; return; }
          std::cout << "{\"event\":\"selected-segment\",\"round\":" << round << ",\"image_index\":" << count
                    << ",\"name\":" << quote(segment) << ",\"address\":" << address << ",\"size\":" << size
                    << ",\"permissions\":" << permissions << "}\n";
        });
        if (!got) bounded = false;
      }
    });
    complete = complete && bounded && count && files && entries;
    std::cout << "{\"event\":\"snapshot-ended\",\"round\":" << round << ",\"loaded_images\":" << count
              << ",\"bounded\":" << (bounded ? "true" : "false") << "}\n";
    snapshot_owner.reset();
    public_snapshot(round + 1);
  }
  std::cout << "{\"event\":\"collection-ended\",\"observations_complete\":" << (complete ? "true" : "false") << "}\n";
  return complete ? 0 : 2;
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::strcmp(argv[1], "--hash-prefix") == 0) return hash_prefix(argv[2]);
    if (argc != 1) throw std::runtime_error("usage: probe [--hash-prefix ABSOLUTE_FILE]");
    return images();
  } catch (const std::exception& error) {
    std::cout << "{\"event\":\"incomplete\",\"reason\":" << quote(error.what()) << "}\n";
    return 2;
  }
}
