@pragma once

module nil.std.collections

import nil.std.core.*

# =============================================================================
# ISet<T> Interface (mirrors System.Collections.Generic.ISet<T>)
# =============================================================================

# Independent contract: unlike ICollection.add, set insertion returns bool.
# No implicit supertrait relationship is claimed by this interface.
pub trait ISet<T>
    fn count(self) -> i32
    fn is_read_only(self) -> bool
    fn clear(mut self) -> void
    fn contains(self, item: T) -> bool
    fn copy_to(self, destination: array<T>, array_index: i32) -> void
    fn remove(mut self, item: T) -> bool

    # Adds an element to the current set and returns a value to indicate if the element was successfully added
    fn add(mut self, item: T) -> bool

    # Modifies the current set so that it contains only elements that are also in a specified collection
    fn intersect_with(mut self, other: Self) -> void

    # Modifies the current set so that it contains only elements that are present either in the current set or in the specified collection, but not both
    fn symmetric_except_with(mut self, other: Self) -> void

    # Determines whether the current set is a proper (strict) subset of a specified collection
    fn is_proper_subset_of(self, other: Self) -> bool

    # Determines whether the current set is a proper (strict) superset of a specified collection
    fn is_proper_superset_of(self, other: Self) -> bool

    # Determines whether the current set is a subset of a specified collection
    fn is_subset_of(self, other: Self) -> bool

    # Determines whether the current set is a superset of a specified collection
    fn is_superset_of(self, other: Self) -> bool

    # Determines whether the current set overlaps with the specified collection
    fn overlaps(self, other: Self) -> bool

    # Determines whether the current set and the specified collection contain the same elements
    fn set_equals(self, other: Self) -> bool

    # Modifies the current set so that it contains all elements that are present in the current set, in the specified collection, or in both
    fn union_with(mut self, other: Self) -> void

    # Removes all elements in the specified collection from the current set
    fn except_with(mut self, other: Self) -> void
end trait
