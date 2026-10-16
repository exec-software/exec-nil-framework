# Inventory model

This application reduces an inventory item after an order and asks its stock
policy whether replenishment is required. It exercises struct literals, field
reads and mutation, a retained trait declaration, an impl block, and concrete
method dispatch. Ny does not yet enforce that the impl satisfies every method
in the declared trait, so this is not a trait-conformance contract yet.

`expected.stdout` and `expected.exit` are the executable contract. Both NIL
formats and both bundled routes pass. LLVM JIT and AOT are checked XFAILs on
the exact missing-metadata diagnostic until native lowering preserves the
concrete receiver schema on implicit `self` in trait-impl method bodies.
