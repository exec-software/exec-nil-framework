#include "database_runtime.hpp"
#include "native_runtime.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <string>
using ny::database::Context;
using ny::database::Read;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
 try {
  if (!ny::database::available("sqlite")) return 77;
  Context first, second;
  auto opened = first.open("sqlite", ":memory:", 1000);
  require(bool(opened), "SQLite open");
  auto handle = opened.number;
  require(!second.read(Read::close, handle), "cross-context token rejected");
  auto bad = first.execute(handle, "SELECT $1", {std::string("a\0b", 3)}, {false}, 1000);
  require(!bad, "NUL binding rejected");
  auto text = first.execute(handle, "SELECT $1 AS word", {"München\n世界"}, {false}, 1000);
  require(bool(text), "Unicode/control binding");
  require(first.read(Read::cell, text.number, 0, 0).text == "München\n世界", "text preserved bytewise");
  first.clear();
  require(!first.read(Read::rows, text.number), "reset drops result");
  require(!first.read(Read::close, handle), "reset closes connection");
  auto fresh = first.open("sqlite", ":memory:", 1000);
  require(bool(fresh) && fresh.number != handle, "reset cannot revive old token");
  require(!first.execute(fresh.number, "SELECT 1", {}, {}, 30001), "timeout ceiling");
  std::string overlong(1024 * 1024 + 1, ' ');
  require(!first.execute(fresh.number, overlong, {}, {}, 1000), "SQL byte ceiling");
  // Limits count metadata too, so millions of empty cells cannot bypass the
  // retained-result budget. Providers remain usable after rejected SQLite SQL.
  require(!first.execute(fresh.number, "SELECT $1, $1", {std::string(8 * 1024 * 1024, 'x')}, {false}, 1000), "retained byte ceiling");
  require(!first.execute(fresh.number, "SELECT $1", std::vector<std::string>(257, "x"), std::vector<bool>(257, false), 1000), "parameter ceiling");
  std::vector<std::int64_t> results;
  for (int i = 0; i < 128; ++i) {
    auto item = first.execute(fresh.number, "SELECT ''", {}, {}, 1000);
    require(bool(item), "result handle allocation"); results.push_back(item.number);
  }
  require(!first.execute(fresh.number, "SELECT 1", {}, {}, 1000), "result handle ceiling");
  for (auto item : results) require(bool(first.read(Read::release, item)), "release bounded result");
  auto directory = std::filesystem::temp_directory_path() / ("nil-database-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  require(std::filesystem::create_directory(directory), "private SQLite fixture directory");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{directory};
  auto path = (directory / "persistent.sqlite").string();
  auto disk = first.open("sqlite", path, 1000);
  require(bool(disk), "SQLite disk open");
  require(bool(first.execute(disk.number, "CREATE TABLE records(value TEXT)", {}, {}, 1000)), "SQLite disk schema");
  require(bool(first.execute(disk.number, "INSERT INTO records VALUES ($1)", {"persisted"}, {false}, 1000)), "SQLite disk write");
  require(bool(first.read(Read::close, disk.number)), "SQLite disk close");
  auto reopened = first.open("sqlite", path, 1000);
  auto persisted = first.execute(reopened.number, "SELECT value FROM records", {}, {}, 1000);
  require(bool(persisted) && first.read(Read::cell, persisted.number).text == "persisted", "SQLite persistence");
  auto concurrent = second.open("sqlite", path, 1000);
  require(bool(concurrent), "SQLite second connection");
  require(bool(first.execute(reopened.number, "BEGIN EXCLUSIVE", {}, {}, 1000)), "SQLite lock");
  auto lock_start = std::chrono::steady_clock::now();
  require(!second.execute(concurrent.number, "INSERT INTO records VALUES ('blocked')", {}, {}, 10), "SQLite busy timeout");
  require(std::chrono::steady_clock::now() - lock_start < std::chrono::seconds(1), "SQLite busy wait bounded");
  require(bool(first.execute(reopened.number, "ROLLBACK", {}, {}, 1000)), "SQLite rollback");
  require(bool(second.execute(concurrent.number, "INSERT INTO records VALUES ('unblocked')", {}, {}, 1000)), "SQLite reuse after busy");
  require(bool(first.read(Read::close, reopened.number)), "SQLite cleanup first");
  require(bool(second.read(Read::close, concurrent.number)), "SQLite cleanup second");
  std::int64_t native_handle = 0; const char *error = nullptr;
  nyrt_context_begin(0, nullptr);
  require(nyrt_db2_open("sqlite", ":memory:", 1000, &native_handle, &error) == 1, "native open");
  nyrt_context_end(); nyrt_context_begin(0, nullptr);
  std::int64_t value = 0;
  require(nyrt_db2_read_i64(6, native_handle, 0, 0, &value, &error) == 0, "native reset invalidates token");
  auto *wrong_array = nyrt_array_new(NYRT_ARRAY_I64, 0);
  auto *flags = nyrt_array_new(NYRT_ARRAY_BOOL, 0);
  require(nyrt_db2_execute(native_handle, "SELECT 1", wrong_array, flags, 1000, &value, &error) == 0, "native typed array checked");
  nyrt_context_end();
  if (argc == 2) {
   require(ny::database::available("postgresql"), "PostgreSQL capability");
   require(!first.open("postgresql", "host=example.invalid dbname=postgres", 1000), "PG rejects DNS before connecting");
   require(!first.open("postgresql", "service=untrusted", 1000), "PG rejects service lookup");
   auto pg = first.open("postgresql", argv[1], 3000);
   if (!pg) throw std::runtime_error(pg.error);
   auto rows = first.execute(pg.number, "SELECT $1::text AS value, $2::text AS missing, '' AS blank", {"quoted ' ; -- 世界", "ignored"}, {false, true}, 3000);
   if (!rows) throw std::runtime_error(rows.error);
   require(first.read(Read::rows, rows.number).number == 1, "PG row count");
   require(first.read(Read::columns, rows.number).number == 3, "PG column count");
   require(first.read(Read::column_name, rows.number, 0, 0).text == "value", "PG column name");
   require(first.read(Read::cell, rows.number, 0, 0).text == "quoted ' ; -- 世界", "PG binds parameters");
   require(first.read(Read::is_null, rows.number, 0, 1).number == 1, "PG NULL distinct");
   require(!first.read(Read::cell, rows.number, 0, 1), "PG NULL read rejects");
   require(first.read(Read::cell, rows.number, 0, 2).text.empty(), "PG empty text");
   require(!first.execute(pg.number, "SELECT 1; SELECT 2", {}, {}, 3000), "PG single statement");
   require(bool(first.execute(pg.number, "SELECT 1", {}, {}, 3000)), "PG reusable after SQL error");
   require(!first.execute(pg.number, "SELECT generate_series(1,10001)", {}, {}, 3000), "PG row bound");
   require(!first.read(Read::close, pg.number), "PG bound closes connection");
   auto timed = first.open("postgresql", argv[1], 3000);
   require(bool(timed), "PG reopen");
   auto start = std::chrono::steady_clock::now();
   require(!first.execute(timed.number, "SELECT pg_sleep(2)", {}, {}, 20), "PG timeout");
   require(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "PG timeout bounded");
   require(!first.read(Read::close, timed.number), "PG timeout closes connection");
  }
  std::cout << "database provider lifecycle checks passed\n";
  return 0;
 } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
