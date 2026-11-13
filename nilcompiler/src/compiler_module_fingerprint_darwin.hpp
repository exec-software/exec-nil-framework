#pragma once

// Bounded, portable parsing and attribution for the Darwin module collector.
// UUIDs select backing files; the builder hashes their entire contents. Neither
// UUIDs nor timestamps are persistent cache identities.
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace nil::detail::compiler_modules::darwin {
using uuid = std::array<unsigned char, 16>;
using bytes = std::vector<unsigned char>;
constexpr std::size_t maximum_images = 16384, maximum_files = 128;
constexpr std::uint64_t maximum_bytes = 8ULL * 1024 * 1024 * 1024;
inline void require(bool condition) { if (!condition) throw std::runtime_error("incomplete Darwin module identity"); }
inline std::uint64_t end(std::uint64_t begin, std::uint64_t size,
                         std::uint64_t limit = UINT64_MAX) {
  require(begin <= limit && size <= limit - begin);
  return begin + size;
}
inline std::uint64_t number(const bytes &data, std::size_t offset, std::size_t width) {
  require(width <= 8 && offset <= data.size() && width <= data.size() - offset);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i) value |= std::uint64_t(data[offset + i]) << (8 * i);
  return value;
}
inline uuid identity(const bytes &data, std::size_t offset) {
  require(offset <= data.size() && 16 <= data.size() - offset);
  uuid value{};
  std::copy_n(data.begin() + offset, value.size(), value.begin());
  require(value != uuid{});
  return value;
}
struct mapping {
  std::uint64_t address = 0, size = 0, offset = 0;
  std::uint32_t maximum = 0, initial = 0;
};
struct subcache { uuid id{}; std::uint64_t offset = 0; std::string suffix; };
struct cache_image { uuid id{}; std::uint64_t address = 0, size = 0; };
struct cache_file {
  std::string path, magic;
  uuid id{};
  std::uint64_t size = 0, base = 0, region_size = 0;
  std::vector<mapping> mappings;
  std::vector<subcache> children;
  std::vector<cache_image> images;
};
using reader = std::function<bytes(std::uint64_t, std::size_t)>;
inline void disjoint(std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges) {
  std::sort(ranges.begin(), ranges.end());
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    const auto finish = end(ranges[i].first, ranges[i].second);
    require(ranges[i].second != 0 && (i + 1 == ranges.size() || finish <= ranges[i + 1].first));
  }
}
inline cache_file parse(const std::string &path, std::uint64_t size,
                        const reader &read, bool image_table = false) {
  require(!path.empty() && path.front() == '/' && path.find('\0') == std::string::npos && size && size <= maximum_bytes);
  const auto first = read(0, 24);
  require(first.size() == 24);
  std::string magic(first.begin(), first.begin() + 16);
  while (!magic.empty() && (magic.back() == '\0' || magic.back() == ' ')) magic.pop_back();
  require(magic == "dyld_v1  arm64e" || magic == "dyld_v1   arm64" ||
          magic == "dyld_v1  x86_64" || magic == "dyld_v1 x86_64h");
  const auto header_size = number(first, 16, 4), count = number(first, 20, 4);
  require(header_size >= 400 && header_size <= 4096 && count > 0 && count <= maximum_files);
  const auto header = read(0, static_cast<std::size_t>(header_size));
  require(header.size() == header_size);
  cache_file result;
  result.path = path; result.magic = magic; result.size = size;
  result.id = identity(header, 88);
  result.base = number(header, 224, 8); result.region_size = number(header, 232, 8);
  end(result.base, result.region_size);
  const auto table = [&](std::uint64_t offset, std::uint64_t entries, std::size_t stride,
                         std::size_t maximum = maximum_files) {
    require(entries <= maximum && entries <= UINT64_MAX / stride);
    end(offset, entries * stride, size);
    auto data = read(offset, static_cast<std::size_t>(entries * stride));
    require(data.size() == entries * stride);
    return data;
  };
  const auto mappings = table(header_size, count, 32);
  std::vector<std::pair<std::uint64_t, std::uint64_t>> addresses, offsets;
  for (std::size_t i = 0; i < count; ++i) {
    const auto at = i * 32;
    mapping item{number(mappings, at, 8), number(mappings, at + 8, 8), number(mappings, at + 16, 8),
                 static_cast<std::uint32_t>(number(mappings, at + 24, 4)), static_cast<std::uint32_t>(number(mappings, at + 28, 4))};
    require(item.size && !(item.initial & ~item.maximum));
    end(item.offset, item.size, size);
    addresses.emplace_back(item.address, item.size); offsets.emplace_back(item.offset, item.size);
    result.mappings.push_back(item);
  }
  disjoint(addresses); disjoint(offsets);
  const auto slide_count = number(header, 316, 4);
  require(slide_count == count);
  const auto slides = table(number(header, 312, 4), slide_count, 56);
  for (std::size_t i = 0; i < count; ++i) {
    const auto at = i * 56;
    const auto &item = result.mappings[i];
    require(item.address == number(slides, at, 8) && item.size == number(slides, at + 8, 8) &&
            item.offset == number(slides, at + 16, 8) && item.maximum == number(slides, at + 48, 4) &&
            item.initial == number(slides, at + 52, 4));
    end(number(slides, at + 24, 8), number(slides, at + 32, 8), size);
  }
  const bool modern = header_size > 456;
  const auto children_count = number(header, 396, 4);
  const std::size_t stride = modern ? 56 : 24;
  const auto children = table(number(header, 392, 4), children_count, stride);
  std::set<uuid> ids;
  std::set<std::string> suffixes;
  std::set<std::uint64_t> child_offsets;
  for (std::size_t i = 0; i < children_count; ++i) {
    subcache child{identity(children, i * stride), number(children, i * stride + 16, 8), {}};
    if (modern) {
      const auto begin = children.begin() + i * stride + 24;
      const auto finish = std::find(begin, begin + 32, 0);
      require(finish != begin + 32);
      child.suffix.assign(begin, finish);
      require(child.suffix.size() > 1 && child.suffix.front() == '.' && child.suffix.back() != '.' &&
              child.suffix.find("..") == std::string::npos &&
              child.suffix.find_first_not_of(".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == std::string::npos);
    } else child.suffix = "." + std::to_string(i + 1);
    require(child.id != result.id && child.offset && ids.insert(child.id).second &&
            suffixes.insert(child.suffix).second && child_offsets.insert(child.offset).second);
    result.children.push_back(std::move(child));
  }
  end(number(header, 40, 8), number(header, 48, 8), size);
  end(number(header, 72, 8), number(header, 80, 8), size);
  if (image_table) {
    const auto image_count = number(header, 144, 8);
    require(image_count > 0);
    const auto images = table(number(header, 136, 8), image_count, 32, maximum_images);
    std::set<std::uint64_t> image_addresses;
    for (std::size_t i = 0; i < image_count; ++i) {
      cache_image image{identity(images, i * 32), number(images, i * 32 + 16, 8), number(images, i * 32 + 24, 4)};
      require(image.size && image_addresses.insert(image.address).second);
      end(image.address, image.size);
      result.images.push_back(image);
    }
  }
  return result;
}
struct loaded_image {
  uuid id{};
  std::uint64_t address = 0, text_size = 0;
  std::int64_t slide = 0;
  std::uint32_t cpu = 0, flags = 0;
  std::string ordinary_path;
};
inline std::int64_t image_slide(std::uint64_t address, std::uint64_t unslid) {
  if (address >= unslid) {
    require(address - unslid <= static_cast<std::uint64_t>(INT64_MAX));
    return static_cast<std::int64_t>(address - unslid);
  }
  const auto magnitude = unslid - address;
  require(magnitude <= (std::uint64_t{1} << 63));
  return magnitude == (std::uint64_t{1} << 63) ? INT64_MIN : -static_cast<std::int64_t>(magnitude);
}
struct observation {
  uuid cache_id{};
  std::uint64_t base = 0, size = 0, slide = 0, loader = 0;
  std::vector<loaded_image> images;
};
// This attribution is equivalent to Linux/Windows loaded-path identity, not
// vnode attestation. The whole selected files remain subject to content hashing
// and the builder's independent post-compilation graph/content verification.
inline std::set<std::string> attribute(const observation &active,
                                        const std::vector<cache_file> &caches,
                                        std::uint32_t cpu) {
  require(!caches.empty() && caches.size() <= maximum_files && !active.images.empty() && active.images.size() <= 4096);
  const cache_file *root = nullptr;
  std::map<std::string, const cache_file *> by_path;
  std::set<uuid> cache_ids;
  std::uint64_t total = 0;
  for (const auto &file : caches) {
    require(by_path.emplace(file.path, &file).second && cache_ids.insert(file.id).second);
    total = end(total, file.size, maximum_bytes);
    if (file.id == active.cache_id) { require(!root); root = &file; }
  }
  require(root && root->children.size() + 1 == caches.size() && root->base &&
          root->region_size == active.size && end(root->base, active.slide) == active.base);
  const auto first_mapping = [](const cache_file &file) {
    require(!file.mappings.empty());
    return std::min_element(file.mappings.begin(), file.mappings.end(),
        [](const auto &a, const auto &b) { return a.address < b.address; })->address;
  };
  require(first_mapping(*root) == root->base && !root->images.empty());
  for (const auto &child : root->children) {
    const auto found = by_path.find(root->path + child.suffix);
    require(found != by_path.end());
    const auto &file = *found->second;
    require(file.id == child.id && file.children.empty() && file.magic == root->magic &&
            first_mapping(file) == end(root->base, child.offset));
  }
  std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
  for (const auto &file : caches) for (const auto &map : file.mappings) {
    require(map.address >= root->base);
    end(map.address - root->base, map.size, root->region_size);
    ranges.emplace_back(map.address, map.size);
  }
  disjoint(ranges);
  std::map<std::uint64_t, const cache_image *> images;
  for (const auto &image : root->images) require(images.emplace(image.address, &image).second);
  std::set<std::string> result;
  for (const auto &file : caches) result.insert(file.path);
  std::set<std::uint64_t> seen;
  bool loader_seen = false, cached = false;
  const bool arm = root->magic.find("arm64") != std::string::npos;
  require(cpu == (arm ? 0x0100000cU : 0x01000007U));
  for (const auto &image : active.images) {
    require(image.cpu == cpu && image.address && image.text_size && seen.insert(image.address).second);
    loader_seen |= image.address == active.loader;
    const mapping *mapped = nullptr;
    for (const auto &file : caches) for (const auto &map : file.mappings) {
      const auto begin = end(map.address, active.slide);
      if (image.address >= begin && image.address < end(begin, map.size)) { require(!mapped); mapped = &map; }
    }
    if (mapped) {
      require(image.address >= active.slide && image.slide >= 0 && static_cast<std::uint64_t>(image.slide) == active.slide && (image.flags & 0x80000000U));
      const auto unslid = image.address - active.slide;
      const auto found = images.find(unslid);
      require(found != images.end() && found->second->id == image.id && found->second->size == image.text_size &&
              (mapped->initial & 5) == 5 && end(unslid, image.text_size) <= end(mapped->address, mapped->size));
      cached = true;
    } else {
      require(!(image.address >= active.base && image.address < end(active.base, active.size)));
      require(!image.ordinary_path.empty() && image.ordinary_path.front() == '/' && image.ordinary_path.find('\0') == std::string::npos);
      result.insert(image.ordinary_path);
    }
  }
  require(loader_seen && cached);
  return result;
}
} // namespace nil::detail::compiler_modules::darwin
