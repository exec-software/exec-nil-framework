#include "database_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <poll.h>
#include <cerrno>
#include <arpa/inet.h>
#endif
#ifdef NIL_DB_SQLITE
#include <sqlite3.h>
#endif
#ifdef NIL_DB_POSTGRESQL
#include <libpq-fe.h>
#endif

namespace ny::database {
namespace {
constexpr std::size_t max_bytes = 16 * 1024 * 1024;
constexpr std::size_t max_sql = 1024 * 1024;
constexpr std::size_t max_rows = 10000, max_columns = 256, max_parameters = 256;
using Clock = std::chrono::steady_clock;
Result fail(const char *message) { Result r; r.error = message; return r; }
Result number(std::int64_t value) { Result r; r.number = value; return r; }
bool nul(const std::string &value) { return value.find('\0') != std::string::npos; }
bool timeout_valid(std::int64_t value) { return value > 0 && value <= 30000; }
// Reject a second token stream before preparing anything: SQLite executes some
// PRAGMAs while preparing. Compound trigger bodies are intentionally outside
// this first statement API; quotes and SQL comments can contain semicolons.
bool sqlite_single_statement(const std::string &sql) {
 bool ended = false;
 for (std::size_t i = 0; i < sql.size();) {
   unsigned char c = static_cast<unsigned char>(sql[i]);
   if (c == ' ' || (c >= 9 && c <= 13)) { ++i; continue; }
   if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
     i = sql.find('\n', i + 2); if (i == std::string::npos) return true; continue;
   }
   if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
     auto end = sql.find("*/", i + 2); if (end == std::string::npos) return true;
     i = end + 2; continue;
   }
   if (ended) return false;
   if (c == ';') { ended = true; ++i; continue; }
   if (c == '\'' || c == '"' || c == '`' || c == '[') {
     char quote = c == '[' ? ']' : static_cast<char>(c); ++i;
     while (i < sql.size()) {
       if (sql[i++] != quote) continue;
       if (quote != ']' && i < sql.size() && sql[i] == quote) { ++i; continue; }
       break;
     }
   } else ++i;
 }
 return true;
}

// Process-wide monotonically increasing tokens cannot alias another context's
// handles or a released handle, including after VM resets/native context end.
std::atomic<std::int64_t> next_handle{1};
std::int64_t token() {
  auto current = next_handle.load();
  while (current < std::numeric_limits<std::int64_t>::max()) {
    if (next_handle.compare_exchange_weak(current, current + 1)) return current;
  }
  return 0;
}
void *library([[maybe_unused]] const char *unix_name, [[maybe_unused]] const char *mac_name, [[maybe_unused]] const char *win_name) {
#ifdef _WIN32
  return reinterpret_cast<void *>(LoadLibraryA(win_name));
#elif defined(__APPLE__)
  return dlopen(mac_name, RTLD_NOW | RTLD_LOCAL);
#else
  return dlopen(unix_name, RTLD_NOW | RTLD_LOCAL);
#endif
}
template<class T> bool symbol(void *handle, T &out, const char *name) {
#ifdef _WIN32
  out = reinterpret_cast<T>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
  out = reinterpret_cast<T>(dlsym(handle, name));
#endif
  return out != nullptr;
}
// Loaded modules remain resident: contexts can be thread_local and outlive
// ordinary static destruction. No provider object survives its owning context.
#ifdef NIL_DB_SQLITE
struct SQLite {
#define DB_SQLITE_FUNCTIONS(X) \
 X(sqlite3_open_v2) X(sqlite3_close_v2) X(sqlite3_prepare_v2) X(sqlite3_finalize) \
 X(sqlite3_bind_parameter_count) X(sqlite3_bind_text) X(sqlite3_bind_null) \
 X(sqlite3_step) X(sqlite3_errstr) X(sqlite3_column_count) X(sqlite3_column_name) \
 X(sqlite3_column_type) X(sqlite3_column_text) X(sqlite3_column_bytes) \
 X(sqlite3_changes64) X(sqlite3_total_changes64) X(sqlite3_stmt_readonly) X(sqlite3_busy_timeout) \
 X(sqlite3_progress_handler) X(sqlite3_busy_handler) X(sqlite3_limit)
#define FIELD(name) decltype(&::name) name = nullptr;
 DB_SQLITE_FUNCTIONS(FIELD)
#undef FIELD
 int (*error_offset)(sqlite3 *) = nullptr;
 bool loaded = false;
 SQLite() {
   auto h = library("libsqlite3.so.0", "libsqlite3.dylib", "sqlite3.dll");
   if (!h) return;
#define LOAD(name) if (!symbol(h, name, #name)) return;
   DB_SQLITE_FUNCTIONS(LOAD)
#undef LOAD
   symbol(h, error_offset, "sqlite3_error_offset");
   loaded = true;
 }
};
SQLite &sqlite() { static SQLite value; return value; }
Result sqlite_error(sqlite3 *db, int status, const char *phase) {
 Result result;
 result.error = std::string("database: SQLite ") + phase + " failed (" + sqlite().sqlite3_errstr(status) + ")";
 if (db && sqlite().error_offset) {
   int offset = sqlite().error_offset(db);
   if (offset >= 0) result.error += " at SQL byte " + std::to_string(offset);
 }
 return result;
}
#endif
#if defined(NIL_DB_POSTGRESQL) && !defined(_WIN32)
struct PostgreSQL {
#define DB_PG_FUNCTIONS(X) \
 X(PQconnectStartParams) X(PQconninfoParse) X(PQconninfoFree) X(PQfreemem) X(PQconnectPoll) X(PQsetnonblocking) X(PQfinish) X(PQsocket) \
 X(PQstatus) X(PQsendQueryParams) X(PQsetSingleRowMode) X(PQflush) \
 X(PQconsumeInput) X(PQisBusy) X(PQgetResult) X(PQclear) X(PQresultStatus) \
 X(PQntuples) X(PQftype) X(PQnfields) X(PQfname) X(PQgetisnull) X(PQgetvalue) \
 X(PQgetlength) X(PQcmdTuples) X(PQresultErrorField)
#define FIELD(name) decltype(&::name) name = nullptr;
 DB_PG_FUNCTIONS(FIELD)
#undef FIELD
 bool loaded = false;
 PostgreSQL() {
   auto h = library("libpq.so.5", "libpq.dylib", "libpq.dll");
   if (!h) return;
#define LOAD(name) if (!symbol(h, name, #name)) return;
   DB_PG_FUNCTIONS(LOAD)
#undef LOAD
   loaded = true;
 }
};
PostgreSQL &postgres() { static PostgreSQL value; return value; }
bool wait_socket(PGconn *connection, short events, Clock::time_point deadline) {
 auto &p = postgres();
 for (;;) {
   auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
   if (remaining <= 0 || p.PQsocket(connection) < 0) return false;
   pollfd fd{p.PQsocket(connection), events, 0};
   int status = poll(&fd, 1, static_cast<int>(remaining));
   if (status < 0 && errno == EINTR) continue;
   return status > 0 && !(fd.revents & (POLLERR | POLLNVAL)) && (fd.revents & (events | POLLHUP));
 }
}
#endif
struct Cell { bool is_null = false; std::string text; };
struct Rows {
 std::int64_t connection = 0;
 std::vector<std::string> names;
 std::vector<std::vector<Cell>> rows;
 std::int64_t affected = 0;
 std::size_t bytes = 0;
 bool account(std::size_t size) {
   if (size > max_bytes - bytes) return false;
   bytes += size; return true;
 }
};
struct Connection {
 enum class Kind { sqlite, postgres } kind;
 void *value;
 ~Connection() {
#ifdef NIL_DB_SQLITE
   if (kind == Kind::sqlite) sqlite().sqlite3_close_v2(static_cast<sqlite3 *>(value));
#endif
#if defined(NIL_DB_POSTGRESQL) && !defined(_WIN32)
   if (kind == Kind::postgres) postgres().PQfinish(static_cast<PGconn *>(value));
#endif
 }
};
}
struct Context::Impl {
 std::unordered_map<std::int64_t, std::unique_ptr<Connection>> connections;
 std::unordered_map<std::int64_t, Rows> results;
 void close(std::int64_t handle) {
   for (auto it = results.begin(); it != results.end();) {
     if (it->second.connection == handle) it = results.erase(it); else ++it;
   }
   connections.erase(handle);
 }
};
Context::Context() : impl_(std::make_unique<Impl>()) {}
Context::~Context() = default;
void Context::clear() noexcept { impl_->results.clear(); impl_->connections.clear(); }
bool available(const std::string &driver) {
#ifdef NIL_DB_SQLITE
 if (driver == "sqlite") return sqlite().loaded;
#endif
#if defined(NIL_DB_POSTGRESQL) && !defined(_WIN32)
 if (driver == "postgresql") return postgres().loaded;
#endif
 return false;
}
Result Context::open(const std::string &driver, const std::string &address, std::int64_t timeout_ms) {
 if (!timeout_valid(timeout_ms)) return fail("database: timeout must be 1..30000 milliseconds");
 if (address.empty() || address.size() > 16384 || nul(address)) return fail("database: invalid connection address");
 if (!available(driver)) return fail("database: requested provider is unavailable");
 if (impl_->connections.size() >= 32) return fail("database: connection limit exceeded");
 auto handle = token();
 if (!handle) return fail("database: handle space exhausted");
#ifdef NIL_DB_SQLITE
 if (driver == "sqlite") {
   sqlite3 *db = nullptr;
   auto &s = sqlite();
   int status = s.sqlite3_open_v2(address.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
   if (status != SQLITE_OK) { auto error = sqlite_error(db, status, "open"); if (db) s.sqlite3_close_v2(db); return error; }
   s.sqlite3_busy_timeout(db, static_cast<int>(timeout_ms));
   s.sqlite3_limit(db, SQLITE_LIMIT_LENGTH, static_cast<int>(max_bytes));
   s.sqlite3_limit(db, SQLITE_LIMIT_SQL_LENGTH, static_cast<int>(max_sql));
   s.sqlite3_limit(db, SQLITE_LIMIT_COLUMN, static_cast<int>(max_columns));
   s.sqlite3_limit(db, SQLITE_LIMIT_VARIABLE_NUMBER, static_cast<int>(max_parameters));
   impl_->connections.emplace(handle, std::unique_ptr<Connection>(new Connection{Connection::Kind::sqlite, db}));
   return number(handle);
 }
#endif
#if defined(NIL_DB_POSTGRESQL) && !defined(_WIN32)
 if (driver == "postgresql") {
   auto &p = postgres();
   auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
   // Require a concrete address to avoid an unbounded synchronous DNS lookup
   // hidden inside libpq's otherwise nonblocking connection state machine.
   char *parse_error = nullptr;
   auto *options = p.PQconninfoParse(address.c_str(), &parse_error);
   if (parse_error) p.PQfreemem(parse_error);
   if (!options) return fail("database: invalid PostgreSQL connection options");
   struct Options { PostgreSQL &p; PQconninfoOption *value; ~Options() { p.PQconninfoFree(value); } } option_guard{p, options};
   std::string host, hostaddr;
   std::vector<const char *> keys, values;
   for (auto *option = options; option->keyword; ++option) {
     if (!option->val || !*option->val) continue;
     std::string key(option->keyword);
     if (key == "service") return fail("database: PostgreSQL service files are outside the bounded connection API");
     if (key == "host") host = option->val;
     if (key == "hostaddr") hostaddr = option->val;
     if (key == "connect_timeout" || key == "client_encoding") continue;
     keys.push_back(option->keyword); values.push_back(option->val);
   }
   unsigned char numeric[16];
   bool numeric_host = !hostaddr.empty() && (inet_pton(AF_INET, hostaddr.c_str(), numeric) == 1 || inet_pton(AF_INET6, hostaddr.c_str(), numeric) == 1);
   bool unix_host = !host.empty() && host[0] == '/' && host.find(',') == std::string::npos && hostaddr.empty();
   if (!numeric_host && !unix_host) return fail("database: PostgreSQL requires numeric hostaddr or an absolute Unix socket host");
   if (host.find(',') != std::string::npos) return fail("database: PostgreSQL requires one host");
   std::string timeout_seconds = std::to_string((timeout_ms + 999) / 1000);
   keys.push_back("connect_timeout"); values.push_back(timeout_seconds.c_str());
   keys.push_back("client_encoding"); values.push_back("UTF8");
   keys.push_back(nullptr); values.push_back(nullptr);
   PGconn *db = p.PQconnectStartParams(keys.data(), values.data(), 0);
   if (!db) return fail("database: PostgreSQL connection failed");
   auto connection = std::unique_ptr<Connection>(new Connection{Connection::Kind::postgres, db});
   for (;;) {
     auto status = p.PQconnectPoll(db);
     if (status == PGRES_POLLING_OK) break;
     if (status == PGRES_POLLING_FAILED) return fail("database: PostgreSQL connection failed");
     if (!wait_socket(db, status == PGRES_POLLING_WRITING ? POLLOUT : POLLIN, deadline))
       return fail("database: PostgreSQL connection timed out or transport failed");
   }
   if (p.PQsetnonblocking(db, 1) != 0) return fail("database: PostgreSQL nonblocking mode failed");
   impl_->connections.emplace(handle, std::move(connection));
   return number(handle);
 }
#endif
 return fail("database: requested provider is unavailable");
}
Result Context::execute(std::int64_t connection, const std::string &sql,
 const std::vector<std::string> &parameters, const std::vector<bool> &nulls, std::int64_t timeout_ms) {
 auto found = impl_->connections.find(connection);
 if (found == impl_->connections.end()) return fail("database: invalid or closed connection handle");
 if (!timeout_valid(timeout_ms)) return fail("database: timeout must be 1..30000 milliseconds");
 if (sql.empty() || sql.size() > max_sql || nul(sql)) return fail("database: invalid SQL text");
 if (parameters.size() != nulls.size() || parameters.size() > max_parameters) return fail("database: parameter and NULL arrays must have equal lengths up to 256");
 std::size_t parameter_bytes = 0;
 for (const auto &value : parameters) {
   if (nul(value) || value.size() > max_bytes - parameter_bytes) return fail("database: parameter text exceeds limits or contains NUL");
   parameter_bytes += value.size();
 }
 if (impl_->results.size() >= 128) return fail("database: result limit exceeded");
 Rows result; result.connection = connection;
 auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
#ifdef NIL_DB_SQLITE
 if (found->second->kind == Connection::Kind::sqlite) {
   if (!sqlite_single_statement(sql)) return fail("database: expected one SQL statement");
   auto &s = sqlite(); auto *db = static_cast<sqlite3 *>(found->second->value);
   s.sqlite3_busy_handler(db, [](void *data, int) -> int {
     auto deadline = *static_cast<Clock::time_point *>(data);
     if (Clock::now() >= deadline) return 0;
     std::this_thread::sleep_for(std::chrono::milliseconds(1));
     return Clock::now() < deadline;
   }, &deadline);
   s.sqlite3_progress_handler(db, 1000, [](void *data) -> int { return Clock::now() >= *static_cast<Clock::time_point *>(data); }, &deadline);
   struct Progress { SQLite &s; sqlite3 *db; ~Progress() { s.sqlite3_progress_handler(db, 0, nullptr, nullptr); s.sqlite3_busy_handler(db, nullptr, nullptr); } } progress{s, db};
   sqlite3_stmt *raw = nullptr; const char *tail = nullptr;
   int prepared = s.sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &raw, &tail);
   if (prepared != SQLITE_OK) return sqlite_error(db, prepared, "preparation");
   struct Statement { SQLite &s; sqlite3_stmt *value; ~Statement() { if (value) s.sqlite3_finalize(value); } } statement{s, raw};
   if (!raw) return fail("database: expected one SQL statement");
   if (s.sqlite3_bind_parameter_count(raw) != static_cast<int>(parameters.size())) return fail("database: SQL parameter count does not match bindings");
   for (std::size_t i = 0; i < parameters.size(); ++i) {
     int status = nulls[i] ? s.sqlite3_bind_null(raw, static_cast<int>(i + 1)) : s.sqlite3_bind_text(raw, static_cast<int>(i + 1), parameters[i].data(), static_cast<int>(parameters[i].size()), SQLITE_TRANSIENT);
     if (status != SQLITE_OK) return sqlite_error(db, status, "parameter binding");
   }
   int columns = s.sqlite3_column_count(raw);
   for (int col = 0; col < columns; ++col) {
     const char *name = s.sqlite3_column_name(raw, col);
     if (!name || !result.account(std::strlen(name) + sizeof(std::string))) return fail("database: result size limit exceeded");
     result.names.emplace_back(name);
   }
   auto initial_changes = s.sqlite3_total_changes64(db);
   for (;;) {
     if (Clock::now() >= deadline) return fail("database: execution timed out");
     int status = s.sqlite3_step(raw);
     if (status == SQLITE_DONE) break;
     if (status != SQLITE_ROW) return status == SQLITE_INTERRUPT || status == SQLITE_BUSY ? fail("database: execution timed out or database is busy") : sqlite_error(db, status, "execution");
     if (result.rows.size() >= max_rows) return fail("database: row limit exceeded");
     if (!result.account(sizeof(std::vector<Cell>) + sizeof(Cell) * static_cast<std::size_t>(columns))) return fail("database: result size limit exceeded");
     std::vector<Cell> row;
     for (int col = 0; col < columns; ++col) {
       Cell cell; cell.is_null = s.sqlite3_column_type(raw, col) == SQLITE_NULL;
       if (!cell.is_null) {
         if (s.sqlite3_column_type(raw, col) == SQLITE_BLOB) return fail("database: binary cells are not supported by the text API");
         const auto *data = s.sqlite3_column_text(raw, col);
         int size = s.sqlite3_column_bytes(raw, col);
         if (!data || !result.account(static_cast<std::size_t>(size))) return fail("database: result size limit exceeded");
         cell.text.assign(reinterpret_cast<const char *>(data), static_cast<std::size_t>(size));
         if (nul(cell.text)) return fail("database: cell text contains NUL");
       }
       row.push_back(std::move(cell));
     }
     result.rows.push_back(std::move(row));
   }
   result.affected = s.sqlite3_stmt_readonly(raw) || s.sqlite3_total_changes64(db) == initial_changes ? 0 : s.sqlite3_changes64(db);
 }
#endif
#if defined(NIL_DB_POSTGRESQL) && !defined(_WIN32)
 if (found->second->kind == Connection::Kind::postgres) {
   auto &p = postgres(); auto *db = static_cast<PGconn *>(found->second->value);
   auto broken = [&](const char *message) { impl_->close(connection); return fail(message); };
   std::vector<const char *> values;
   for (std::size_t i = 0; i < parameters.size(); ++i) values.push_back(nulls[i] ? nullptr : parameters[i].c_str());
   if (!p.PQsendQueryParams(db, sql.c_str(), static_cast<int>(values.size()), nullptr, values.data(), nullptr, nullptr, 0) || !p.PQsetSingleRowMode(db))
     return broken("database: PostgreSQL submission failed; connection closed");
   for (;;) {
     int status = p.PQflush(db);
     if (status == 0) break;
     if (status < 0 || !wait_socket(db, POLLOUT, deadline)) return broken("database: PostgreSQL transport failed or timed out; connection closed");
   }
   std::string execution_error;
   for (;;) {
     while (p.PQisBusy(db)) {
       if (!wait_socket(db, POLLIN, deadline) || !p.PQconsumeInput(db)) return broken("database: PostgreSQL transport failed or timed out; connection closed");
     }
     if (Clock::now() >= deadline) return broken("database: execution timed out; connection closed");
     PGresult *raw = p.PQgetResult(db);
     if (!raw) break;
     struct PgResult { PostgreSQL &p; PGresult *value; ~PgResult() { p.PQclear(value); } } guard{p, raw};
     auto status = p.PQresultStatus(raw);
     if (status != PGRES_SINGLE_TUPLE && status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
       if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) return broken("database: COPY is outside the text API; connection closed");
       execution_error = "database: PostgreSQL execution failed";
       const char *state = p.PQresultErrorField(raw, PG_DIAG_SQLSTATE);
       if (state && std::strlen(state) == 5) execution_error += std::string(" (SQLSTATE ") + state + ")";
       continue;
     }
     int columns = p.PQnfields(raw);
     if (columns < 0 || columns > static_cast<int>(max_columns)) return broken("database: column limit exceeded; connection closed");
     if (result.names.empty()) {
       for (int col = 0; col < columns; ++col) {
         const char *name = p.PQfname(raw, col);
         if (!name || !result.account(std::strlen(name) + sizeof(std::string))) return broken("database: result size limit exceeded; connection closed");
         result.names.emplace_back(name);
       }
     }
     for (int r = 0; r < p.PQntuples(raw); ++r) {
       if (result.rows.size() >= max_rows) return broken("database: row limit exceeded; connection closed");
       if (!result.account(sizeof(std::vector<Cell>) + sizeof(Cell) * static_cast<std::size_t>(columns))) return broken("database: result size limit exceeded; connection closed");
       std::vector<Cell> row;
       for (int col = 0; col < columns; ++col) {
         if (p.PQftype(raw, col) == 17) return broken("database: binary cells are not supported by the text API; connection closed");
         Cell cell; cell.is_null = p.PQgetisnull(raw, r, col) != 0;
         if (!cell.is_null) {
           int size = p.PQgetlength(raw, r, col);
           if (size < 0 || !result.account(static_cast<std::size_t>(size))) return broken("database: result size limit exceeded; connection closed");
           cell.text.assign(p.PQgetvalue(raw, r, col), static_cast<std::size_t>(size));
           if (nul(cell.text)) return broken("database: cell text contains NUL; connection closed");
         }
         row.push_back(std::move(cell));
       }
       result.rows.push_back(std::move(row));
     }
     if (status == PGRES_COMMAND_OK) {
       const char *count = p.PQcmdTuples(raw);
       if (count && *count) {
         auto parsed = std::from_chars(count, count + std::strlen(count), result.affected);
         if (parsed.ec != std::errc()) return broken("database: affected row count overflow; connection closed");
       }
     }
   }
   if (!execution_error.empty()) { Result error; error.error = execution_error; return error; }
 }
#endif
 auto handle = token();
 if (!handle) return fail("database: handle space exhausted");
 impl_->results.emplace(handle, std::move(result));
 return number(handle);
}
Result Context::read(Read operation, std::int64_t handle, std::int64_t row, std::int64_t column) {
 if (operation == Read::close) {
   if (!impl_->connections.count(handle)) return fail("database: invalid or closed connection handle");
   impl_->close(handle); return number(1);
 }
 auto found = impl_->results.find(handle);
 if (found == impl_->results.end()) return fail("database: invalid or released result handle");
 auto &result = found->second;
 if (operation == Read::release) { impl_->results.erase(found); return number(1); }
 if (operation == Read::rows) return number(static_cast<std::int64_t>(result.rows.size()));
 if (operation == Read::columns) return number(static_cast<std::int64_t>(result.names.size()));
 if (operation == Read::affected) return number(result.affected);
 if (column < 0 || static_cast<std::size_t>(column) >= result.names.size()) return fail("database: column index out of range");
 if (operation == Read::column_name) { Result value; value.text = result.names[column]; return value; }
 if (row < 0 || static_cast<std::size_t>(row) >= result.rows.size()) return fail("database: row index out of range");
 const auto &cell = result.rows[row][column];
 if (operation == Read::is_null) return number(cell.is_null ? 1 : 0);
 if (operation != Read::cell) return fail("database: unknown result operation");
 if (cell.is_null) return fail("database: cell is NULL");
 Result value; value.text = cell.text; return value;
}
}
