@pragma once

module nil.std.collections

import nil.std.core.*

# Stable entry representation used by map iterators.
pub struct map_entry<K, V>
    pub key: K
    pub value: V
end struct

pub struct map_iterator<K, V>
    entries: vec<map_entry<K, V>>
    index_cell: array<i32>
    stop: i32
end struct

# A compact equality map. The implementation is intentionally linear until
# the runtime exposes allocator-aware bucket storage; its behavior is complete
# and deterministic for small maps rather than pretending to hash unsafely.
pub struct map<K, V>
    entries: vec<map_entry<K, V>>
end struct

impl<K: Default, V: Default> Default for map<K, V>
    fn default() -> map<K, V>
        let entries: vec<map_entry<K, V>> = vec.new()
        return map<K, V> { entries: entries }
    end fn
end impl

impl<K: Debug, V: Debug> Debug for map<K, V>
    fn debug(self) -> text
        let mut output: text = "map["
        let mut i: i32 = 0
        while i < self.entries.length()
            if i > 0 then
                output = output + ", "
            end if
            let entry: map_entry<K, V> = self.entries.get(i).unwrap()
            output = output + entry.key.debug() + ": " + entry.value.debug()
            i = i + 1
        end while
        return output + "]"
    end fn
end impl

impl<K, V> map<K, V>
    pub fn new() -> map<K, V>
        let entries: vec<map_entry<K, V>> = vec.new()
        return map<K, V> { entries: entries }
    end fn

    pub fn insert(mut self, key: K, value: V) -> Option<V>
        let mut i: i32 = 0
        while i < self.entries.length()
            let entry: map_entry<K, V> = self.entries.get(i).unwrap()
            if entry.key == key then
                let old_value: V = entry.value
                self.entries.set(i, map_entry<K, V> { key: key, value: value })
                return Option.Some(old_value)
            end if
            i = i + 1
        end while

        self.entries.push(map_entry<K, V> { key: key, value: value })
        return Option.None
    end fn

    pub fn get(self, key: K) -> Option<V>
        let mut i: i32 = 0
        while i < self.entries.length()
            let entry: map_entry<K, V> = self.entries.get(i).unwrap()
            if entry.key == key then
                return Option.Some(entry.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn remove(mut self, key: K) -> Option<V>
        let mut i: i32 = 0
        while i < self.entries.length()
            let entry: map_entry<K, V> = self.entries.get(i).unwrap()
            if entry.key == key then
                self.entries.remove_at(i)
                return Option.Some(entry.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn contains(self, key: K) -> bool
        return self.get(key).is_some()
    end fn

    pub fn length(self) -> i32
        return self.entries.length()
    end fn

    pub fn clear(mut self) -> void
        self.entries.clear()
    end fn

    pub fn iter(self) -> map_iterator<K, V>
        return map_iterator<K, V> {
            entries: self.entries,
            index_cell: [0],
            stop: self.entries.length()
        }
    end fn
end impl

impl<K, V> map_iterator<K, V>
    pub fn has_next(self) -> bool
        return self.index_cell[0] < self.stop
    end fn

    pub fn next(mut self) -> Option<map_entry<K, V>>
        if not self.has_next() then
            return Option.None
        end if
        let index: i32 = self.index_cell[0]
        self.index_cell[0] = index + 1
        return self.entries.get(index)
    end fn
end impl
