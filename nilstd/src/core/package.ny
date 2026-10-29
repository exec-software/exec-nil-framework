@pragma once
# =============================================================================
# ny.lang.core — Core Language Package
# =============================================================================
# Fundamental value types, behavior contracts, and numeric utilities.
#
# Provides:
#   - Option<T> and Result<T,E> value types (option.ny, result.ny)
#   - Clone, Display, Debug, Default, Eq, and Hash contracts (traits.ny)
#   - Standard error type with chaining (error.ny)
#   - Ordering enum and comparison helpers (ordering.ny)
#   - Array sorting, searching, aggregation, and shuffle (sorting.ny)
#   - Checked explicit text, number, base, byte, and scalar conversions
#     (convert.ny)
#   - Bounded integer algorithms (algorithm.ny)
#   - Fault and panic infrastructure (faults.ny)
#   - Assertion helpers (assert.ny)
#
# Cross-refs:
#   ny.lang.collections — uses Option, Result, Clone, Eq, and Hash from here
#   ny.lang.io          — uses Result<T,E> from here
#   ny.lang.math        — uses numeric primitives from here
#   ny.lang.text        — uses text extensions from here
# =============================================================================

package nil.std.core

import nil.std.core.traits
import nil.std.core.option
import nil.std.core.dynamic
import nil.std.core.result
import nil.std.core.error
import nil.std.core.ordering
import nil.std.core.sorting
import nil.std.core.sorting_indices
import nil.std.core.convert
import nil.std.core.algorithm
import nil.std.core.faults
import nil.std.core.assert

# Generic iterator, memory, exception, and functional-pattern designs remain
# quarantined in experimental/ until their runtime contracts are executable.
