@schema
@pragma once
module nil.std.db

import nil.std.core

# Shared VM/JIT/native provider boundary. Driver names: sqlite, postgresql.
# Capability means the provider library is loadable, not that a server exists.
@intrinsic("db2.available")
pub fn db_available(driver: text) -> bool

# SQLite address is a filename or :memory:. PostgreSQL uses libpq conninfo
# with numeric hostaddr or an absolute Unix socket host (one host, no service
# files). DNS lookup is excluded; provider support is currently POSIX only.
# Timeout must be 1..30000 ms. Handles belong to the current execution context.
@intrinsic("db2.open")
pub fn db_open(driver: text, address: text, timeout_ms: i64) -> Result<i64, text>

# One statement, <=256 bound text parameters. true NULL flags bind SQL NULL.
# Results retain <=10000 rows, <=256 columns and <=16 MiB of text. Binary/NUL
# cells are rejected. No SQL/credentials/parameter values are echoed in errors.
# PostgreSQL uses $1 placeholders; SQLite accepts $1 or ?1. A timeout/transport
# failure closes PostgreSQL's connection. SQLite keeps its connection usable.
# Errors do not imply rollback: use explicit BEGIN/COMMIT/ROLLBACK statements.
# SQLite compound trigger bodies are outside this single-statement slice.
@intrinsic("db2.execute")
pub fn db_execute(connection: i64, sql: text, parameters: array<text>, nulls: array<bool>, timeout_ms: i64) -> Result<i64, text>

@intrinsic("db2.rows")
pub fn db_rows(result: i64) -> Result<i64, text>

@intrinsic("db2.columns")
pub fn db_columns(result: i64) -> Result<i64, text>

@intrinsic("db2.affected")
pub fn db_affected(result: i64) -> Result<i64, text>

# Returns 1 for SQL NULL and 0 for a non-NULL cell. Indices are zero based.
@intrinsic("db2.is_null")
pub fn db_is_null(result: i64, row: i64, column: i64) -> Result<i64, text>

@intrinsic("db2.column_name")
pub fn db_column_name(result: i64, column: i64) -> Result<text, text>

# NULL is an error here: use db_is_null to distinguish NULL from empty text.
@intrinsic("db2.cell")
pub fn db_cell(result: i64, row: i64, column: i64) -> Result<text, text>

# Release/close return 1 once, then Error for stale handles. Closing also
# invalidates that connection's results. Context teardown closes everything.
@intrinsic("db2.release")
pub fn db_release(result: i64) -> Result<i64, text>

@intrinsic("db2.close")
pub fn db_close(connection: i64) -> Result<i64, text>
