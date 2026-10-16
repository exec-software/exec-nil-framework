@pragma once

module nil.std.collections

import nil.std.core.*

# A deterministic hash map with collision-safe equality checks. Parallel
# vectors avoid fabricating default K/V values for unused buckets while still
# preserving the original public storage view.
pub struct HashMap<K, V>
    pub keys: vec<K>
    pub vals: vec<V>
    pub occupied: vec<bool>
    pub len: i32
    pub capacity: i32
    hashes: vec<i64>
    length_cell: array<i32>
end struct

impl<K: Hash + Eq, V> HashMap<K, V>
    pub fn new() -> HashMap<K, V>
        return HashMap<K, V> {
            keys: vec<K>.new(),
            vals: vec<V>.new(),
            occupied: vec<bool>.new(),
            len: 0,
            capacity: 16,
            hashes: vec<i64>.new(),
            length_cell: [0]
        }
    end fn

    pub fn length(self) -> i32
        return self.length_cell[0]
    end fn

    pub fn is_empty(self) -> bool
        return self.length() == 0
    end fn

    pub fn insert(mut self, key: K, value: V) -> void
        let key_hash: i64 = key.hash()
        let mut i: i32 = 0
        while i < self.length()
            if self.hashes.get(i).unwrap() == key_hash and
               self.keys.get(i).unwrap().eq(key) then
                self.vals.set(i, value)
                return
            end if
            i = i + 1
        end while

        self.keys.push(key)
        self.vals.push(value)
        self.occupied.push(true)
        self.hashes.push(key_hash)
        self.length_cell[0] = self.length() + 1
        self.len = self.length()
        if self.length() > self.capacity then
            self.capacity = self.capacity * 2
        end if
    end fn

    pub fn get(self, key: K) -> Option<V>
        let key_hash: i64 = key.hash()
        let mut i: i32 = 0
        while i < self.length()
            if self.hashes.get(i).unwrap() == key_hash and
               self.keys.get(i).unwrap().eq(key) then
                return self.vals.get(i)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn contains_key(self, key: K) -> bool
        return self.get(key).is_some()
    end fn

    pub fn remove(mut self, key: K) -> Option<V>
        let key_hash: i64 = key.hash()
        let mut i: i32 = 0
        while i < self.length()
            if self.hashes.get(i).unwrap() == key_hash and
               self.keys.get(i).unwrap().eq(key) then
                let old_value: V = self.vals.get(i).unwrap()
                self.keys.remove_at(i)
                self.vals.remove_at(i)
                self.occupied.remove_at(i)
                self.hashes.remove_at(i)
                self.length_cell[0] = self.length() - 1
                self.len = self.length()
                return Option.Some(old_value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn clear(mut self) -> void
        self.keys.clear()
        self.vals.clear()
        self.occupied.clear()
        self.hashes.clear()
        self.length_cell[0] = 0
        self.len = 0
    end fn
end impl
