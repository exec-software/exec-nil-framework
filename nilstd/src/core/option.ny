@pragma once
# =============================================================================
# Ny.Language.Core.Option
# =============================================================================
# The Option<T> type represents an optional value - either Some(T) or None.
# This is used for values that may or may not be present, eliminating null.
#
# Usage:
#   let maybe_value: Option<i32> = Option.Some(42)
#   let nothing: Option<i32> = Option.None
#
#   if maybe_value.is_some() then
#       let value = maybe_value.unwrap()
#   end if
# =============================================================================

module nil.std.core

import nil.std.core.traits
import nil.std.core.result


# =============================================================================
# Option<T> Enum
# =============================================================================

pub enum Option<T>
    Some(T)
    None
end enum

# =============================================================================
# Option<T> Implementation
# =============================================================================

pub impl<T> Option<T>
    # ---------------------------------------------------------------------
    # Constructors
    # ---------------------------------------------------------------------

    # Create a Some variant with value
    pub fn some(value: T) -> Option<T>
        return Option.Some(value)
    end fn

    # Create a None variant
    pub fn none() -> Option<T>
        return Option.None
    end fn

    # ---------------------------------------------------------------------
    # Query Methods
    # ---------------------------------------------------------------------

    # Returns true if the option is Some
    pub fn is_some(self) -> bool
        match self
            Option.Some(_) -> return true
            Option.None -> return false
        end match
    end fn

    # Returns true if the option is None
    pub fn is_none(self) -> bool
        match self
            Option.Some(_) -> return false
            Option.None -> return true
        end match
    end fn

    # Returns true if Some and the value satisfies the predicate
    pub fn is_some_and(self, predicate: fn(T) -> bool) -> bool
        match self
            Option.Some(value) -> return predicate(value)
            Option.None -> return false
        end match
    end fn

    # ---------------------------------------------------------------------
    # Extraction Methods
    # ---------------------------------------------------------------------

    # Returns the contained value, panics if None
    pub fn unwrap(self) -> T
        match self
            Option.Some(value) -> return value
            Option.None -> panic("Called unwrap() on None value")
        end match
    end fn

    # Returns the contained value or panics with custom message
    pub fn expect(self, message: text) -> T
        match self
            Option.Some(value) -> return value
            Option.None -> panic(message)
        end match
    end fn

    # Returns the contained value or a default
    pub fn unwrap_or(self, default: T) -> T
        match self
            Option.Some(value) -> return value
            Option.None -> return default
        end match
    end fn

    # Returns the contained value or computes from closure
    pub fn unwrap_or_else(self, default_fn: fn() -> T) -> T
        match self
            Option.Some(value) -> return value
            Option.None -> return default_fn()
        end match
    end fn

    # ---------------------------------------------------------------------
    # Transformation Methods
    # ---------------------------------------------------------------------

    # Maps Option<T> to Option<U> by applying f to contained value
    pub fn map<U>(self, f: fn(T) -> U) -> Option<U>
        match self
            Option.Some(value) -> return Option.Some(f(value))
            Option.None -> return Option.None
        end match
    end fn

    # Maps Option<T> to U by applying f or returning default
    pub fn map_or<U>(self, default: U, f: fn(T) -> U) -> U
        match self
            Option.Some(value) -> return f(value)
            Option.None -> return default
        end match
    end fn

    # Maps Option<T> to U by applying f or computing default
    pub fn map_or_else<U>(self, default_fn: fn() -> U, f: fn(T) -> U) -> U
        match self
            Option.Some(value) -> return f(value)
            Option.None -> return default_fn()
        end match
    end fn

    # Applies function that returns Option, flattens result
    pub fn and_then<U>(self, f: fn(T) -> Option<U>) -> Option<U>
        match self
            Option.Some(value) -> return f(value)
            Option.None -> return Option.None
        end match
    end fn

    # Alias for and_then (monadic bind)
    pub fn flat_map<U>(self, f: fn(T) -> Option<U>) -> Option<U>
        return self.and_then(f)
    end fn

    # Returns None if None, otherwise returns other
    pub fn and<U>(self, other: Option<U>) -> Option<U>
        match self
            Option.Some(_) -> return other
            Option.None -> return Option.None
        end match
    end fn

    # Returns self if Some, otherwise returns other
    pub fn or(self, other: Option<T>) -> Option<T>
        match self
            Option.Some(_) -> return self
            Option.None -> return other
        end match
    end fn

    # Returns self if Some, otherwise computes from closure
    pub fn or_else(self, f: fn() -> Option<T>) -> Option<T>
        match self
            Option.Some(_) -> return self
            Option.None -> return f()
        end match
    end fn

    # Returns Some if exactly one is Some, otherwise None
    pub fn xor(self, other: Option<T>) -> Option<T>
        match self
            Option.Some(_) ->
                match other
                    Option.Some(_) -> return Option.None
                    Option.None -> return self
                end match
            Option.None -> return other
        end match
    end fn

    # Filters Option based on predicate
    pub fn filter(self, predicate: fn(T) -> bool) -> Option<T>
        match self
            Option.Some(value) ->
                if predicate(value) then
                    return self
                else
                    return Option.None
                end if
            Option.None -> return Option.None
        end match
    end fn

    # Zips self with another Option
    pub fn zip<U>(self, other: Option<U>) -> Option<Tuple<T, U>>
        match self
            Option.Some(a) ->
                match other
                    Option.Some(b) -> return Option.Some(Tuple { first: a, second: b })
                    Option.None -> return Option.None
                end match
            Option.None -> return Option.None
        end match
    end fn

    # ---------------------------------------------------------------------
    # Conversion Methods
    # ---------------------------------------------------------------------

    # Converts Option<T> to Result<T, E>
    pub fn ok_or<E>(self, err: E) -> Result<T, E>
        match self
            Option.Some(value) -> return Result.Ok(value)
            Option.None -> return Result.Error(err)
        end match
    end fn

    # Converts Option<T> to Result<T, E> with error from closure
    pub fn ok_or_else<E>(self, err_fn: fn() -> E) -> Result<T, E>
        match self
            Option.Some(value) -> return Result.Ok(value)
            Option.None -> return Result.Error(err_fn())
        end match
    end fn

    # ---------------------------------------------------------------------
    # Iteration Methods
    # ---------------------------------------------------------------------

    # Returns iterator over contained value (0 or 1 items)
    pub fn iter(self) -> OptionIterator<T>
        return OptionIterator { option: self, done: false }
    end fn
end impl

# =============================================================================
# Option Iterator
# =============================================================================

pub struct OptionIterator<T>
    option: Option<T>
    done: bool
end struct

pub impl<T> OptionIterator<T>
    pub fn next(mut self) -> Option<T>
        if self.done then
            return Option.None
        end if
        self.done = true
        return self.option
    end fn

    pub fn has_next(self) -> bool
        return not self.done and self.option.is_some()
    end fn
end impl

# =============================================================================
# Trait Implementations
# =============================================================================

pub impl<T: Clone> Clone for Option<T>
    fn clone(self) -> Option<T>
        match self
            Option.Some(value) -> return Option.Some(value.clone())
            Option.None -> return Option.None
        end match
    end fn
end impl

pub impl<T: Default> Default for Option<T>
    # Keep the method's requirement on a checked impl-level generic bound.
    pub fn unwrap_or_default(self) -> T
        match self
            Option.Some(value) -> return value
            Option.None -> return T.default()
        end match
    end fn

    fn default() -> Option<T>
        return Option.None
    end fn
end impl

pub impl<T: Eq> Eq for Option<T>
    fn eq(self, other: Option<T>) -> bool
        match self
            Option.Some(a) ->
                match other
                    Option.Some(b) -> return a.eq(b)
                    Option.None -> return false
                end match
            Option.None ->
                match other
                    Option.Some(_) -> return false
                    Option.None -> return true
                end match
        end match
    end fn

    fn ne(self, other: Option<T>) -> bool
        return not self.eq(other)
    end fn
end impl

pub impl<T: Debug> Debug for Option<T>
    fn debug(self) -> text
        match self
            Option.Some(value) -> return "Some(" + value.debug() + ")"
            Option.None -> return "None"
        end match
    end fn
end impl

pub impl<T: Display> Display for Option<T>
    fn to_string(self) -> text
        match self
            Option.Some(value) -> return value.to_string()
            Option.None -> return "None"
        end match
    end fn
end impl

# =============================================================================
# Helper Struct for Zip
# =============================================================================

pub struct Tuple<T1, T2>
    pub first: T1
    pub second: T2
end struct
