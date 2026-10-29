#ifndef NY_COMPILER_NIL_LOADER_HPP
#define NY_COMPILER_NIL_LOADER_HPP

#include "codegen.hpp"
#include "vm.hpp"
#include <istream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ny {

struct nil_library_source {
  std::string path;
  std::string contents;
};

struct nil_library_metadata {
  std::string module_name;
  std::vector<std::string> dependencies;
  std::vector<nil_library_source> sources;
};

// Parse textual NIL format
std::vector<Instruction>
parseProgram(std::istream &is,
             std::unordered_map<std::string, Value> &constants);

// Parse binary NIL format
std::vector<Instruction>
parseBinaryProgram(std::istream &is,
                   std::unordered_map<std::string, Value> &constants);

// Read the self-contained semantic interface carried by a NIL v2/v4 library.
// Throws std::runtime_error for executable NIL, malformed metadata, unsafe
// embedded paths, or unsupported versions.
// When requested, content_digest receives SHA-256 of the exact parsed bytes.
nil_library_metadata parseBinaryLibraryMetadata(std::istream &is,
                                               std::string *content_digest = nullptr);

// Parse a compiler-emitted textual or binary executable NIL artifact without
// flattening its function boundaries and signatures into VM instructions.
// The binary reader deliberately rejects NIL v2 libraries: their embedded
// semantic source interface belongs to library resolution, not native object
// assembly.
std::unique_ptr<nil_module> parseTextNilModule(std::istream &is);
std::unique_ptr<nil_module> parseBinaryNilModule(std::istream &is);

} // namespace ny

#endif // NY_COMPILER_NIL_LOADER_HPP
