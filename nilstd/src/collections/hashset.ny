@pragma once

module nil.std.collections

import nil.std.core.*

pub struct hash_set<T>
    items: set<T>
end struct

impl<T> ISet<T> for hash_set<T>
    pub fn new() -> hash_set<T>
        return hash_set<T> { items: set<T>.new() }
    end fn

    pub fn add(mut self, item: T) -> bool
        return self.items.insert(item)
    end fn

    pub fn except_with(mut self, other: hash_set<T>) -> void
        let values: vec<T> = other.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            self.items.remove(values.get(i).unwrap())
            i = i + 1
        end while
    end fn

    pub fn intersect_with(mut self, other: hash_set<T>) -> void
        let values: vec<T> = self.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            let value: T = values.get(i).unwrap()
            if not other.contains(value) then
                self.items.remove(value)
            end if
            i = i + 1
        end while
    end fn

    pub fn is_proper_subset_of(self, other: hash_set<T>) -> bool
        return self.count() < other.count() and self.is_subset_of(other)
    end fn

    pub fn is_proper_superset_of(self, other: hash_set<T>) -> bool
        return self.count() > other.count() and self.is_superset_of(other)
    end fn

    pub fn is_subset_of(self, other: hash_set<T>) -> bool
        let values: vec<T> = self.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            if not other.contains(values.get(i).unwrap()) then
                return false
            end if
            i = i + 1
        end while
        return true
    end fn

    pub fn is_superset_of(self, other: hash_set<T>) -> bool
        return other.is_subset_of(self)
    end fn

    pub fn overlaps(self, other: hash_set<T>) -> bool
        let values: vec<T> = self.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            if other.contains(values.get(i).unwrap()) then
                return true
            end if
            i = i + 1
        end while
        return false
    end fn

    pub fn set_equals(self, other: hash_set<T>) -> bool
        return self.count() == other.count() and self.is_subset_of(other)
    end fn

    pub fn symmetric_except_with(mut self, other: hash_set<T>) -> void
        let values: vec<T> = other.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            let value: T = values.get(i).unwrap()
            if self.contains(value) then
                self.items.remove(value)
            else
                self.items.insert(value)
            end if
            i = i + 1
        end while
    end fn

    pub fn union_with(mut self, other: hash_set<T>) -> void
        let values: vec<T> = other.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            self.items.insert(values.get(i).unwrap())
            i = i + 1
        end while
    end fn

    pub fn count(self) -> i32
        return self.items.length()
    end fn

    pub fn is_read_only(self) -> bool
        return false
    end fn

    pub fn clear(mut self) -> void
        self.items.clear()
    end fn

    pub fn contains(self, item: T) -> bool
        return self.items.contains(item)
    end fn

    pub fn copy_to(self, destination: array<T>, array_index: i32) -> void
        let destination_length: i32 = destination.len()
        if array_index < 0 or array_index > destination_length then
            panic("HashSet copy_to destination is too small")
        end if
        # Subtract only after validating the offset, so large offsets cannot
        # overflow a count-plus-offset check and bypass the capacity guard.
        if self.count() > destination_length - array_index then
            panic("HashSet copy_to destination is too small")
        end if

        let values: vec<T> = self.items.to_vec()
        let mut i: i32 = 0
        while i < values.length()
            destination.set(array_index + i, values.get(i).unwrap())
            i = i + 1
        end while
    end fn

    pub fn remove(mut self, item: T) -> bool
        return self.items.remove(item)
    end fn
end impl
