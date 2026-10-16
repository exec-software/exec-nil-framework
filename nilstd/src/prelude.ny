# prelude.ny — Candidate standard prelude.
#
# This file is installed with nilstd but is not automatically injected by
# nilcompiler while the library and package re-export semantics are still being
# stabilized.

import nil.std.core

# Core types available everywhere without qualification:
#   Option<T>, Result<T,E>, text, bool, i32, i64, f32, f64, u8 ...
#   Clone, Display, Debug, Default, Eq, Ord, Hash traits
#   Iterator<T>, Into<T>, From<T>
#   Box<T>, Rc<T>, Span<T>
#   panic(), print()

# Note: 'pub use' to re-export into the consumer's scope is not yet
# supported by the parser — consumers import ny.lang.core directly or
# wait for a future, explicit prelude contract.
