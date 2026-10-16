# Checked database providers

`import nil.std.db` exposes the SQL builder and a text-oriented SQLite/PostgreSQL
API. `ny.lang.db` remains an import alias. The VM, bundled executables, LLVM JIT,
and native executables call the same provider implementation.

```ny
import nil.std.db

fn main() -> i32
    let db: i64 = db_open("sqlite", ":memory:", 1000).unwrap()
    let rows: i64 = db_execute(db, "SELECT $1 AS greeting", ["hello"], [false], 1000).unwrap()
    println(db_cell(rows, 0, 0).unwrap())
    db_release(rows).unwrap()
    db_close(db).unwrap()
    return 0
end fn
```

## API

| Function | Result |
| --- | --- |
| `db_available(driver: text)` | `bool`: client provider can be loaded |
| `db_open(driver: text, address: text, timeout_ms: i64)` | `Result<i64,text>`: connection handle |
| `db_execute(connection: i64, sql: text, parameters: array<text>, nulls: array<bool>, timeout_ms: i64)` | `Result<i64,text>`: result handle |
| `db_rows(result: i64)`, `db_columns(result: i64)` | `Result<i64,text>`: materialized dimensions |
| `db_affected(result: i64)` | `Result<i64,text>`: affected-row count; SELECT reports zero |
| `db_column_name(result: i64, column: i64)` | `Result<text,text>` |
| `db_is_null(result: i64, row: i64, column: i64)` | `Result<i64,text>`: 1 for SQL NULL, 0 otherwise |
| `db_cell(result: i64, row: i64, column: i64)` | `Result<text,text>`: NULL is an error |
| `db_release(result: i64)`, `db_close(connection: i64)` | `Result<i64,text>`: 1 on success |

Indices start at zero and reject negative or out-of-range values. Handles belong
to one execution context. Released, closed, foreign-context, and stale handles
return errors. Closing a connection invalidates its results. VM reset and native
context teardown close all connections and release all results. Tokens are not
reused after reset.

Parameter arrays must have equal lengths. A true NULL flag binds SQL NULL; false
binds the corresponding text, including empty text. Values are never interpolated
into SQL. Use `$1`, `$2`, … for PostgreSQL; SQLite also accepts `?1`, `?2`, ….
SQL identifiers remain trusted SQL syntax, not parameter values. Use explicit
SQL casts where PostgreSQL cannot infer a parameter's type.

## Providers and deployment

`sqlite` accepts a filesystem path or `:memory:`. It opens read/write, creating a
missing file. The SQLite URI option is not enabled. `postgresql` accepts libpq
connection options with one numeric `hostaddr` or one absolute Unix socket
`host`. DNS host lookup, multiple hosts, and `service` options are outside this
first connection contract. TLS options can accompany a numeric `hostaddr` and a
`host` used for certificate verification. Client encoding is UTF-8.

The build uses optional SQLite and PostgreSQL headers. At runtime, providers
load the system SQLite/libpq shared client libraries. A provider is unavailable
if its headers were absent at build time, its required client symbols are absent,
or its shared library cannot load. A capability check does not connect to a
server or promise authentication success. Missing providers do not prevent the
tools or a native program from starting. No server is bundled with the SDK.

SQLite requires the `sqlite3_changes64`/`sqlite3_total_changes64` API.
PostgreSQL requires libpq single-row retrieval support. The
PostgreSQL provider currently supports POSIX socket waiting; Windows reports it
unavailable. Linux is the validated platform for this slice. AOT programs link
the SDK's static `libnil` and the platform loader, with no build-machine SQLite
or libpq library path embedded in the executable. The shared `libnil` exports
the same additive `nyrt_db2_*` C ABI; existing ABI version and exports remain.

The old `nilstd/experimental/db/postgresql.ny` declarations are retained as
historical design material. Their VM-only `db.*` dispatcher is no longer enabled
by the build. New code should use this shared `db2.*`-backed API.

## Bounds, timeouts, and transaction behavior

Each context allows 32 connections and 128 live results. Execution accepts at
most 256 parameters, 1 MiB of SQL, and 16 MiB of parameter text in total. A result
retains at most 10,000 rows, 256 columns, and 16 MiB including cell/row metadata,
column names, and text. All text is NUL-free. SQLite BLOB and PostgreSQL bytea
cells are rejected; numeric and boolean SQL values are returned as provider text.
This is not yet a typed SQL value or binary streaming API.

Only one statement is accepted per execution. SQLite checks statement boundaries
before preparing SQL because some PRAGMAs have effects during preparation. Quoted
semicolons and trailing SQL comments are accepted; compound trigger definitions
with internal statement separators are outside this first slice. PostgreSQL
uses the single-statement extended-query protocol. COPY is unsupported.

Timeouts must be between 1 and 30,000 milliseconds. SQLite checks a shared
deadline during VM instruction progress and lock waiting. PostgreSQL uses
nonblocking connect/send/receive and closes a connection after a transport error,
timeout, unsupported COPY, or result-limit failure. Ordinary PostgreSQL SQL errors
are drained and leave the connection available; a failed explicit transaction
still requires rollback. SQLite leaves the connection available after an error.

These are cooperative database deadlines, not an operating-system cancellation
guarantee: filesystem opens, provider allocation, and individual host calls can
outlast the requested deadline. PostgreSQL single-row retrieval bounds retained
results; libpq may temporarily allocate a large server row before it can be
checked. SQLite's own value/column limits are also configured.

An error does not imply rollback or prove that a write did not happen. SQL may
have produced effects before a row limit, timeout, or transport failure was
observed. Use explicit `BEGIN`, `COMMIT`, and `ROLLBACK` statements when an
application needs transactional behavior. Connection loss can still leave commit
outcome uncertain. Results must be explicitly released during long executions;
context cleanup is a final fallback.

Errors never echo SQL, bound values, connection strings, or paths. SQLite errors
include the provider's generic status description and SQL byte offset when that
client library exposes one. PostgreSQL SQL failures include SQLSTATE when
available. Full provider error messages are intentionally excluded because they
can contain query values or credentials.

## Validation

`ny.routes.database-sqlite-runtime` exercises all six execution routes, including
binding, NULL/empty distinction, stale handles, bounds, multiple-statement
rejection before effects, and CPU timeout. `nil.database-runtime` additionally
checks independent contexts, reset cleanup, file persistence, lock timeout, and
handle/result budgets. `tests/tools/database_contract.py` rejects 55 forged
native database signatures while preserving a previous output artifact.

`tests/database_postgresql_isolated.py` creates a private temporary cluster with
no TCP listener, runs provider and all six route tests, and stops it in `finally`.
Pass server binaries with `--postgres-bin` (or `NIL_TEST_POSTGRES_BIN`) and optional
`--postgres-share` (or `NIL_TEST_POSTGRES_SHARE`). Without explicit server binaries
this optional test skips. It never uses an existing database. Local validation
used PostgreSQL 18.6 and SQLite 3.46.1.
