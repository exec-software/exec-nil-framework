@pragma once

module nil.std.collections

import nil.std.core.*

# Shared position cells keep FIFO mutations visible through value-type aliases.
# Slots form a ring; dequeue reuses storage instead of shifting every item.
# Unused initialized slots retain their values until overwritten or the backing
# storage is reclaimed; this API does not promise eager element destruction.
pub struct queue<T>
    items: vec<T>
    positions: array<i32>
end struct

impl<T> ICollection<T> for queue<T>
    pub fn new() -> queue<T>
        return queue<T> { items: vec<T>.new(), positions: [0, 0] }
    end fn

    pub fn with_capacity(capacity: i32) -> queue<T>
        return queue<T> { items: vec<T>.with_capacity(capacity), positions: [0, 0] }
    end fn

    fn slot(self, offset: i32) -> i32
        # Subtract before adding so even a large ring cannot overflow an index.
        let until_end: i32 = self.items.length() - self.positions[0]
        if offset < until_end then
            return self.positions[0] + offset
        end if
        return offset - until_end
    end fn

    pub fn enqueue(mut self, item: T) -> void
        let old_slots: i32 = self.items.length()
        if self.count() == old_slots then
            if old_slots == 0 then
                # Reserved slots need a real T before they can hold values.
                # Initializing them on the first enqueue avoids inventing a
                # default value or constraining the queue to Default types.
                let mut slots: i32 = self.items.capacity()
                if slots < 1 then slots = 1 end if
                let mut i: i32 = 0
                while i < slots
                    self.items.push(item)
                    i = i + 1
                end while
            else
                if old_slots > 1073741823 then
                    panic("Queue capacity exceeds the supported index range")
                end if
                # Copy the old FIFO into the fresh upper half before moving
                # it to the front. No unread wrapped entry is overwritten.
                let mut i: i32 = 0
                while i < old_slots
                    self.items.push(item)
                    i = i + 1
                end while
                i = 0
                while i < old_slots
                    let source: i32 = (self.positions[0] + i) % old_slots
                    self.items.set(old_slots + i, self.items.get(source).unwrap())
                    i = i + 1
                end while
                i = 0
                while i < old_slots
                    self.items.set(i, self.items.get(old_slots + i).unwrap())
                    i = i + 1
                end while
                self.positions[0] = 0
            end if
        end if
        self.items.set(self.slot(self.count()), item)
        self.positions[1] = self.count() + 1
    end fn

    pub fn dequeue(mut self) -> Option<T>
        if self.is_empty() then return Option.None end if
        let first: T = self.items.get(self.positions[0]).unwrap()
        self.positions[1] = self.count() - 1
        if self.is_empty() then
            self.positions[0] = 0
        else
            self.positions[0] = (self.positions[0] + 1) % self.items.length()
        end if
        return Option.Some(first)
    end fn

    pub fn peek(self) -> Option<T>
        if self.is_empty() then return Option.None end if
        return self.items.get(self.positions[0])
    end fn

    pub fn is_empty(self) -> bool
        return self.count() == 0
    end fn

    pub fn count(self) -> i32
        return self.positions[1]
    end fn

    pub fn is_read_only(self) -> bool
        return false
    end fn

    pub fn add(mut self, item: T) -> void
        self.enqueue(item)
    end fn

    pub fn clear(mut self) -> void
        self.positions[0] = 0
        self.positions[1] = 0
    end fn

    pub fn contains(self, item: T) -> bool
        let mut i: i32 = 0
        while i < self.count()
            if self.items.get(self.slot(i)).unwrap() == item then return true end if
            i = i + 1
        end while
        return false
    end fn

    pub fn copy_to(self, destination: array<T>, array_index: i32) -> void
        let destination_length: i32 = destination.len()
        if array_index < 0 or array_index > destination_length then
            panic("Queue copy_to destination is too small")
        end if
        # Subtract only after validating the offset, so large offsets cannot
        # overflow a count-plus-offset check and bypass the capacity guard.
        if self.count() > destination_length - array_index then
            panic("Queue copy_to destination is too small")
        end if
        let mut i: i32 = 0
        while i < self.count()
            destination.set(array_index + i, self.items.get(self.slot(i)).unwrap())
            i = i + 1
        end while
    end fn

    pub fn remove(mut self, item: T) -> bool
        let mut i: i32 = 0
        while i < self.count()
            if self.items.get(self.slot(i)).unwrap() == item then
                let mut next: i32 = i + 1
                while next < self.count()
                    self.items.set(self.slot(next - 1), self.items.get(self.slot(next)).unwrap())
                    next = next + 1
                end while
                self.positions[1] = self.count() - 1
                if self.is_empty() then self.positions[0] = 0 end if
                return true
            end if
            i = i + 1
        end while
        return false
    end fn
end impl
