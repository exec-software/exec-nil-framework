@pragma once

module nil.std.collections

import nil.std.core.*

# Hash buckets contain indices into dense parallel key/value vectors, so no
# default K/V values are fabricated for unused buckets. Collision chains use
# scalar indices and equality checks. Public vectors retain insertion order;
# removing an entry therefore compacts and rebuilds the index in O(n).
pub struct HashMap<K, V>
    pub keys: vec<K>
    pub vals: vec<V>
    pub occupied: vec<bool>
    pub len: i32
    pub capacity: i32
    hashes: vec<i64>
    buckets: vec<i32>
    next_entries: vec<i32>
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
            buckets: vec<i32>.new(),
            next_entries: vec<i32>.new(),
            length_cell: [0]
        }
    end fn

    pub fn length(self) -> i32
        return self.length_cell[0]
    end fn

    pub fn is_empty(self) -> bool
        return self.length() == 0
    end fn

    fn bucket_for(self, key_hash: i64) -> i32
        # Masking handles every signed hash, including the minimum i64,
        # without overflow from negating that value.
        let positive: i64 = key_hash & 9223372036854775807
        return (positive % (self.buckets.length() as i64)) as i32
    end fn

    fn entry_for(self, key: K, key_hash: i64) -> i32
        if self.is_empty() then return -1 end if
        let bucket: i32 = self.bucket_for(key_hash)
        let mut entry: i32 = self.buckets.get(bucket).unwrap()
        while entry >= 0
            if self.hashes.get(entry).unwrap() == key_hash and
               self.keys.get(entry).unwrap().eq(key) then
                return entry
            end if
            entry = self.next_entries.get(entry).unwrap()
        end while
        return -1
    end fn

    fn rebuild_buckets(mut self, bucket_count: i32) -> void
        let mut i: i32 = 0
        while i < self.buckets.length()
            self.buckets.set(i, -1)
            i = i + 1
        end while
        while self.buckets.length() < bucket_count
            self.buckets.push(-1)
        end while
        i = 0
        while i < self.length()
            let bucket: i32 = self.bucket_for(self.hashes.get(i).unwrap())
            self.next_entries.set(i, self.buckets.get(bucket).unwrap())
            self.buckets.set(bucket, i)
            i = i + 1
        end while
    end fn

    pub fn insert(mut self, key: K, value: V) -> void
        let key_hash: i64 = key.hash()
        let existing: i32 = self.entry_for(key, key_hash)
        if existing >= 0 then
            self.vals.set(existing, value)
            return
        end if
        if self.buckets.is_empty() then
            self.rebuild_buckets(16)
        else
            if self.length() == self.buckets.length() then
                if self.buckets.length() > 1073741823 then
                    panic("HashMap capacity exceeds the supported index range")
                end if
                self.rebuild_buckets(self.buckets.length() * 2)
            end if
        end if
        let entry: i32 = self.length()
        let bucket: i32 = self.bucket_for(key_hash)
        self.keys.push(key)
        self.vals.push(value)
        self.occupied.push(true)
        self.hashes.push(key_hash)
        self.next_entries.push(self.buckets.get(bucket).unwrap())
        self.buckets.set(bucket, entry)
        self.length_cell[0] = entry + 1
        self.len = self.length()
        self.capacity = self.buckets.length()
    end fn

    pub fn get(self, key: K) -> Option<V>
        let entry: i32 = self.entry_for(key, key.hash())
        if entry < 0 then return Option.None end if
        return self.vals.get(entry)
    end fn

    pub fn contains_key(self, key: K) -> bool
        return self.get(key).is_some()
    end fn

    pub fn remove(mut self, key: K) -> Option<V>
        let entry: i32 = self.entry_for(key, key.hash())
        if entry < 0 then return Option.None end if
        let old_value: V = self.vals.get(entry).unwrap()
        self.keys.remove_at(entry)
        self.vals.remove_at(entry)
        self.occupied.remove_at(entry)
        self.hashes.remove_at(entry)
        self.next_entries.remove_at(entry)
        self.length_cell[0] = self.length() - 1
        self.len = self.length()
        self.rebuild_buckets(self.buckets.length())
        return Option.Some(old_value)
    end fn

    pub fn clear(mut self) -> void
        self.keys.clear()
        self.vals.clear()
        self.occupied.clear()
        self.hashes.clear()
        self.next_entries.clear()
        self.length_cell[0] = 0
        self.len = 0
        self.rebuild_buckets(self.buckets.length())
    end fn
end impl
