#include "../nilcompiler/src/compiler_module_fingerprint.hpp"
#include "../nilcompiler/src/compiler_module_fingerprint_darwin.hpp"
#include "../nilbuild/digest_phase.hpp"
#include "../common/private_directory.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace d = nil::detail::compiler_modules::darwin;
namespace fs = std::filesystem;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class Function> void rejected(Function invoke, const char *message) {
  bool failed = false;
  try { invoke(); } catch (const std::exception &) { failed = true; }
  check(failed, message);
}
void put(d::bytes &data, std::size_t offset, std::uint64_t value, std::size_t width = 8) {
  for (std::size_t i = 0; i < width; ++i) data.at(offset + i) = static_cast<unsigned char>(value >> (8 * i));
}
d::uuid id(unsigned char value) { d::uuid result{}; result.back() = value; return result; }
void put_id(d::bytes &data, std::size_t offset, unsigned char value) { data.at(offset + 15) = value; }
d::bytes cache(bool root) {
  d::bytes data(8192);
  const std::string magic = "dyld_v1  arm64e";
  std::copy(magic.begin(), magic.end(), data.begin());
  put(data, 16, 512, 4); put(data, 20, 1, 4); put_id(data, 88, root ? 1 : 2);
  put(data, 224, 0x100000); put(data, 232, 0x10000);
  put(data, 312, 544, 4); put(data, 316, 1, 4);
  for (const auto offset : {512U, 544U}) {
    put(data, offset, root ? 0x100000 : 0x108000); put(data, offset + 8, 8192);
    put(data, offset + (offset == 512 ? 24 : 48), 5, 4);
    put(data, offset + (offset == 512 ? 28 : 52), 5, 4);
  }
  if (root) {
    put(data, 392, 600, 4); put(data, 396, 1, 4); put_id(data, 600, 2); put(data, 616, 0x8000);
    data[624] = '.'; data[625] = '1';
    put(data, 136, 1024); put(data, 144, 2);
    put_id(data, 1024, 3); put(data, 1040, 0x101000); put(data, 1048, 0x100, 4);
    put_id(data, 1056, 4); put(data, 1072, 0x109000); put(data, 1080, 0x100, 4);
  }
  return data;
}
d::cache_file parse(const d::bytes &data, bool root) {
  return d::parse(root ? "/cache" : "/cache.1", data.size(), [&](std::uint64_t offset, std::size_t size) {
    d::end(offset, size, data.size());
    return d::bytes(data.begin() + offset, data.begin() + offset + size);
  }, root);
}
d::observation active() {
  d::observation result;
  result.cache_id = id(1); result.base = 0x1100000; result.size = 0x10000;
  result.slide = 0x1000000; result.loader = 0x1109000;
  result.images = {{id(3), 0x1101000, 0x100, 0x1000000, 0x100000c, 0x80000000U, {}},
                   {id(4), result.loader, 0x100, 0x1000000, 0x100000c, 0x80000000U, {}},
                   {id(3), 0x5000000, 0x100, 0, 0x100000c, 0x80000000U, "/ordinary"}};
  return result;
}
void attribution() {
  const auto root = cache(true), child = cache(false);
  const std::vector<d::cache_file> files{parse(root, true), parse(child, false)};
  check(d::attribute(active(), files, 0x100000c) == std::set<std::string>{"/cache", "/cache.1", "/ordinary"},
        "complete backing set and same-UUID outside-cache image stay content inputs");
  auto ordinary_loader = active();
  ordinary_loader.loader = ordinary_loader.images.back().address;
  ordinary_loader.images.back().slide = -4096;
  check(d::attribute(ordinary_loader, files, 0x100000c).count("/ordinary"), "ordinary dyld uses its own slide and full file identity");
  ordinary_loader.images.back().ordinary_path.clear();
  rejected([&] { d::attribute(ordinary_loader, files, 0x100000c); }, "ordinary dyld without readable-path attribution is incomplete");
  check(d::image_slide(0x1000, 0x2000) == -4096 && d::image_slide(0x2000, 0x1000) == 4096,
        "actual header derives positive and negative ordinary loader slides");
  auto mutate_image = [&](auto mutation) { auto value = active(); mutation(value); d::attribute(value, files, 0x100000c); };
  rejected([&] { mutate_image([](auto &v) { ++v.images[0].address; }); }, "off-by-one header cannot match cache image");
  rejected([&] { mutate_image([](auto &v) { ++v.images[0].slide; }); }, "wrong slide cannot match cache image");
  rejected([&] { mutate_image([](auto &v) { v.images[0].id = id(8); }); }, "wrong UUID cannot match cache image");
  rejected([&] { mutate_image([](auto &v) { v.images[0].address = v.base + 0x4000; }); }, "envelope gap is not a cache mapping");
  rejected([&] { mutate_image([](auto &v) { v.images[0].text_size = 0x2000; }); }, "TEXT bounds cannot cross mappings");
  rejected([&] { mutate_image([](auto &v) { v.images[0].flags = 0; }); }, "cached image flag required");
  rejected([&] { mutate_image([](auto &v) { v.images[2].ordinary_path.clear(); }); }, "ordinary image needs actual path");
  rejected([&] { mutate_image([](auto &v) { v.images.erase(v.images.begin() + 1); }); }, "dyld loader must be accounted for");
  rejected([&] { mutate_image([](auto &v) { v.images.push_back(v.images[0]); }); }, "duplicate header rejected");
  rejected([&] { mutate_image([](auto &v) { v.cache_id = id(7); }); }, "wrong active root rejected");
  rejected([&] { mutate_image([](auto &v) { ++v.size; }); }, "active and disk cache bounds must match");
  auto mutate_files = [&](auto mutation) { auto value = files; mutation(value); d::attribute(active(), value, 0x100000c); };
  rejected([&] { mutate_files([](auto &v) { v.pop_back(); }); }, "missing backing file rejected");
  rejected([&] { mutate_files([](auto &v) { v.push_back(v[1]); }); }, "duplicate backing file rejected");
  rejected([&] { mutate_files([](auto &v) { v[1].id = id(5); }); }, "subcache UUID correspondence required");
  rejected([&] { mutate_files([](auto &v) { v[1].path += ".other"; }); }, "root suffix set must match exact file list");
  rejected([&] { mutate_files([](auto &v) { v[1].mappings[0].address = 0x100000; }); }, "overlapping cache mappings rejected");
  rejected([&] { mutate_files([](auto &v) { v[0].images.push_back(v[0].images[0]); }); }, "duplicate image table address rejected");
  rejected([&] { mutate_files([](auto &v) { v[0].children[0].offset += 1; }); }, "subcache VM offset correspondence required");
  rejected([&] { d::attribute(active(), files, 0x1000007); }, "target architecture must match cache");
  for (const auto offset : {16U, 20U, 312U, 316U, 392U, 396U, 136U, 144U}) {
    auto invalid = root; put(invalid, offset, UINT32_MAX, 4);
    rejected([&] { parse(invalid, true); }, "out-of-range metadata rejected");
  }
  auto truncated = root; truncated.resize(100);
  rejected([&] { parse(truncated, true); }, "truncated metadata rejected");
  auto overflow = root; put(overflow, 512, UINT64_MAX - 5);
  rejected([&] { parse(overflow, true); }, "overflowing address rejected");
  auto suffix = root; suffix[626] = '/';
  rejected([&] { parse(suffix, true); }, "subcache suffix cannot introduce path traversal");
}
void digests(const fs::path &directory) {
  check(nil::detail::content_digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty SHA256 vector");
  check(nil::detail::content_digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc SHA256 vector");
  check(nil::detail::content_digest(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "million-a SHA256 vector");
  for (const std::size_t size : {1U, 55U, 56U, 63U, 64U, 65U, 4095U, 4096U, 65535U, 65536U, 65537U, 1048593U}) {
    std::string value(size, '\0');
    for (std::size_t i = 0; i < size; ++i) value[i] = static_cast<char>(i * 31);
    nil::detail::portable_sha256 portable;
    portable.update(value.data(), value.size());
    const auto expected = portable.finish();
    check(nil::detail::content_digest(value) == expected, "platform and scalar SHA256 agree at boundaries");
    const auto file = directory / "input";
    { std::ofstream output(file, std::ios::binary); output.write(value.data(), value.size()); }
    check(nil::detail::file_digest(file) == expected, "file SHA256 hashes every byte including NUL");
  }
  const auto file = directory / "input";
  int cancellation = 0;
  rejected([&] { nil::detail::file_digest(file, [&] { if (++cancellation == 3) throw std::runtime_error("cancel"); }); }, "hash checks cancellation between bounded chunks");
  check(cancellation == 3, "cancellation stops the hash immediately");
  nilbuild::digest_phase prepared;
  unsigned reads = 0;
  const auto read = [&](const fs::path &path) { ++reads; return nil::detail::file_digest(path); };
  const auto before = prepared.read(file, read);
  check(prepared.read(file, read) == before && reads == 1, "same preparation phase coalesces shared input");
  const auto stamp = fs::last_write_time(file);
  { std::fstream output(file, std::ios::binary | std::ios::in | std::ios::out); output.put('Z'); }
  fs::last_write_time(file, stamp);
  nilbuild::digest_phase verified;
  check(verified.read(file, read) != before && reads == 2, "verification reads new bytes despite same length and timestamp");
  nilbuild::digest_phase next_build;
  check(next_build.read(file, read) == verified.read(file, read) && reads == 3, "new invocation cannot reuse a previous phase digest");
  nilbuild::digest_phase overlap;
  overlap.exclude(file);
  const auto old_reads = reads;
  const auto current = overlap.read(file, read);
  { std::fstream output(file, std::ios::binary | std::ios::in | std::ios::out); output.put('Q'); }
  fs::last_write_time(file, stamp);
  check(overlap.read(file, read) != current && reads == old_reads + 2 && !overlap.contains(file.string()),
        "consumed source advertised as a shared module is never memoized");
  const auto alias =
#ifdef _WIN32
      directory / "." / file.filename();
#else
      directory / "alias";
  fs::create_symlink(file.filename(), alias);
#endif
  const auto alias_reads = reads;
  (void)overlap.read(alias, read); (void)overlap.read(alias, read);
  check(reads == alias_reads + 2 && !overlap.contains(alias.string()), "source alias cannot bypass memo exclusion");
  nilbuild::digest_phase overlap_verification;
  overlap_verification.inherit_exclusions(overlap);
  const auto verification_reads = reads;
  (void)overlap_verification.read(alias, read); (void)overlap_verification.read(alias, read);
  check(reads == verification_reads + 2, "verification inherits source exclusions without preparation digests");
  const auto hard_link = directory / "hard-link";
  fs::create_hard_link(file, hard_link);
  const auto hard_link_reads = reads;
  (void)overlap_verification.read(hard_link, read); (void)overlap_verification.read(hard_link, read);
  check(reads == hard_link_reads + 2 && !overlap_verification.contains(hard_link.string()),
        "source hard-link alias cannot enter the shared memo");
}
} // namespace
int main() {
  const auto directory = nil::detail::create_private_directory(fs::temp_directory_path(), "nil-darwin-modules-");
  try {
    attribution(); digests(directory);
#ifdef __APPLE__
    nil::detail::compiler_modules::files first, second;
    check(nil::detail::compiler_modules::collect_darwin(first), "actual Darwin module attribution completes");
    check(nil::detail::compiler_modules::collect_darwin(second) && first == second, "actual Darwin module inventory is stable");
    check(std::any_of(first.begin(), first.end(), [](const auto &path) { return path.find("dyld_shared_cache") != std::string::npos; }), "actual backing files are ordinary digest inputs");
#endif
    fs::remove_all(directory);
    std::cout << "Darwin module identity and digest phases: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code ignored; fs::remove_all(directory, ignored);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
