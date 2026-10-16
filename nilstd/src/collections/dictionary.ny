@pragma once

module nil.std.collections

import nil.std.core.*

# Dictionary exposes the familiar key/value API while sharing the deterministic
# map implementation used by the rest of the package.
pub struct dictionary<TKey, TValue>
    items: map<TKey, TValue>
end struct

impl<TKey, TValue> IDictionary<TKey, TValue> for dictionary<TKey, TValue>
    pub fn new() -> dictionary<TKey, TValue>
        return dictionary<TKey, TValue> { items: map<TKey, TValue>.new() }
    end fn

    pub fn get_item(self, key: TKey) -> Option<TValue>
        return self.items.get(key)
    end fn

    pub fn set_item(mut self, key: TKey, value: TValue) -> void
        self.items.insert(key, value)
    end fn

    pub fn keys(self) -> vec<TKey>
        let mut result: vec<TKey> = vec.new()
        let mut iterator: map_iterator<TKey, TValue> = self.items.iter()
        while iterator.has_next()
            result.push(iterator.next().unwrap().key)
        end while
        return result
    end fn

    pub fn values(self) -> vec<TValue>
        let mut result: vec<TValue> = vec.new()
        let mut iterator: map_iterator<TKey, TValue> = self.items.iter()
        while iterator.has_next()
            result.push(iterator.next().unwrap().value)
        end while
        return result
    end fn

    pub fn contains_key(self, key: TKey) -> bool
        return self.items.contains(key)
    end fn

    pub fn try_get_value(self, key: TKey) -> Option<TValue>
        return self.items.get(key)
    end fn

    pub fn add_key_value(mut self, key: TKey, value: TValue) -> void
        if self.contains_key(key) then
            panic("Dictionary already contains key")
        end if
        self.items.insert(key, value)
    end fn

    pub fn count(self) -> i32
        return self.items.length()
    end fn

    pub fn is_read_only(self) -> bool
        return false
    end fn

    pub fn add(mut self, item: key_value_pair<TKey, TValue>) -> void
        self.add_key_value(item.key, item.value)
    end fn

    pub fn clear(mut self) -> void
        self.items.clear()
    end fn

    pub fn contains(self, item: key_value_pair<TKey, TValue>) -> bool
        let value: Option<TValue> = self.items.get(item.key)
        return value.is_some() and value.unwrap() == item.value
    end fn

    pub fn copy_to(self, destination: array<key_value_pair<TKey, TValue>>, array_index: i32) -> void
        let destination_length: i32 = destination.len()
        if array_index < 0 or array_index > destination_length then
            panic("Dictionary copy_to destination is too small")
        end if
        # Subtract only after validating the offset, so large offsets cannot
        # overflow a count-plus-offset check and bypass the capacity guard.
        if self.count() > destination_length - array_index then
            panic("Dictionary copy_to destination is too small")
        end if

        let mut i: i32 = 0
        let mut iterator: map_iterator<TKey, TValue> = self.items.iter()
        while iterator.has_next()
            let entry: map_entry<TKey, TValue> = iterator.next().unwrap()
            destination.set(
                array_index + i,
                key_value_pair<TKey, TValue> { key: entry.key, value: entry.value }
            )
            i = i + 1
        end while
    end fn

    pub fn remove(mut self, item: key_value_pair<TKey, TValue>) -> bool
        if not self.contains(item) then
            return false
        end if
        self.items.remove(item.key)
        return true
    end fn

    pub fn remove_key(mut self, key: TKey) -> Option<TValue>
        return self.items.remove(key)
    end fn
end impl
