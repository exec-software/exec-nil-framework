// Standalone own-process observations only; never enables compiler reuse.
// Layout/API references: Apple dyld-1285.19 dyld.h, dyld_images.h,
// dyld_introspection.h and dyld_cache_format.h. No external task is queried.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../common/content_digest.hpp"

namespace {
using clock_type = std::chrono::steady_clock;
constexpr std::size_t max_images = 4096, max_files = 128, max_commands = 65536;
constexpr std::uint64_t max_bytes = 8ULL * 1024 * 1024 * 1024;
std::string quote(const char* value) {
  if (!value || strnlen(value, 4096) == 4096) return "null";
  std::string result = "\"";
  constexpr char digits[] = "0123456789abcdef";
  for (const auto* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
    if (*p == '"' || *p == '\\') { result += '\\'; result += static_cast<char>(*p); }
    else if (*p < 32) { result += "\\u00"; result += digits[*p >> 4]; result += digits[*p & 15]; }
    else result += static_cast<char>(*p);
  }
  return result + '"';
}
std::string hex(const unsigned char* bytes, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (std::size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
  return result;
}
bool read_self(std::uint64_t address, void* output, std::size_t size) {
  if (!address || size > max_commands || address > UINT64_MAX - size) return false;
  mach_vm_size_t copied = 0;
  return mach_vm_read_overwrite(mach_task_self(), address, size,
      reinterpret_cast<mach_vm_address_t>(output), &copied) == KERN_SUCCESS && copied == size;
}
struct header_record {
  std::uint64_t address = 0, text_address = 0, text_size = 0;
  std::int64_t slide = 0;
  std::uint32_t cpu = 0, flags = 0, kind = 0;
  std::array<unsigned char, 16> uuid{};
  bool has_uuid = false, good = false, active = true;
};
header_record inspect(const mach_header* pointer, std::int64_t slide) {
  header_record row;
  row.address = reinterpret_cast<std::uintptr_t>(pointer); row.slide = slide;
  mach_header_64 header{};
  if (!read_self(row.address, &header, sizeof(header)) || header.magic != MH_MAGIC_64 ||
      header.ncmds == 0 || header.ncmds > 1024 || header.sizeofcmds > max_commands ||
      header.ncmds > header.sizeofcmds / sizeof(load_command)) return row;
  row.cpu = static_cast<std::uint32_t>(header.cputype); row.flags = header.flags; row.kind = header.filetype;
  std::array<unsigned char, max_commands> commands{};
  if (row.address > UINT64_MAX - sizeof(header) ||
      !read_self(row.address + sizeof(header), commands.data(), header.sizeofcmds)) return row;
  std::size_t offset = 0; bool text = false;
  for (std::uint32_t i = 0; i < header.ncmds; ++i) {
    if (offset > header.sizeofcmds || header.sizeofcmds - offset < sizeof(load_command)) return row;
    load_command command{}; std::memcpy(&command, commands.data() + offset, sizeof(command));
    if (command.cmdsize < sizeof(command) || command.cmdsize % 8 || command.cmdsize > header.sizeofcmds - offset) return row;
    if (command.cmd == LC_UUID) {
      if (row.has_uuid || command.cmdsize != sizeof(uuid_command)) return row;
      uuid_command uuid{}; std::memcpy(&uuid, commands.data() + offset, sizeof(uuid));
      std::copy(std::begin(uuid.uuid), std::end(uuid.uuid), row.uuid.begin()); row.has_uuid = true;
    }
    if (command.cmd == LC_SEGMENT_64) {
      if (command.cmdsize < sizeof(segment_command_64)) return row;
      segment_command_64 segment{}; std::memcpy(&segment, commands.data() + offset, sizeof(segment));
      if (segment.nsects > (command.cmdsize - sizeof(segment)) / sizeof(section_64) ||
          sizeof(segment) + segment.nsects * sizeof(section_64) != command.cmdsize ||
          segment.vmaddr > UINT64_MAX - segment.vmsize) return row;
      if (std::strncmp(segment.segname, "__TEXT", sizeof(segment.segname)) == 0) {
        if (text || !segment.vmsize) return row;
        text = true; row.text_address = segment.vmaddr; row.text_size = segment.vmsize;
      }
    }
    offset += command.cmdsize;
  }
  row.good = text && offset == header.sizeofcmds;
  return row;
}
bool derive_slide(header_record& row) {
  if (row.address >= row.text_address) {
    const auto delta = row.address - row.text_address;
    if (delta > static_cast<std::uint64_t>(INT64_MAX)) return false;
    row.slide = static_cast<std::int64_t>(delta);
  } else {
    const auto delta = row.text_address - row.address;
    const auto bound = std::uint64_t{1} << 63;
    if (delta > bound) return false;
    row.slide = delta == bound ? INT64_MIN : -static_cast<std::int64_t>(delta);
  }
  return true;
}
struct state {
  std::mutex mutex;
  std::vector<header_record> rows;
  std::uint64_t generation = 0;
  bool overflow = false;
};
// dyld callbacks cannot be unregistered; keep their bounded owner until exit.
state& observed = *new state;
void added(const mach_header* header, intptr_t slide) {
  const auto row = inspect(header, slide);
  std::lock_guard<std::mutex> lock(observed.mutex); ++observed.generation;
  if (observed.rows.size() >= max_images) { observed.overflow = true; return; }
  observed.rows.push_back(row);
}
void removed(const mach_header* header, intptr_t) {
  std::lock_guard<std::mutex> lock(observed.mutex); ++observed.generation;
  for (auto& row : observed.rows) if (row.address == reinterpret_cast<std::uintptr_t>(header)) row.active = false;
}
template<class Function> Function symbol(const char* name) {
  dlerror(); void* pointer = dlsym(RTLD_DEFAULT, name);
  if (!pointer || dlerror()) throw std::runtime_error(std::string("required SPI unavailable: ") + name);
  return reinterpret_cast<Function>(pointer);
}
using opaque = void*;
struct api {
  opaque (*create)() = symbol<opaque(*)()>("dyld_process_create_for_current_task");
  void (*dispose)(opaque) = symbol<void(*)(opaque)>("dyld_process_dispose");
  opaque (*snapshot)(opaque, int*) = symbol<opaque(*)(opaque, int*)>("dyld_process_snapshot_create_for_process");
  void (*free_snapshot)(opaque) = symbol<void(*)(opaque)>("dyld_process_snapshot_dispose");
  void (*each_image)(opaque, void(^)(opaque)) = symbol<void(*)(opaque, void(^)(opaque))>("dyld_process_snapshot_for_each_image");
  const char* (*path)(opaque) = symbol<const char*(*)(opaque)>("dyld_image_get_file_path");
  const char* (*name)(opaque) = symbol<const char*(*)(opaque)>("dyld_image_get_installname");
  bool (*uuid)(opaque, unsigned char(*)[16]) = symbol<bool(*)(opaque, unsigned char(*)[16])>("dyld_image_copy_uuid");
  bool (*segments)(opaque, void(^)(const char*, std::uint64_t, std::uint64_t, int)) = symbol<bool(*)(opaque, void(^)(const char*, std::uint64_t, std::uint64_t, int))>("dyld_image_for_each_segment_info");
  opaque (*cache)(opaque) = symbol<opaque(*)(opaque)>("dyld_process_snapshot_get_shared_cache");
  void (*cache_uuid)(opaque, unsigned char(*)[16]) = symbol<void(*)(opaque, unsigned char(*)[16])>("dyld_shared_cache_copy_uuid");
  std::uint64_t (*base)(opaque) = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_base_address");
  std::uint64_t (*size)(opaque) = symbol<std::uint64_t(*)(opaque)>("dyld_shared_cache_get_mapped_size");
  bool (*private_mapping)(opaque) = symbol<bool(*)(opaque)>("dyld_shared_cache_is_mapped_private");
  void (*files)(opaque, void(^)(const char*)) = symbol<void(*)(opaque, void(^)(const char*))>("dyld_shared_cache_for_each_file");
};
dyld_all_image_infos own_loader() {
  task_dyld_info_data_t task{}; mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&task), &count) != KERN_SUCCESS ||
      count != TASK_DYLD_INFO_COUNT || task.all_image_info_format != TASK_DYLD_ALL_IMAGE_INFO_64 ||
      task.all_image_info_size < offsetof(dyld_all_image_infos, dyldPath) + sizeof(const char*))
    throw std::runtime_error("own-task dyld info unavailable or unsupported");
  dyld_all_image_infos info{};
  const auto bytes = offsetof(dyld_all_image_infos, dyldPath) + sizeof(const char*);
  if (!read_self(task.all_image_info_addr, &info, bytes) || info.version < 15 || !info.dyldImageLoadAddress ||
      !info.sharedCacheBaseAddress || !info.infoArray || !info.infoArrayCount || info.infoArrayCount > max_images)
    throw std::runtime_error("own-task dyld info incomplete");
  return info;
}
void emit_header(const header_record& row, int round, const char* origin) {
  Dl_info name{}; const bool named = dladdr(reinterpret_cast<const void*>(row.address), &name) != 0;
  std::cout << "{\"event\":\"header\",\"round\":" << round << ",\"origin\":" << quote(origin)
    << ",\"address\":" << row.address << ",\"slide\":" << row.slide
    << ",\"text_address\":" << row.text_address << ",\"text_size\":" << row.text_size
    << ",\"cpu\":" << row.cpu << ",\"flags\":" << row.flags << ",\"kind\":" << row.kind
    << ",\"uuid\":" << (row.has_uuid ? quote(hex(row.uuid.data(), 16).c_str()) : "null")
    << ",\"valid\":" << (row.good ? "true" : "false") << ",\"path\":" << quote(named ? name.dli_fname : nullptr)
    << ",\"dladdr_base\":" << (named ? reinterpret_cast<std::uintptr_t>(name.dli_fbase) : 0) << "}\n";
}
bool snapshot(const api& funcs, opaque process, int round) {
  int error = -1; auto pointer = funcs.snapshot(process, &error);
  std::unique_ptr<void, void(*)(void*)> owner(pointer, funcs.free_snapshot);
  if (!pointer || error) throw std::runtime_error("own-process SPI snapshot failed");
  auto cache = funcs.cache(pointer);
  if (!cache) throw std::runtime_error("active shared cache unavailable");
  const bool emit = round >= 0;
  std::vector<header_record> rows; std::uint64_t generation = 0; bool overflow = false;
  if (emit) {
    std::lock_guard<std::mutex> lock(observed.mutex);
    rows = observed.rows; generation = observed.generation; overflow = observed.overflow;
  }
  const auto loader = own_loader();
  unsigned char cache_id[16]{}; funcs.cache_uuid(cache, &cache_id);
  if (emit) std::cout << "{\"event\":\"cache\",\"round\":" << round
    << ",\"uuid\":" << quote(hex(cache_id, 16).c_str()) << ",\"base\":" << funcs.base(cache)
    << ",\"size\":" << funcs.size(cache) << ",\"private\":" << (funcs.private_mapping(cache) ? "true" : "false")
    << ",\"loader_header\":" << reinterpret_cast<std::uintptr_t>(loader.dyldImageLoadAddress)
    << ",\"loader_cache_base\":" << loader.sharedCacheBaseAddress << ",\"loader_cache_slide\":" << loader.sharedCacheSlide
    << ",\"loader_cache_uuid\":" << quote(hex(loader.sharedCacheUUID, 16).c_str())
    << ",\"loader_timestamp\":" << loader.infoArrayChangeTimestamp << "}\n";
  __block bool good = true; __block std::size_t file_count = 0, image_count = 0;
  funcs.files(cache, ^(const char* path) {
    if (++file_count > max_files || !path || path[0] != '/' || strnlen(path, 4096) == 4096) { good = false; return; }
    if (emit) std::cout << "{\"event\":\"cache-file\",\"round\":" << round << ",\"path\":" << quote(path) << "}\n";
  });
  funcs.each_image(pointer, ^(opaque image) {
    if (++image_count > max_images) { good = false; return; }
    unsigned char id[16]{}; const bool has_uuid = funcs.uuid(image, &id);
    __block std::uint64_t text_address = 0, text_size = 0; __block unsigned texts = 0, segments = 0;
    const bool segmented = funcs.segments(image, ^(const char* name, std::uint64_t address, std::uint64_t size, int) {
      if (++segments > 64 || !name || strnlen(name, 32) == 32) { good = false; return; }
      if (std::strcmp(name, "__TEXT") == 0) { ++texts; text_address = address; text_size = size; }
    });
    if (!segmented || texts != 1 || !text_size) good = false;
    if (emit) std::cout << "{\"event\":\"spi-image\",\"round\":" << round
      << ",\"path\":" << quote(funcs.path(image)) << ",\"install_name\":" << quote(funcs.name(image))
      << ",\"uuid\":" << (has_uuid ? quote(hex(id, 16).c_str()) : "null")
      << ",\"text_address\":" << text_address << ",\"text_size\":" << text_size << "}\n";
    else { (void)funcs.path(image); (void)funcs.name(image); }
  });
  if (emit) {
    bool loader_seen = false;
    for (const auto& row : rows) if (row.active) {
      emit_header(row, round, "public-callback"); good = good && row.good;
      if (row.address == reinterpret_cast<std::uintptr_t>(loader.dyldImageLoadAddress)) loader_seen = true;
    }
    if (!loader_seen) {
      // dyld itself can be outside the cache. Derive its slide from its actual
      // header/TEXT metadata; downstream cache attribution must independently
      // agree with the active cache slide if it classifies this as cached.
      auto row = inspect(loader.dyldImageLoadAddress, 0);
      row.good = row.good && derive_slide(row);
      emit_header(row, round, "own-task-loader"); good = good && row.good;
    }
    const auto after = own_loader(); std::uint64_t final_generation;
    { std::lock_guard<std::mutex> lock(observed.mutex); final_generation = observed.generation; overflow = overflow || observed.overflow; }
    good = good && !overflow && generation == final_generation &&
      loader.infoArrayChangeTimestamp == after.infoArrayChangeTimestamp &&
      loader.dyldImageLoadAddress == after.dyldImageLoadAddress && loader.sharedCacheBaseAddress == after.sharedCacheBaseAddress &&
      loader.sharedCacheSlide == after.sharedCacheSlide && !std::memcmp(loader.sharedCacheUUID, after.sharedCacheUUID, 16);
    std::cout << "{\"event\":\"generation\",\"round\":" << round << ",\"before\":" << generation
      << ",\"after\":" << final_generation << ",\"overflow\":" << (overflow ? "true" : "false")
      << ",\"complete\":" << (good ? "true" : "false") << "}\n";
  }
  return good && file_count && image_count;
}
bool same(const struct stat& a, const struct stat& b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size && a.st_mode == b.st_mode &&
    a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
    a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}
void emit_identity(const struct stat& value) {
  std::cout << '[' << value.st_dev << ',' << value.st_ino << ',' << value.st_size << ','
    << static_cast<std::int64_t>(value.st_mtimespec.tv_sec) * 1000000000 + value.st_mtimespec.tv_nsec << ','
    << static_cast<std::int64_t>(value.st_ctimespec.tv_sec) * 1000000000 + value.st_ctimespec.tv_nsec << ']';
}
std::string crypto(const char* bytes, std::size_t size) {
  unsigned char result[CC_SHA256_DIGEST_LENGTH]{};
  if (!CC_SHA256(bytes, static_cast<CC_LONG>(size), result)) throw std::runtime_error("SHA256 control failed");
  return hex(result, sizeof(result));
}
void hash_controls() {
  if (crypto("", 0) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
      crypto("abc", 3) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    throw std::runtime_error("known SHA256 vectors differ");
  unsigned controls = 2;
  for (const std::size_t size : {1U, 55U, 56U, 63U, 64U, 65U, 65535U, 65536U, 65537U, 1048576U}) {
    std::string bytes(size, '\0'); for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<char>(i % 251);
    nil::detail::sha256 scalar; scalar.update(bytes.data(), size);
    if (scalar.finish() != crypto(bytes.data(), size)) throw std::runtime_error("scalar/CC boundary mismatch");
    ++controls;
  }
  std::cout << "{\"event\":\"hash-controls\",\"passed\":" << controls << ",\"algorithm\":\"SHA256\"}\n";
}
struct opened_file {
  std::string path; int fd; struct stat before{};
  opened_file(const char* name) : path(name), fd(open(name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)) {
    if (fd < 0) throw std::runtime_error("cache open failed: " + std::to_string(errno));
  }
  ~opened_file() { if (fd >= 0) close(fd); }
};
int hash_files(int count, char** paths) {
  if (count < 1 || count > static_cast<int>(max_files)) throw std::runtime_error("cache file count bound");
  const auto started = clock_type::now(), deadline = started + std::chrono::seconds(230);
  hash_controls();
  std::vector<std::unique_ptr<opened_file>> files;
  std::set<std::pair<dev_t, ino_t>> identities; std::uint64_t total = 0;
  for (int i = 0; i < count; ++i) {
    if (paths[i][0] != '/' || strnlen(paths[i], 4096) == 4096) throw std::runtime_error("nonabsolute/unbounded cache path");
    auto file = std::make_unique<opened_file>(paths[i]); struct stat named{};
    if (fstat(file->fd, &file->before) || lstat(paths[i], &named) || !S_ISREG(named.st_mode) ||
        !same(file->before, named) || file->before.st_size <= 0 ||
        static_cast<std::uint64_t>(file->before.st_size) > max_bytes - total ||
        !identities.emplace(named.st_dev, named.st_ino).second) throw std::runtime_error("cache identity/byte bound");
    total += static_cast<std::uint64_t>(file->before.st_size); files.push_back(std::move(file));
  }
  std::array<char, 1024 * 1024> buffer{}; std::vector<std::string> first;
  for (unsigned pass = 0; pass < 2; ++pass) {
    const auto pass_start = clock_type::now();
    for (std::size_t i = 0; i < files.size(); ++i) {
      const auto& file = *files[i]; const auto begin = clock_type::now();
      CC_SHA256_CTX context{}; if (CC_SHA256_Init(&context) != 1) throw std::runtime_error("CC init failed");
      std::uint64_t offset = 0, size = static_cast<std::uint64_t>(file.before.st_size);
      while (offset < size) {
        if (clock_type::now() >= deadline) throw std::runtime_error("whole hash deadline exceeded");
        const auto got = pread(file.fd, buffer.data(), std::min<std::uint64_t>(buffer.size(), size - offset), static_cast<off_t>(offset));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0 || CC_SHA256_Update(&context, buffer.data(), static_cast<CC_LONG>(got)) != 1)
          throw std::runtime_error("cache shortened/read/hash failure");
        offset += static_cast<std::uint64_t>(got);
      }
      unsigned char result[CC_SHA256_DIGEST_LENGTH]{}; struct stat after{}, named{}; char tail;
      if (CC_SHA256_Final(result, &context) != 1 || pread(file.fd, &tail, 1, static_cast<off_t>(size)) != 0 ||
          fstat(file.fd, &after) || lstat(file.path.c_str(), &named) || !same(file.before, after) || !same(after, named))
        throw std::runtime_error("cache changed during full hash");
      const auto digest = hex(result, sizeof(result));
      if (!pass) first.push_back(digest); else if (first[i] != digest) throw std::runtime_error("whole hash passes differ");
      std::cout << "{\"event\":\"whole-hash\",\"pass\":" << pass << ",\"path\":" << quote(file.path.c_str())
        << ",\"bytes\":" << size << ",\"sha256\":" << quote(digest.c_str()) << ",\"identity\":";
      emit_identity(after);
      std::cout << ",\"identity_unchanged\":true,\"seconds_including_pread\":"
        << std::chrono::duration<double>(clock_type::now() - begin).count() << "}\n" << std::flush;
    }
    std::cout << "{\"event\":\"hash-pass\",\"pass\":" << pass << ",\"files\":" << files.size()
      << ",\"bytes\":" << total << ",\"seconds_including_pread\":"
      << std::chrono::duration<double>(clock_type::now() - pass_start).count() << "}\n" << std::flush;
  }
  for (const auto& file : files) {
    struct stat after{}, named{};
    if (fstat(file->fd, &after) || lstat(file->path.c_str(), &named) || !same(file->before, after) || !same(after, named))
      throw std::runtime_error("cache changed before final hash observation");
  }
  std::cout << "{\"event\":\"hash-ended\",\"complete\":true,\"files\":" << files.size()
    << ",\"bytes_per_pass\":" << total << ",\"total_hashed_bytes\":" << 2 * total
    << ",\"seconds\":" << std::chrono::duration<double>(clock_type::now() - started).count() << "}\n";
  return 0;
}
}
int main(int argc, char** argv) {
  try {
    if (argc > 2 && !std::strcmp(argv[1], "--hash-files")) return hash_files(argc - 2, argv + 2);
    if (argc != 1) throw std::runtime_error("usage: probe [--hash-files ABSOLUTE_CACHE ...]");
    hash_controls(); api funcs; auto process = funcs.create();
    if (!process) throw std::runtime_error("own-process handle unavailable");
    std::unique_ptr<void, void(*)(void*)> owner(process, funcs.dispose);
    if (!snapshot(funcs, process, -1)) throw std::runtime_error("SPI warmup incomplete");
    observed.rows.reserve(max_images);
    _dyld_register_func_for_remove_image(removed); _dyld_register_func_for_add_image(added);
    const bool first = snapshot(funcs, process, 0), second = snapshot(funcs, process, 1);
    std::cout << "{\"event\":\"images-ended\",\"complete\":" << (first && second ? "true" : "false") << "}\n";
    return first && second ? 0 : 2;
  } catch (const std::exception& error) {
    std::cout << "{\"event\":\"incomplete\",\"error\":" << quote(error.what()) << "}\n" << std::flush;
    return 2;
  }
}
