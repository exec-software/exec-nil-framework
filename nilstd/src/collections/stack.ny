@pragma once

module nil.std.collections

import nil.std.core.*

pub struct stack<T>
    items: vec<T>
end struct

impl<T> ICollection<T> for stack<T>
    pub fn new() -> stack<T>
        return stack<T> { items: vec<T>.new() }
    end fn

    pub fn with_capacity(capacity: i32) -> stack<T>
        return stack<T> { items: vec<T>.with_capacity(capacity) }
    end fn

    pub fn push(mut self, item: T) -> void
        self.items.push(item)
    end fn

    pub fn pop(mut self) -> Option<T>
        return self.items.pop()
    end fn

    pub fn peek(self) -> Option<T>
        return self.items.get(self.items.length() - 1)
    end fn

    pub fn is_empty(self) -> bool
        return self.items.is_empty()
    end fn

    pub fn count(self) -> i32
        return self.items.length()
    end fn

    pub fn is_read_only(self) -> bool
        return false
    end fn

    pub fn add(mut self, item: T) -> void
        self.push(item)
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
            panic("Stack copy_to destination is too small")
        end if
        # Subtract only after validating the offset, so large offsets cannot
        # overflow a count-plus-offset check and bypass the capacity guard.
        if self.count() > destination_length - array_index then
            panic("Stack copy_to destination is too small")
        end if
        let mut i: i32 = 0
        while i < self.count()
            destination.set(array_index + i, self.items.get(i).unwrap())
            i = i + 1
        end while
    end fn

    pub fn remove(mut self, item: T) -> bool
        let mut i: i32 = 0
        while i < self.count()
            if self.items.get(i).unwrap() == item then
                self.items.remove_at(i)
                return true
            end if
            i = i + 1
        end while
        return false
    end fn
end impl
