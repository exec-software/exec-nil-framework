@pragma once

module nil.std.collections

import nil.std.core.*

# =============================================================================
# ICollection<T> Interface (mirrors System.Collections.Generic.ICollection<T>)
# =============================================================================

pub trait ICollection<T>
    # Gets the number of elements contained in the collection
    fn count(self) -> i32

    # Determines whether the collection is read-only
    fn is_read_only(self) -> bool

    # Adds an item to the collection
    fn add(mut self, item: T) -> void

    # Removes all items from the collection
    fn clear(mut self) -> void

    # Determines whether the collection contains a specific value
    fn contains(self, item: T) -> bool

    # Copies the elements of the collection to an array, starting at a particular array index
    fn copy_to(self, destination: array<T>, array_index: i32) -> void

    # Removes the first occurrence of a specific object from the collection
    fn remove(mut self, item: T) -> bool
end trait
