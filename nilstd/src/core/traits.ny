@pragma once

module nil.std.core

# Fundamental behavior contracts used by Option, Result, and the collection
# library.  These declarations intentionally stay small: a trait defines a
# callable contract, while concrete policy and helper algorithms live in
# their own modules.

pub trait Clone
    fn clone(self) -> Self
end trait

pub trait Display
    fn to_string(self) -> text
end trait

pub trait Debug
    fn debug(self) -> text
end trait

pub trait Default
    fn default() -> Self
end trait

pub trait Eq
    fn eq(self, other: Self) -> bool
    fn ne(self, other: Self) -> bool
end trait

pub trait Hash
    fn hash(self) -> i64
end trait
