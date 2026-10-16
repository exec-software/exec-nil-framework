@pragma once

module nil.std.collections

import nil.std.core.*

# An indexed, growable collection. `list` deliberately delegates storage and
# mutation to `vec` so every operation shares the same bounds behavior.
pub struct list<T>
    items: vec<T>
end struct

impl<T> IList<T> for list<T>
    pub fn new() -> list<T>
        return list<T> { items: vec<T>.new() }
    end fn

    pub fn with_capacity(capacity: i32) -> list<T>
        return list<T> { items: vec<T>.with_capacity(capacity) }
    end fn

    pub fn count(self) -> i32
        return self.items.length()
    end fn

    pub fn is_read_only(self) -> bool
        return false
    end fn

    pub fn get_item(self, index: i32) -> Option<T>
        return self.items.get(index)
    end fn

    pub fn set_item(mut self, index: i32, value: T) -> void
        self.items.set(index, value)
    end fn

    pub fn index_of(self, item: T) -> i32
        let mut i: i32 = 0
        while i < self.items.length()
            if self.items.get(i).unwrap() == item then
                return i
            end if
            i = i + 1
        end while
        return -1
    end fn

    pub fn insert(mut self, index: i32, item: T) -> void
        self.items.insert(index, item)
    end fn

    pub fn remove_at(mut self, index: i32) -> void
        if self.items.remove_at(index).is_none() then
            panic("List remove_at index out of bounds")
        end if
    end fn

    pub fn add(mut self, item: T) -> void
        self.items.push(item)
    end fn

    pub fn clear(mut self) -> void
        self.items.clear()
    end fn

    pub fn contains(self, item: T) -> bool
        return self.index_of(item) >= 0
    end fn

    pub fn copy_to(self, destination: array<T>, array_index: i32) -> void
        let destination_length: i32 = destination.len()
        if array_index < 0 or array_index > destination_length then
            panic("List copy_to destination is too small")
        end if
        # Subtract only after validating the offset, so large offsets cannot
        # overflow a count-plus-offset check and bypass the capacity guard.
        if self.count() > destination_length - array_index then
            panic("List copy_to destination is too small")
        end if

        let mut i: i32 = 0
        while i < self.items.length()
            destination.set(array_index + i, self.items.get(i).unwrap())
            i = i + 1
        end while
    end fn

    pub fn remove(mut self, item: T) -> bool
        let index: i32 = self.index_of(item)
        if index < 0 then
            return false
        end if
        self.items.remove_at(index)
        return true
    end fn
end impl
