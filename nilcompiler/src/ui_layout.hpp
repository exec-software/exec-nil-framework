#pragma once

// Toolkit-independent allocation for the checked ui.v1 row/column/grid model.
// Coordinates and node counts have the same bounds as the public provider.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ny::ui::layout {
using Pixel = std::int64_t;

// Native widget coordinates are signed 32-bit integers on Win32. Extremely
// sparse/nested valid grids can exceed that addressable extent; keep their
// requisitions and off-screen coordinates representable on every provider.
inline Pixel native_extent(Pixel value) noexcept {
  return std::max(Pixel(0), std::min(Pixel(std::numeric_limits<std::int32_t>::max()), value));
}

inline bool expands(std::optional<bool> local, bool visible, bool descendants) {
  return visible && local.value_or(descendants);
}

struct Item {
  std::int64_t start = 0, span = 1;
  Pixel natural = 0;
  bool expand = false, visible = true;
};

struct Axis {
  Pixel natural = 0, allocated = 0;
  std::vector<Pixel> positions, sizes;

  Pixel extent(std::int64_t start, std::int64_t span) const {
    const auto last = static_cast<std::size_t>(start + span - 1);
    return positions.at(last) + sizes.at(last) - positions.at(static_cast<std::size_t>(start));
  }
};

namespace detail {
// Range additions and range sums over the occupied tracks. A second instance
// indexes only expanding tracks, so a 65,536-cell span needs logarithmic work
// even when its spare pixels belong to a sparse subset of tracks.
class Additions {
  std::vector<Pixel> delta_, weighted_;
  static void add(std::vector<Pixel> &tree, std::size_t index, Pixel value) {
    for (; index < tree.size(); index += index & (~index + 1)) tree[index] += value;
  }
  static Pixel sum(const std::vector<Pixel> &tree, std::size_t index) {
    Pixel value = 0;
    for (; index; index -= index & (~index + 1)) value += tree[index];
    return value;
  }
  Pixel prefix(std::size_t end) const {
    return static_cast<Pixel>(end) * sum(delta_, end) - sum(weighted_, end);
  }
public:
  explicit Additions(std::size_t count) : delta_(count + 1), weighted_(count + 1) {}
  void increase(std::size_t begin, std::size_t end, Pixel amount) {
    if (begin == end || !amount) return;
    add(delta_, begin + 1, amount); add(delta_, end + 1, -amount);
    add(weighted_, begin + 1, amount * static_cast<Pixel>(begin));
    add(weighted_, end + 1, -amount * static_cast<Pixel>(end));
  }
  Pixel total(std::size_t begin, std::size_t end) const { return prefix(end) - prefix(begin); }
  Pixel at(std::size_t index) const { return sum(delta_, index + 1); }
};
} // namespace detail

inline Axis arrange(const std::vector<Item> &items, Pixel spacing, Pixel available = 0) {
  if (items.size() > 4096 || spacing < 0 || spacing > 4096 || available < 0 ||
      available > std::numeric_limits<std::int32_t>::max())
    throw std::invalid_argument("UI layout bounds exceeded");
  std::size_t extent = 0;
  for (const auto &item : items) {
    if (item.start < 0 || item.span < 1 || item.start > 65535 || item.span > 65536 - item.start ||
        item.natural < 0 || item.natural > std::numeric_limits<std::int32_t>::max())
      throw std::invalid_argument("UI layout item bounds exceeded");
    if (item.visible) extent = std::max(extent, static_cast<std::size_t>(item.start + item.span));
  }
  Axis result;
  result.positions.resize(extent); result.sizes.resize(extent);
  if (!extent) return result;
  std::vector<int> coverage(extent + 1), requested_expand(extent + 1);
  std::vector<Pixel> initial(extent);
  std::vector<bool> single_expand(extent);
  for (const auto &item : items) {
    if (!item.visible) continue;
    const auto begin = static_cast<std::size_t>(item.start), end = begin + static_cast<std::size_t>(item.span);
    ++coverage[begin]; --coverage[end];
    if (item.span == 1) {
      initial[begin] = std::max(initial[begin], item.natural);
      single_expand[begin] = single_expand[begin] || item.expand;
    }
  }
  std::vector<std::size_t> single_prefix(extent + 1);
  for (std::size_t index = 0; index < extent; ++index)
    single_prefix[index + 1] = single_prefix[index] + (single_expand[index] ? 1 : 0);
  // An expanding span uses an already-expanding track if it has one. Otherwise
  // all its tracks may expand. Applying these requests together avoids order
  // dependence between different spanning controls.
  for (const auto &item : items) {
    if (!item.visible || !item.expand || item.span == 1) continue;
    const auto begin = static_cast<std::size_t>(item.start), end = begin + static_cast<std::size_t>(item.span);
    if (single_prefix[end] == single_prefix[begin]) { ++requested_expand[begin]; --requested_expand[end]; }
  }
  std::vector<std::size_t> occupied_prefix(extent + 1), expanded_prefix(1, 0);
  std::vector<Pixel> base_prefix(1, 0);
  std::vector<bool> expanding;
  int covered = 0, requested = 0;
  for (std::size_t index = 0; index < extent; ++index) {
    covered += coverage[index]; requested += requested_expand[index];
    occupied_prefix[index + 1] = occupied_prefix[index] + (covered ? 1 : 0);
    if (!covered) continue;
    expanding.push_back(single_expand[index] || requested > 0);
    expanded_prefix.push_back(expanded_prefix.back() + (expanding.back() ? 1 : 0));
    base_prefix.push_back(base_prefix.back() + initial[index]);
  }
  const auto count = expanding.size();
  detail::Additions all(count), expanded(expanded_prefix.back());
  const auto sum = [&](std::size_t begin, std::size_t end) {
    return base_prefix[end] - base_prefix[begin] + all.total(begin, end) +
           expanded.total(expanded_prefix[begin], expanded_prefix[end]);
  };
  const auto distribute = [&](std::size_t begin, std::size_t end, Pixel extra, bool use_expanding) {
    auto &changes = use_expanding ? expanded : all;
    if (use_expanding) { begin = expanded_prefix[begin]; end = expanded_prefix[end]; }
    const auto selected = static_cast<Pixel>(end - begin);
    if (extra <= 0 || !selected) return;
    changes.increase(begin, end, extra / selected);
    changes.increase(begin, begin + static_cast<std::size_t>(extra % selected), 1);
  };
  for (const auto &item : items) {
    if (!item.visible || item.span == 1) continue;
    const auto begin = occupied_prefix[static_cast<std::size_t>(item.start)];
    const auto end = occupied_prefix[static_cast<std::size_t>(item.start + item.span)];
    const Pixel current = sum(begin, end) + spacing * static_cast<Pixel>(end - begin - 1);
    distribute(begin, end, item.natural - current, expanded_prefix[end] != expanded_prefix[begin]);
  }
  result.natural = sum(0, count) + spacing * static_cast<Pixel>(count - 1);
  if (available > result.natural && expanded_prefix.back())
    distribute(0, count, available - result.natural, true);
  std::size_t active = 0;
  for (std::size_t coordinate = 0; coordinate < extent; ++coordinate) {
    if (occupied_prefix[coordinate + 1] == occupied_prefix[coordinate]) continue;
    if (active) result.allocated += spacing;
    result.positions[coordinate] = result.allocated;
    const Pixel size = initial[coordinate] + all.at(active) +
        (expanding[active] ? expanded.at(expanded_prefix[active]) : 0);
    result.sizes[coordinate] = size; result.allocated += size; ++active;
  }
  return result;
}
} // namespace ny::ui::layout
