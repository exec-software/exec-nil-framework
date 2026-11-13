#include "compiler_module_fingerprint.hpp"
#include "compiler_module_fingerprint_darwin.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>

namespace nil::detail::compiler_modules {
namespace {
using namespace darwin;
constexpr std::size_t maximum_commands = 65536;
bool read_self(std::uint64_t address, void *destination, std::size_t size) {
  if (!address || size > maximum_commands || address > UINT64_MAX - size) return false;
  mach_vm_size_t copied = 0;
  return mach_vm_read_overwrite(mach_task_self(), address, size,
      reinterpret_cast<mach_vm_address_t>(destination), &copied) == KERN_SUCCESS && copied == size;
}
loaded_image inspect(const mach_header *pointer, std::int64_t slide, bool derive_slide = false) {
  loaded_image row;
  row.address = reinterpret_cast<std::uintptr_t>(pointer);
  row.slide = slide;
  mach_header_64 header{};
  require(read_self(row.address, &header, sizeof(header)) && header.magic == MH_MAGIC_64 &&
          header.ncmds && header.ncmds <= 1024 && header.sizeofcmds <= maximum_commands &&
          header.ncmds <= header.sizeofcmds / sizeof(load_command));
  row.cpu = static_cast<std::uint32_t>(header.cputype); row.flags = header.flags;
  std::array<unsigned char, maximum_commands> commands{};
  require(read_self(end(row.address, sizeof(header)), commands.data(), header.sizeofcmds));
  std::size_t offset = 0;
  bool text = false, identifier = false;
  for (std::uint32_t i = 0; i < header.ncmds; ++i) {
    require(offset <= header.sizeofcmds && sizeof(load_command) <= header.sizeofcmds - offset);
    load_command command{};
    std::memcpy(&command, commands.data() + offset, sizeof(command));
    require(command.cmdsize >= sizeof(command) && command.cmdsize % 8 == 0 && command.cmdsize <= header.sizeofcmds - offset);
    if (command.cmd == LC_UUID) {
      require(!identifier && command.cmdsize == sizeof(uuid_command));
      uuid_command item{}; std::memcpy(&item, commands.data() + offset, sizeof(item));
      std::copy(std::begin(item.uuid), std::end(item.uuid), row.id.begin());
      identifier = true;
    }
    if (command.cmd == LC_SEGMENT_64) {
      require(command.cmdsize >= sizeof(segment_command_64));
      segment_command_64 item{}; std::memcpy(&item, commands.data() + offset, sizeof(item));
      require(item.nsects <= (command.cmdsize - sizeof(item)) / sizeof(section_64) &&
              sizeof(item) + item.nsects * sizeof(section_64) == command.cmdsize);
      end(item.vmaddr, item.vmsize);
      if (std::strncmp(item.segname, "__TEXT", sizeof(item.segname)) == 0) {
        require(!text && item.vmsize);
        const auto actual_slide = image_slide(row.address, item.vmaddr);
        if (derive_slide) row.slide = actual_slide;
        else require(actual_slide == row.slide);
        row.text_size = item.vmsize; text = true;
      }
    }
    offset += command.cmdsize;
  }
  require(text && offset == header.sizeofcmds);
  // An ordinary executable built without LC_UUID is still covered by its full
  // file digest. Cached images must carry a matching UUID in attribute().
  return row;
}
struct image_state {
  std::mutex mutex;
  std::vector<loaded_image> rows;
  std::uint64_t generation = 0;
  bool failed = false;
};
// dyld callbacks cannot be unregistered and may run during process teardown.
image_state &state() { static auto *value = new image_state; return *value; }
void added(const mach_header *header, intptr_t slide) {
  auto &owner = state();
  try {
    auto row = inspect(header, slide);
    std::lock_guard<std::mutex> lock(owner.mutex);
    ++owner.generation;
    if (owner.rows.size() >= 4096) owner.failed = true;
    else owner.rows.push_back(std::move(row));
  } catch (...) {
    std::lock_guard<std::mutex> lock(owner.mutex);
    ++owner.generation; owner.failed = true;
  }
}
void removed(const mach_header *header, intptr_t) {
  auto &owner = state();
  std::lock_guard<std::mutex> lock(owner.mutex);
  ++owner.generation;
  const auto address = reinterpret_cast<std::uintptr_t>(header);
  owner.rows.erase(std::remove_if(owner.rows.begin(), owner.rows.end(),
      [&](const auto &row) { return row.address == address; }), owner.rows.end());
}
template<class Function> Function symbol(const char *name) {
  dlerror(); void *pointer = dlsym(RTLD_DEFAULT, name);
  require(pointer && !dlerror());
  return reinterpret_cast<Function>(pointer);
}
using opaque = void *;
struct api {
  opaque (*create)() = symbol<opaque(*)()>("dyld_process_create_for_current_task");
  void (*dispose)(opaque) = symbol<void(*)(opaque)>("dyld_process_dispose");
  opaque (*snapshot)(opaque, int *) = symbol<opaque(*)(opaque, int *)>("dyld_process_snapshot_create_for_process");
  void (*free_snapshot)(opaque) = symbol<void(*)(opaque)>("dyld_process_snapshot_dispose");
  opaque (*cache)(opaque) = symbol<opaque(*)(opaque)>("dyld_process_snapshot_get_shared_cache");
  void (*cache_uuid)(opaque, unsigned char(*)[16]) = symbol<void(*)(opaque, unsigned char(*)[16])>("dyld_shared_cache_copy_uuid");
  std::uint64_t (*base)(opaque) = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_base_address");
  std::uint64_t (*size)(opaque) = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_mapped_size");
  bool (*private_mapping)(opaque) = symbol<bool(*)(opaque)>("dyld_shared_cache_is_mapped_private");
  void (*files)(opaque, void(^)(const char *)) = symbol<void(*)(opaque, void(^)(const char *))>("dyld_shared_cache_for_each_file");
};
struct snapshot {
  observation active;
  std::vector<std::string> paths;
};
snapshot capture(const api &functions, opaque process) {
  int error = -1;
  std::unique_ptr<void, void(*)(void *)> owner(functions.snapshot(process, &error), functions.free_snapshot);
  require(owner && !error);
  const auto cache = functions.cache(owner.get());
  require(cache && !functions.private_mapping(cache));
  snapshot result;
  unsigned char id[16]{};
  functions.cache_uuid(cache, &id);
  std::copy(std::begin(id), std::end(id), result.active.cache_id.begin());
  result.active.base = functions.base(cache); result.active.size = functions.size(cache);
  __block bool complete = true;
  // A pointer capture avoids copying the mutable C++ owner into the block.
  auto *paths = &result.paths;
  functions.files(cache, ^(const char *path) {
    if (!path || path[0] != '/' || strnlen(path, 4096) == 4096 || paths->size() >= maximum_files) { complete = false; return; }
    try { paths->emplace_back(path); } catch (...) { complete = false; }
  });
  require(complete && !result.paths.empty());
  std::sort(result.paths.begin(), result.paths.end());
  require(std::adjacent_find(result.paths.begin(), result.paths.end()) == result.paths.end());
  return result;
}
dyld_all_image_infos loader() {
  task_dyld_info_data_t task{};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  require(task_info(mach_task_self(), TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&task), &count) == KERN_SUCCESS &&
          count == TASK_DYLD_INFO_COUNT && task.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64 &&
          task.all_image_info_size >= offsetof(dyld_all_image_infos, dyldPath) + sizeof(const char *));
  dyld_all_image_infos info{};
  require(read_self(task.all_image_info_addr, &info, offsetof(dyld_all_image_infos, dyldPath) + sizeof(const char *)) &&
          info.version >= 15 && info.dyldImageLoadAddress && info.sharedCacheBaseAddress &&
          info.infoArray && info.infoArrayCount && info.infoArrayCount <= 4096);
  return info;
}
bool same_file(const struct stat &a, const struct stat &b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
         a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
         a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}
void retain_readable(files &observed, const std::string &path) {
  if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos) return;
  std::error_code error;
  if (fs::is_regular_file(fs::u8path(path), error) && !error &&
      static_cast<bool>(std::ifstream(fs::u8path(path), std::ios::binary))) observed.insert(path);
}
} // namespace

bool collect_darwin(files &observed) {
  try {
    // Preserve independently known ordinary inputs even on an older host
    // without the optional shared-cache APIs. This inventory alone never
    // certifies completeness; cached virtual install names are not files.
    const auto ordinary_count = _dyld_image_count();
    require(ordinary_count <= 4096);
    for (std::uint32_t i = 0; i < ordinary_count; ++i) {
      const auto *path = _dyld_get_image_name(i);
      if (path && strnlen(path, 4096) < 4096) retain_readable(observed, path);
    }
    api functions;
    std::unique_ptr<void, void(*)(void *)> process(functions.create(), functions.dispose);
    require(static_cast<bool>(process));
    // Resolve/prewarm optional API and digest machinery before observing loader
    // generations. Their libraries are themselves part of the resulting set.
    CC_SHA256_CTX digest{}; require(CC_SHA256_Init(&digest) == 1);
    (void)capture(functions, process.get());
    static std::once_flag registration;
    std::call_once(registration, [] {
      (void)state();
      _dyld_register_func_for_add_image(added);
      _dyld_register_func_for_remove_image(removed);
    });
    auto before = capture(functions, process.get());
    const auto loaded = loader();
    require(loaded.sharedCacheBaseAddress == before.active.base &&
            std::equal(std::begin(loaded.sharedCacheUUID), std::end(loaded.sharedCacheUUID), before.active.cache_id.begin()));
    before.active.slide = loaded.sharedCacheSlide;
    before.active.loader = reinterpret_cast<std::uintptr_t>(loaded.dyldImageLoadAddress);
    std::uint64_t generation = 0;
    {
      auto &owner = state();
      std::lock_guard<std::mutex> lock(owner.mutex);
      require(!owner.failed && owner.rows.size() == loaded.infoArrayCount);
      before.active.images = owner.rows; generation = owner.generation;
    }
    if (std::none_of(before.active.images.begin(), before.active.images.end(),
        [&](const auto &row) { return row.address == before.active.loader; }))
      before.active.images.push_back(inspect(loaded.dyldImageLoadAddress, 0, true));
    for (auto &row : before.active.images) {
      Dl_info named{};
      if (dladdr(reinterpret_cast<const void *>(row.address), &named) &&
          reinterpret_cast<std::uintptr_t>(named.dli_fbase) == row.address && named.dli_fname &&
          strnlen(named.dli_fname, 4096) < 4096) row.ordinary_path = named.dli_fname;
      if (row.ordinary_path.empty() && row.address == before.active.loader && loaded.dyldPath &&
          strnlen(loaded.dyldPath, 4096) < 4096) row.ordinary_path = loaded.dyldPath;
      retain_readable(observed, row.ordinary_path);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::size_t remaining = 8U * 1024 * 1024;
    std::vector<cache_file> caches;
    std::set<std::pair<dev_t, ino_t>> backing_ids;
    for (const auto &path : before.paths) {
      const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
      require(descriptor >= 0);
      struct close_file { int value; ~close_file() { close(value); } } cleanup{descriptor};
      struct stat first{}, last{}, named{};
      require(!fstat(descriptor, &first) && S_ISREG(first.st_mode) && first.st_size > 0 &&
              static_cast<std::uint64_t>(first.st_size) <= maximum_bytes && backing_ids.emplace(first.st_dev, first.st_ino).second);
      // A readable API-reported backing file remains protected even if a
      // later metadata check is unsupported or another backing file is absent.
      observed.insert(path);
      const reader read = [&](std::uint64_t offset, std::size_t count) {
        require(std::chrono::steady_clock::now() < deadline && count <= remaining);
        end(offset, count, static_cast<std::uint64_t>(first.st_size));
        remaining -= count;
        bytes data(count);
        std::size_t copied = 0;
        while (copied < count) {
          require(std::chrono::steady_clock::now() < deadline);
          const auto size = pread(descriptor, data.data() + copied, count - copied, static_cast<off_t>(offset + copied));
          if (size < 0 && errno == EINTR) continue;
          require(size > 0); copied += static_cast<std::size_t>(size);
        }
        return data;
      };
      const auto prefix = read(0, 104);
      caches.push_back(parse(path, static_cast<std::uint64_t>(first.st_size), read,
                             identity(prefix, 88) == before.active.cache_id));
      require(!fstat(descriptor, &last) && !stat(path.c_str(), &named) && same_file(first, last) && same_file(first, named));
    }
#if defined(__aarch64__) || defined(__arm64__)
    constexpr std::uint32_t cpu = 0x0100000cU;
#else
    constexpr std::uint32_t cpu = 0x01000007U;
#endif
    const auto paths = attribute(before.active, caches, cpu);
    for (const auto &path : paths) {
      std::error_code error;
      require(fs::is_regular_file(fs::u8path(path), error) && !error && static_cast<bool>(std::ifstream(fs::u8path(path), std::ios::binary)));
    }
    const auto after = capture(functions, process.get());
    const auto loaded_after = loader();
    require(after.paths == before.paths && after.active.cache_id == before.active.cache_id &&
            after.active.base == before.active.base && after.active.size == before.active.size &&
            loaded_after.infoArrayChangeTimestamp == loaded.infoArrayChangeTimestamp &&
            loaded_after.sharedCacheBaseAddress == loaded.sharedCacheBaseAddress &&
            loaded_after.sharedCacheSlide == loaded.sharedCacheSlide);
    {
      auto &owner = state(); std::lock_guard<std::mutex> lock(owner.mutex);
      require(!owner.failed && owner.generation == generation);
    }
    observed.insert(paths.begin(), paths.end());
    return true;
  } catch (...) {
    return false;
  }
}
} // namespace nil::detail::compiler_modules
