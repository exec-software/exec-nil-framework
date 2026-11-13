#pragma once

#include "../common/content_digest.hpp"
#include <map>
#include <set>
#include <string>

namespace nilbuild {
// A single preparation OR verification phase owns this map. Never retain it
// across compilation, analysis refresh, dependency layers, or build commands.
// Only shared compiler/toolchain inputs use it; source dependency checks stay
// independent. Selected path spellings remain identities, including symlinks.
class digest_phase {
  std::map<std::string, std::string> values;
  std::set<std::string> excluded;
  bool excluded_path(const std::filesystem::path &path) const {
    const auto spelling = std::filesystem::absolute(path).lexically_normal().string();
    if (excluded.count(spelling)) return true;
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || excluded.count(canonical.string())) return true;
    // Canonicalization resolves symlinks, not hard links. Most shared inputs
    // have one link, so avoid a per-source identity scan in that common case.
    const auto links = std::filesystem::hard_link_count(path, error);
    if (error) return true;
    if (links > 1) for (const auto &source : excluded) {
      const bool same = std::filesystem::equivalent(path, source, error);
      if (same) return true;
      // Missing/changed sources are handled by the independent source check;
      // don't memoize while their physical identity cannot be compared.
      if (error) return true;
    }
    return false;
  }
public:
  void exclude(const std::filesystem::path &path) {
    excluded.insert(std::filesystem::absolute(path).lexically_normal().string());
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (!error) excluded.insert(canonical.string());
  }
  void inherit_exclusions(const digest_phase &preparation) { excluded = preparation.excluded; }
  template<class Read> std::string read(const std::filesystem::path &path, Read digest) {
    if (excluded_path(path)) return digest(path);
    const auto key = std::filesystem::absolute(path).lexically_normal().string();
    const auto found = values.find(key);
    if (found != values.end()) return found->second;
    return values.emplace(key, digest(path)).first->second;
  }
  bool contains(const std::string &path) const { return !excluded_path(path) && values.count(path) != 0; }
};
} // namespace nilbuild
