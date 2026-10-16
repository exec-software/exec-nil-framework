@pragma once

module nil.std.collections

import nil.std.core.*

# =============================================================================
# IList<T> Interface (mirrors System.Collections.Generic.IList<T>)
# =============================================================================

# Independent contract: supertrait inheritance is not implemented yet.
# Keep the collection requirements explicit so every impl is checked.
pub trait IList<T>
    fn count(self) -> i32
    fn is_read_only(self) -> bool
    fn add(mut self, item: T) -> void
    fn clear(mut self) -> void
    fn contains(self, item: T) -> bool
    fn copy_to(self, destination: array<T>, array_index: i32) -> void
    fn remove(mut self, item: T) -> bool

    # Gets or sets the element at the specified index
    fn get_item(self, index: i32) -> Option<T>
    fn set_item(mut self, index: i32, value: T) -> void

    # Determines the index of a specific item in the list
    fn index_of(self, item: T) -> i32

    # Inserts an item to the list at the specified index
    fn insert(mut self, index: i32, item: T) -> void

    # Removes the item at the specified index
    fn remove_at(mut self, index: i32) -> void
end trait
