@pragma once

module nil.std.collections

import nil.std.core.*

# =============================================================================
# KeyValuePair<TKey, TValue> Struct (mirrors System.Collections.Generic.KeyValuePair<TKey,TValue>)
# =============================================================================

pub struct key_value_pair<TKey, TValue>
    pub key: TKey
    pub value: TValue
end struct

impl<TKey: Eq + Hash, TValue: Eq + Hash> Eq for key_value_pair<TKey, TValue>
    fn eq(self, other: key_value_pair<TKey, TValue>) -> bool
        return self.key.eq(other.key) and self.value.eq(other.value)
    end fn

    fn ne(self, other: key_value_pair<TKey, TValue>) -> bool
        return not self.eq(other)
    end fn
end impl

impl<TKey: Hash, TValue: Hash> Hash for key_value_pair<TKey, TValue>
    fn hash(self) -> i64
        return self.key.hash() ^ self.value.hash()
    end fn
end impl

impl<TKey: Debug, TValue: Debug> Debug for key_value_pair<TKey, TValue>
    fn debug(self) -> text
        return "KeyValuePair{key: " + self.key.debug() + ", value: " + self.value.debug() + "}"
    end fn
end impl

# =============================================================================
# IDictionary<TKey, TValue> Interface (mirrors System.Collections.Generic.IDictionary<TKey,TValue>)
# =============================================================================

# Independent contract: spell out collection requirements until supertraits
# have checked bound implication and dispatch support.
pub trait IDictionary<TKey, TValue>
    fn count(self) -> i32
    fn is_read_only(self) -> bool
    fn add(mut self, item: key_value_pair<TKey, TValue>) -> void
    fn clear(mut self) -> void
    fn contains(self, item: key_value_pair<TKey, TValue>) -> bool
    fn copy_to(self, destination: array<key_value_pair<TKey, TValue>>, array_index: i32) -> void
    fn remove(mut self, item: key_value_pair<TKey, TValue>) -> bool

    # Gets or sets the element with the specified key (indexer equivalent)
    fn get_item(self, key: TKey) -> Option<TValue>
    fn set_item(mut self, key: TKey, value: TValue) -> void

    # Gets an ICollection containing the keys of the dictionary
    fn keys(self) -> vec<TKey>

    # Gets an ICollection containing the values of the dictionary
    fn values(self) -> vec<TValue>

    # Determines whether the dictionary contains an element with the specified key
    fn contains_key(self, key: TKey) -> bool

    # Gets the value associated with the specified key
    fn try_get_value(self, key: TKey) -> Option<TValue>

    # Adds an element with the provided key and value to the dictionary
    fn add_key_value(mut self, key: TKey, value: TValue) -> void
end trait
