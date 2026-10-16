# Matrix route fixtures

Fixtures driven by `tests/routes/ny_route_matrix.py` and registered in CMake
as `matrix.routes.*`.

## `inline-assembly-called.matrix`

Sample 13 declares an `assembly` paragraph but never calls it, so running the
sample cannot show what any route does with assembly that *executes*. This
page calls it, and the expectations pin the contract from
[`sample 13's own comments`](../../../matrix/samples/13-context-and-interop.matrix):

- `inline-assembly-interpreted.stderr` — the four interpreted routes
  (text, binary, bundle-text, bundle-binary) exit 1 with
  `INLINE_ASM requires a matching LLVM native route`. The operation is
  **rejected**, never erased or emulated: a program that means one thing on
  native routes and quietly means nothing on interpreted routes is worse than
  one that fails everywhere.
- `inline-assembly-native.stdout` — the LLVM routes execute the same NIL.
  `bswapq` of 1 is `0x0100000000000000`, printed as a `number`.

## `<sample>.stdout`

Expected stdout for running `matrix/samples/08-faults`, `09-memory`,
`10-concurrency-and-time` and `13-context-and-interop` on each of the six
execution routes. These are the samples themselves — the README calls the
corpus normative, so a route test against a drifted copy would prove nothing
about the sample.
