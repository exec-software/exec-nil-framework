#include "../nilcompiler/src/ui_layout.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) { std::cerr << "UI layout: " << message << '\n'; std::exit(1); }
}
using ny::ui::layout::arrange;
using ny::ui::layout::expands;
using ny::ui::layout::Item;

void invariants(const std::vector<Item> &items, std::int64_t spacing, std::int64_t available) {
  const auto measured = arrange(items, spacing), placed = arrange(items, spacing, available);
  check(placed.natural == measured.natural, "allocation does not alter requisition");
  std::vector<bool> occupied(placed.sizes.size());
  bool expandable = false;
  for (const auto &item : items) {
    if (!item.visible) continue;
    expandable = expandable || item.expand;
    check(placed.extent(item.start, item.span) >= item.natural, "every spanning control fits its full requisition");
    for (std::int64_t i = 0; i < item.span; ++i) occupied[static_cast<std::size_t>(item.start + i)] = true;
  }
  std::int64_t end = 0; bool previous = false;
  for (std::size_t index = 0; index < occupied.size(); ++index) {
    if (!occupied[index]) continue;
    check(placed.sizes[index] >= 0, "track size is nonnegative");
    check(placed.positions[index] == end + (previous ? spacing : 0), "occupied tracks have exactly one gap and never overlap");
    end = placed.positions[index] + placed.sizes[index]; previous = true;
  }
  check(end == placed.allocated, "all allocated pixels are accounted for");
  check(placed.allocated == (expandable ? std::max(available, measured.natural) : measured.natural),
        "spare pixels go only to expanding tracks with no rounding loss");
}
}

int main() {
  const auto lone = arrange({{0, 3, 101, false}}, 5);
  check(lone.natural == 101 && lone.extent(0, 3) == 101, "lone spanning control is not divided down to its first cell");
  check(lone.sizes.size() == 3 && lone.sizes[0] == 31 && lone.sizes[1] == 30 && lone.sizes[2] == 30,
        "span remainder uses deterministic integer pixels");
  const auto empty = arrange({{0, 1, 900, true, false}}, 7, 300);
  check(empty.natural == 0 && empty.allocated == 0 && empty.sizes.empty(), "hidden-only container has no dimensions or gaps");
  const auto shown = arrange({{0, 1, 40, false}, {1, 1, 20, true}, {2, 1, 30, false}}, 5, 200);
  const auto hidden = arrange({{0, 1, 40, false, false}, {1, 1, 20, true}, {2, 1, 30, false}}, 5, 200);
  check(hidden.sizes[1] == shown.sizes[1] + 45 && hidden.positions[1] == 0,
        "hiding a sibling releases its full requisition and gap to the expanding control");
  const auto mixed = arrange({{0, 1, 10, false}, {1, 1, 20, true}, {0, 3, 90, true}}, 3, 240);
  check(mixed.sizes[0] == 10 && mixed.sizes[2] == 0 && mixed.sizes[1] == 224,
        "expanding span uses an existing expanding track without growing fixed tracks");
  const auto last = arrange({{65535, 1, 17, false}}, 4096);
  check(last.natural == 17 && last.positions[65535] == 0, "unused coordinate gaps do not reserve empty tracks");
  const auto maximum_span = arrange({{0, 65536, 65537, true}}, 0, 131073);
  check(maximum_span.natural == 65537 && maximum_span.extent(0, 65536) == 131073 && maximum_span.sizes[0] == 3,
        "full coordinate-range span preserves requisition and expansion remainder");

  const bool leaf = expands(true, true, false);
  check(expands(std::nullopt, true, expands(std::nullopt, true, leaf)), "nested containers inherit descendant expansion by default");
  check(!expands(false, true, leaf), "explicit false overrides descendant expansion");
  check(!expands(std::nullopt, true, expands(true, false, false)), "hidden descendants cannot request expansion");
  check(expands(true, true, false), "explicit true expands an empty visible container");
  check(!expands(true, false, true), "hidden container does not expand");

  std::int64_t nested = 1;
  for (unsigned level = 0; level < 12; ++level) {
    const auto grid = arrange({{0, 1, nested}, {1, 65535, 1}}, 4096);
    const auto next = ny::ui::layout::native_extent(grid.natural);
    check(next >= nested && next <= std::numeric_limits<std::int32_t>::max(),
          "nested sparse grids saturate representable native requisitions without wrapping");
    nested = next;
  }
  check(nested == std::numeric_limits<std::int32_t>::max(), "nested maximum spans reach the native coordinate limit safely");

  // A small independently checked domain covers overlapping axis spans from
  // controls on different rows, sparse coordinates, hidden nodes and resizing.
  std::mt19937 random(0x4e494cU);
  for (unsigned trial = 0; trial < 160; ++trial) {
    std::vector<Item> items;
    for (unsigned i = 0, count = 1 + random() % 20; i < count; ++i) {
      const auto start = static_cast<std::int64_t>(random() % 48);
      items.push_back({start, static_cast<std::int64_t>(1 + random() % (64 - start)), static_cast<std::int64_t>(random() % 400),
                       random() % 3 == 0, random() % 4 != 0});
    }
    invariants(items, random() % 12, random() % 3000);
  }
  // The public maxima must not turn each span into a 65,536-step scan.
  std::vector<Item> maximal;
  for (std::int64_t index = 0; index < 4096; ++index) maximal.push_back({0, 65536, 65536 + index, index == 0});
  const auto bounded = arrange(maximal, 1, 500000);
  check(bounded.natural == 69631 && bounded.allocated == 500000,
        "4096 full-range spanning constraints remain bounded and conserve allocation");
  for (const auto &item : maximal)
    check(bounded.extent(item.start, item.span) >= item.natural, "maximum-domain child is never clipped");
  std::cout << "UI layout: " << checks << " checks passed\n";
}
