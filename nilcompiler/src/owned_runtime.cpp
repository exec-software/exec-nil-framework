#include "owned_runtime.hpp"
#include "native_runtime.hpp"
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {
using binding = std::pair<std::uint64_t, void *>;
thread_local std::vector<std::vector<binding>> owned_scopes;
thread_local std::uint64_t drop_count = 0;
[[noreturn]] void invalid_owned_protocol() noexcept {
  nyrt_fail("NIL native runtime error: invalid affine ownership protocol\n");
}
}

extern "C" {
std::uint64_t nyrt_owned_v1_depth() noexcept { return owned_scopes.size(); }
void nyrt_owned_v1_enter() noexcept {
  try { owned_scopes.emplace_back(); }
  catch (...) { nyrt_fail("NIL native runtime error: could not enter owned scope\n"); }
}
void nyrt_owned_v1_bind(std::uint64_t slot, void *buffer) noexcept {
  if (owned_scopes.empty() || slot == 0 || buffer == nullptr)
    invalid_owned_protocol();
  for (const auto &scope : owned_scopes)
    for (const auto &item : scope)
      if (item.first == slot || item.second == buffer) invalid_owned_protocol();
  try { owned_scopes.back().emplace_back(slot, buffer); }
  catch (...) { nyrt_fail("NIL native runtime error: could not bind owned buffer\n"); }
}
void *nyrt_owned_v1_take(std::uint64_t slot) noexcept {
  for (auto scope = owned_scopes.rbegin(); scope != owned_scopes.rend(); ++scope) {
    const auto item = std::find_if(scope->begin(), scope->end(),
        [slot](const binding &value) { return value.first == slot; });
    if (item != scope->end()) {
      void *buffer = item->second;
      scope->erase(item);
      return buffer;
    }
  }
  invalid_owned_protocol();
}
void nyrt_owned_v1_leave() noexcept {
  if (owned_scopes.empty()) invalid_owned_protocol();
  // Reverse declaration order, including moved-in owners.
  for (auto item = owned_scopes.back().rbegin(); item != owned_scopes.back().rend(); ++item) {
    if (!nyrt_owned_buffer_release(item->second)) invalid_owned_protocol();
    if (drop_count != std::numeric_limits<std::uint64_t>::max()) ++drop_count;
  }
  owned_scopes.pop_back();
}
void nyrt_owned_v1_unwind(std::uint64_t depth) noexcept {
  if (depth > owned_scopes.size()) invalid_owned_protocol();
  while (owned_scopes.size() > depth) nyrt_owned_v1_leave();
}
std::uint64_t nyrt_owned_v1_live() noexcept {
  std::uint64_t live = 0;
  for (const auto &scope : owned_scopes) live += scope.size();
  return live;
}
std::uint64_t nyrt_owned_v1_drops() noexcept { return drop_count; }
}
