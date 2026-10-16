@schema
@pragma once

module core

# Primitive type aliases
pub type u8 = i8
pub type void = unit

# Intrinsics and core functions
pub function print(s: text)
end function

pub function to_string(n: i32) -> text
end function

# Option and Result types
pub enum option<T>
    some(T)
    none
end enum

impl<T> option<T>
    pub function unwrap(self) -> T
        match self
            option::some(val) -> return val
            option::none -> panic("Called unwrap on none")
        end match
    end function
end impl

pub enum result<T, E>
    ok(T)
    error(E)
end enum

impl<T, E> result<T, E>
    pub function unwrap(self) -> T
        match self
            result::ok(val) -> return val
            result::error(_) -> panic("Called unwrap on error")
        end match
    end function
end impl

# Vector and Collections
pub struct tuple<T1, T2>
    first: T1
    second: T2
end struct

pub struct vec<T>
    len: i32
end struct

impl<T> vec<T>
    pub function new() -> vec<T>
        return vec { len: 0 }
    end function
    pub function length(self) -> i32
        return self.len
    end function
end impl
