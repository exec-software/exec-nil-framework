@pragma once

module nil.std.collections

import nil.std.core.*

pub struct set<T>
    data: map<T, bool>
end struct

pub struct set_iterator<T>
    inner: map_iterator<T, bool>
end struct

impl<T: Default> Default for set<T>
    fn default() -> set<T>
        return set<T> { data: map<T, bool>.new() }
    end fn
end impl

impl<T: Debug> Debug for set<T>
    fn debug(self) -> text
        return "set"
    end fn
end impl

impl<T> set<T>
    pub fn new() -> set<T>
        return set<T> { data: map<T, bool>.new() }
    end fn

    pub fn insert(mut self, value: T) -> bool
        return self.data.insert(value, true).is_none()
    end fn

    pub fn contains(self, value: T) -> bool
        return self.data.contains(value)
    end fn

    pub fn remove(mut self, value: T) -> bool
        return self.data.remove(value).is_some()
    end fn

    pub fn length(self) -> i32
        return self.data.length()
    end fn

    pub fn clear(mut self) -> void
        self.data.clear()
    end fn

    pub fn iter(self) -> set_iterator<T>
        return set_iterator<T> { inner: self.data.iter() }
    end fn

    pub fn to_vec(self) -> vec<T>
        let mut result: vec<T> = vec.new()
        let mut iterator: set_iterator<T> = self.iter()
        while iterator.has_next()
            result.push(iterator.next().unwrap())
        end while
        return result
    end fn
end impl

impl<T> set_iterator<T>
    pub fn has_next(self) -> bool
        return self.inner.has_next()
    end fn

    pub fn next(mut self) -> Option<T>
        let entry: Option<map_entry<T, bool>> = self.inner.next()
        if entry.is_none() then
            return Option.None
        end if
        return Option.Some(entry.unwrap().key)
    end fn
end impl
