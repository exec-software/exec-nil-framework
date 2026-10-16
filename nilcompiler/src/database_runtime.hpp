#ifndef NIL_DATABASE_RUNTIME_HPP
#define NIL_DATABASE_RUNTIME_HPP
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace ny::database {
// Text-only, context-owned database boundary. Provider messages deliberately do
// not echo SQL, connection strings, parameter values, or filesystem paths.
struct Result {
  std::int64_t number = 0;
  std::string text;
  std::string error;
  explicit operator bool() const { return error.empty(); }
};
enum class Read : std::int64_t {
  rows = 1, columns = 2, affected = 3, is_null = 4,
  release = 5, close = 6, column_name = 7, cell = 8
};
bool available(const std::string &driver);
class Context {
 public:
  Context();
  ~Context();
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;
  void clear() noexcept;
  Result open(const std::string &driver, const std::string &address,
              std::int64_t timeout_ms);
  Result execute(std::int64_t connection, const std::string &sql,
                 const std::vector<std::string> &parameters,
                 const std::vector<bool> &nulls, std::int64_t timeout_ms);
  Result read(Read operation, std::int64_t handle, std::int64_t row = 0,
              std::int64_t column = 0);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
#endif
