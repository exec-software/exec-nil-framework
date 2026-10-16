@pragma once
# =============================================================================
# ny.lang.linq — LINQ Package
# =============================================================================
# Language-Integrated Query operations over any ICollection<T>.
# Mirrors C# System.Linq.
#
# Provides (all in enumerable.ny):
#   Filtering  : where, of_type, distinct
#   Projection : select, select_many
#   Ordering   : order_by, order_by_descending, then_by
#   Aggregation: count, sum, min, max, average
#   Elements   : first, last, single, element_at
#   Quantifiers: any, all, contains
#   Set ops    : union, intersect, except, concat
#   Generation : range, repeat, empty
#   Partitioning: take, skip, take_while, skip_while
#   Conversion : to_list, to_array, group_by
#
# Cross-refs:
#   ny.lang.collections — operates on ICollection<T>, List<T>
#   ny.lang.core        — Iterator<T>, Option<T>
# =============================================================================

package nil.std.linq

# Experimental: enumerable
