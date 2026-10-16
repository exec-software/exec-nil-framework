# Database and networking capability boundary

NIL separates deterministic protocol helpers from optional executable providers. Active source is an executable support claim;
design-only declarations live under `nilstd/experimental`.

## `nil.std.db`

The active package contains a transport-independent parameterized SQL builder.
It builds `SELECT`, `INSERT`, `UPDATE`, and `DELETE` statements, preserves value
order, and emits PostgreSQL-style positional placeholders. Schema identifiers,
sort columns, and join expressions are trusted SQL fragments; runtime values
belong in the parameter APIs and are never interpolated into statement text.

`ny.routes.db-query-builder-runtime` checks exact SQL and parameter results in
both textual and binary NIL interpreter routes. The builder contains no
`@intrinsic` or `@interop` declarations.

Active database execution now uses a shared SQLite/PostgreSQL provider on VM and
native routes. See the [database contract](database.md) for capability detection,
bindings, resource ownership, limits, and isolated-server validation. The old
experimental `db_connection` API is not re-exported; `db_open` and the checked
result functions are the supported initial API. `ny.lang.db` remains an alias.

## `nil.std.net`

The active package contains transport-independent HTTP method/status values,
case-insensitive ASCII header lookup, and request/response construction. It has
no network side effects and contains no `@intrinsic` or `@interop` declarations.
`ny.routes.http-values-runtime` checks parsing, status classification, header
replacement, bodies, and authentication-header construction in textual and
binary NIL interpreter routes.

Network transports remain experimental:

- `http.send` has no compiler opcode, interpreter dispatcher, or linked native
  provider;
- the `ml_nyhttp` and `ml_nyweb` C++ sources have no CMake build/install target
  and are absent from the SDK;
- socket source shims are preserved but likewise have no SDK target; and
- WebSocket, IMAP, and SMTP sources depend on that unavailable socket layer and
  still contain design-era placeholders.

`ny.routes.reject-unbacked-db-net` verifies that the active packages do not
silently re-export PostgreSQL connections or an HTTP client. Promotion requires
a provider built into or shipped with the SDK, bounded failure behavior, and an
integration fixture for every claimed execution route.

The supported boundary is deliberately narrow. SQL builders do not validate
schema identifiers, raw join fragments, empty table names, or statement
completeness; those inputs must come from trusted program structure. The HTTP
status enum is a useful common subset rather than a complete registry. Native database provider evidence is recorded in the linked contract; HTTP
transport remains unimplemented.

## Compiler contracts exercised by these APIs

The cleanup initially exposed two lowering defects: an immediate method chain
after a static factory lost its receiver type, and enum-associated functions
were mistaken for variants. Both were repaired during this pass. The registered
fixtures deliberately retain fluent factory chains and calls to
`http_method.parse` and `http_status.from_code` as regression coverage.
