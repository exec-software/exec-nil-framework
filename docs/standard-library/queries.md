# Collected queries

Ny and Matrix support eager array queries over `i32`, `i64`, `u8`, `bool`,
`f64`, text and structural tuples of those values. They share typing and NIL lowering. No callbacks escape
and no database provider is selected implicitly.

```matrix
value orders is [(101, false, 4.5), (102, true, 9.0), (103, false, 7.0)]
value selected is collect begin query
    from each order in orders
    where order.1 equals false
    order by order.2 descending
    take 10
    give (order.0, order.2)
end query
```

```ny
let orders = [(101, false, 4.5), (102, true, 9.0), (103, false, 7.0)]
let selected = collect query
    from order in orders
    where order.1 == false
    order by order.2 descending
    take 10
    select (order.0, order.2)
end query
```

Both produce `[(103, 7.0), (101, 4.5)]`. The row binder is scoped to the query's
predicate, order key and projection. Existing names in the enclosing scope
remain available. The result is an ordinary array with independent storage and
shallow element copies.

Clauses appear once in this order: `from`, optional `where`, optional `order by`,
optional `skip`, optional `take`, and required `select` (Ny) or `give` (Matrix).
`order by` accepts one scalar or text key and optional `ascending`/`descending`;
ascending is the default. Duplicate, missing and out-of-order clauses are errors.
The `from` source must be an array; direct record, enum and nested-array elements
are outside this initial query profile. Tuple projections are supported, including
singleton and nested tuples of supported values.

## Evaluation

1. Evaluate the source expression once.
2. Evaluate `skip`, then `take`, once in the enclosing scope. Counts must be
   `i32`, `i64` or `u8` integers. Negative counts fail before row processing.
3. Copy the source's current elements into a shallow snapshot.
4. Without ordering, walk rows in input order, filter, skip accepted rows, and
   project at most `take` rows. Stop after that many selected rows. A zero take
   evaluates no row predicates or projections.
5. With ordering, evaluate the predicate for every row and the key once for each
   accepted row. Stable merge sort the accepted indices, paginate, then project
   selected rows once each.

Equal keys preserve input order. Floating NaNs sort after finite/infinite values
in both directions; NaNs retain input order among themselves. Text ordering uses
the existing bytewise text comparison. Queries without ordering are linear in
the rows visited plus snapshot creation; ordered queries add O(n log n) sorting
and temporary index/key arrays. This is eager execution with explicit temporary
storage, not a lazy LINQ iterator implementation. Side effects follow the order
above; mutation of the original array during row processing cannot change which
snapshot values are enumerated.

## Standard library terminals

Import/read `nil.std.collections`. These functions accept an already evaluated
array; they do not rerun its query or short-circuit work that already happened.

| Ny name / Matrix spelling | Result |
| --- | --- |
| `query_count` / `query-count` | `i64` / `number` length |
| `query_any` / `query-any` | Whether the result has any rows |
| `query_first` / `query-first` | `Option<T>` first row |
| `query_last` / `query-last` | `Option<T>` last row |
| `query_single` / `query-single` | `Result<T, text>`; distinct empty/multiple errors |

Use `take 1` before projection when only a first result is needed. Existing
numeric aggregation helpers can consume collected numeric arrays. Empty-result
terminals preserve absence instead of returning a fabricated zero or null.

Grouping, joins, distinct, multiple order keys, deferred iteration and database
translation remain separate follow-up work. SQLite/PostgreSQL support continues
through `nil.std.db`; a query expression never silently downloads a table or
substitutes client evaluation for SQL. Legacy experimental LINQ files remain
excluded from the shipped standard library.
