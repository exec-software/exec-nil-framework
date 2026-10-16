# Experimental database providers

`postgresql.ny` preserves the libpq-backed connection and result design. It is
not part of active `nilstd`: the provider is build-optional, absent from the
current configured toolchain, and not represented in the SDK manifest.

Promote it only with a provider capability flag, lifecycle/handle hardening,
and integration tests against an isolated PostgreSQL instance. Pure SQL
construction remains active in `src/db/query_builder.ny`.
