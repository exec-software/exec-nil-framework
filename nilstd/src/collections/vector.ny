@pragma once

module nil.std.collections

import nil.std.core.*

# A growable, interpreter-safe vector. The backing array owns element storage;
# a one-element array carries the logical length so mutations remain visible
# when a value-type vector is passed through a method call.
pub struct vec<T>
    pub items: array<T>
    pub length_cell: array<i32>
end struct

pub struct vec_iterator<T>
    items: array<T>
    index_cell: array<i32>
    stop: i32
end struct

impl<T> vec<T>
    pub fn new() -> vec<T>
        return vec<T> { items: [], length_cell: [0] }
    end fn

    # Capacity is advisory for the current growable-array runtime. Keeping this
    # constructor makes callers independent of the eventual allocator policy.
    pub fn with_capacity(capacity: i32) -> vec<T>
        return vec<T> { items: [], length_cell: [0] }
    end fn

    pub fn from(elements: array<T>) -> vec<T>
        let mut result: vec<T> = vec<T>.new()
        let mut i: i32 = 0
        while i < elements.len()
            result.push(elements[i])
            i = i + 1
        end while
        return result
    end fn

    pub fn length(self) -> i32
        return self.length_cell[0]
    end fn

    pub fn capacity(self) -> i32
        return self.items.len()
    end fn

    pub fn is_empty(self) -> bool
        return self.length() == 0
    end fn

    pub fn push(mut self, value: T) -> void
        let logical_length: i32 = self.length()
        if logical_length < self.items.len() then
            self.items.set(logical_length, value)
        else
            self.items.push(value)
        end if
        self.length_cell[0] = logical_length + 1
    end fn

    pub fn get(self, index: i32) -> Option<T>
        if index < 0 or index >= self.length() then
            return Option.None
        end if
        return Option.Some(self.items[index])
    end fn

    pub fn set(mut self, index: i32, value: T) -> void
        if index < 0 or index >= self.length() then
            panic("Vector index out of bounds")
        end if
        self.items.set(index, value)
    end fn

    pub fn insert(mut self, index: i32, value: T) -> void
        let logical_length: i32 = self.length()
        if index < 0 or index > logical_length then
            panic("Vector insert index out of bounds")
        end if

        # Materialize one writable tail slot before shifting values right.
        if logical_length < self.items.len() then
            self.items.set(logical_length, value)
        else
            self.items.push(value)
        end if

        let mut i: i32 = logical_length
        while i > index
            self.items.set(i, self.items[i - 1])
            i = i - 1
        end while
        self.items.set(index, value)
        self.length_cell[0] = logical_length + 1
    end fn

    pub fn remove_at(mut self, index: i32) -> Option<T>
        let logical_length: i32 = self.length()
        if index < 0 or index >= logical_length then
            return Option.None
        end if

        let removed: T = self.items[index]
        let mut i: i32 = index
        while i < logical_length - 1
            self.items.set(i, self.items[i + 1])
            i = i + 1
        end while
        self.length_cell[0] = logical_length - 1
        return Option.Some(removed)
    end fn

    pub fn pop(mut self) -> Option<T>
        return self.remove_at(self.length() - 1)
    end fn

    pub fn clear(mut self) -> void
        self.length_cell[0] = 0
    end fn

    pub fn contains(self, value: T) -> bool
        let mut i: i32 = 0
        while i < self.length()
            if self.items[i] == value then
                return true
            end if
            i = i + 1
        end while
        return false
    end fn

    pub fn iter(self) -> vec_iterator<T>
        return vec_iterator<T> {
            items: self.items,
            index_cell: [0],
            stop: self.length()
        }
    end fn

    pub fn map<U>(self, transform: fn(T) -> U) -> vec<U>
        let mut result: vec<U> = vec<U>.new()
        let mut i: i32 = 0
        while i < self.length()
            result.push(transform(self.items[i]))
            i = i + 1
        end while
        return result
    end fn

    pub fn filter(self, predicate: fn(T) -> bool) -> vec<T>
        let mut result: vec<T> = vec<T>.new()
        let mut i: i32 = 0
        while i < self.length()
            let value: T = self.items[i]
            if predicate(value) then
                result.push(value)
            end if
            i = i + 1
        end while
        return result
    end fn
end impl

impl<T> vec_iterator<T>
    pub fn has_next(self) -> bool
        return self.index_cell[0] < self.stop
    end fn

    pub fn next(mut self) -> Option<T>
        if not self.has_next() then
            return Option.None
        end if
        let index: i32 = self.index_cell[0]
        self.index_cell[0] = index + 1
        return Option.Some(self.items[index])
    end fn
end impl

# Compatibility spelling retained for code written against the early SDK.
pub type Vec<T> = vec<T>

# Explicitly-prefixed free forms avoid collisions in the shared
# `ny.lang.collections` namespace; method syntax remains the primary API.
pub fn vec_new<T>() -> vec<T>
    return vec<T>.new()
end fn

pub fn vec_with_capacity<T>(capacity: i32) -> vec<T>
    return vec<T>.with_capacity(capacity)
end fn

pub fn vec_from<T>(elements: array<T>) -> vec<T>
    return vec<T>.from(elements)
end fn

pub fn vec_length<T>(value: vec<T>) -> i32
    return value.length()
end fn

pub fn vec_capacity<T>(value: vec<T>) -> i32
    return value.capacity()
end fn

pub fn vec_is_empty<T>(value: vec<T>) -> bool
    return value.is_empty()
end fn

pub fn vec_push<T>(mut value: vec<T>, element: T) -> vec<T>
    value.push(element)
    return value
end fn

pub fn vec_get<T>(value: vec<T>, index: i32) -> Option<T>
    return value.get(index)
end fn

pub fn vec_set<T>(mut value: vec<T>, index: i32, element: T) -> vec<T>
    value.set(index, element)
    return value
end fn

pub fn vec_iter<T>(value: vec<T>) -> vec_iterator<T>
    return value.iter()
end fn

pub fn vec_map<T, U>(value: vec<T>, transform: fn(T) -> U) -> vec<U>
    return value.map(transform)
end fn

pub fn vec_filter<T>(value: vec<T>, predicate: fn(T) -> bool) -> vec<T>
    return value.filter(predicate)
end fn
