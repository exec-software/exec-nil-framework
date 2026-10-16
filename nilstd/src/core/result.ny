@pragma once
# =============================================================================
# Ny.Language.Core.Result
# =============================================================================
# The Result<T, E> type represents either success (Ok(T)) or failure (Error(E)).
# This is used for operations that can fail, providing explicit error handling.
#
# Usage:
#   let result: Result<i32, text> = Result.Ok(42)
#   let error: Result<i32, text> = Result.Error("Something went wrong")
#
#   match result
#       Result.Ok(value) -> print("Got: " + value.to_string())
#       Result.Error(err) -> print("Error: " + err)
#   end match
# =============================================================================

module nil.std.core


# =============================================================================
# Result<T, E> Enum
# =============================================================================

pub enum Result<T, E>
    Ok(T)
    Error(E)
end enum

# =============================================================================
# Result<T, E> Implementation
# =============================================================================

pub impl<T, E> Result<T, E>
    # ---------------------------------------------------------------------
    # Constructors
    # ---------------------------------------------------------------------

    # Create an Ok variant with value
    pub fn ok(value: T) -> Result<T, E>
        return Result.Ok(value)
    end fn

    # Create an Error variant with error
    pub fn error(err: E) -> Result<T, E>
        return Result.Error(err)
    end fn

    # ---------------------------------------------------------------------
    # Query Methods
    # ---------------------------------------------------------------------

    # Returns true if the result is Ok
    pub fn is_ok(self) -> bool
        match self
            Result.Ok(_) -> return true
            Result.Error(_) -> return false
        end match
    end fn

    # Returns true if the result is Error
    pub fn is_error(self) -> bool
        match self
            Result.Ok(_) -> return false
            Result.Error(_) -> return true
        end match
    end fn

    # Alias for is_error
    pub fn is_err(self) -> bool
        return self.is_error()
    end fn

    # Returns true if Ok and value satisfies predicate
    pub fn is_ok_and(self, predicate: fn(T) -> bool) -> bool
        match self
            Result.Ok(value) -> return predicate(value)
            Result.Error(_) -> return false
        end match
    end fn

    # Returns true if Error and error satisfies predicate
    pub fn is_error_and(self, predicate: fn(E) -> bool) -> bool
        match self
            Result.Ok(_) -> return false
            Result.Error(err) -> return predicate(err)
        end match
    end fn

    # ---------------------------------------------------------------------
    # Extraction Methods
    # ---------------------------------------------------------------------

    # Returns the Ok value, panics if Error
    pub fn unwrap(self) -> T
        match self
            Result.Ok(value) -> return value
            Result.Error(_) -> panic("Called unwrap() on Error value")
        end match
    end fn

    # Returns the Ok value or panics with custom message
    pub fn expect(self, message: text) -> T
        match self
            Result.Ok(value) -> return value
            Result.Error(_) -> panic(message)
        end match
    end fn

    # Returns the Error value, panics if Ok
    pub fn unwrap_error(self) -> E
        match self
            Result.Ok(_) -> panic("Called unwrap_error() on Ok value")
            Result.Error(err) -> return err
        end match
    end fn

    # Alias for unwrap_error
    pub fn unwrap_err(self) -> E
        return self.unwrap_error()
    end fn

    # Returns the Error value or panics with custom message
    pub fn expect_error(self, message: text) -> E
        match self
            Result.Ok(_) -> panic(message)
            Result.Error(err) -> return err
        end match
    end fn

    # Returns the Ok value or a default
    pub fn unwrap_or(self, default: T) -> T
        match self
            Result.Ok(value) -> return value
            Result.Error(_) -> return default
        end match
    end fn

    # Returns the Ok value or computes from closure
    pub fn unwrap_or_else(self, default_fn: fn(E) -> T) -> T
        match self
            Result.Ok(value) -> return value
            Result.Error(err) -> return default_fn(err)
        end match
    end fn

    # ---------------------------------------------------------------------
    # Transformation Methods
    # ---------------------------------------------------------------------

    # Maps Result<T, E> to Result<U, E> by applying f to Ok value
    pub fn map<U>(self, f: fn(T) -> U) -> Result<U, E>
        match self
            Result.Ok(value) -> return Result.Ok(f(value))
            Result.Error(err) -> return Result.Error(err)
        end match
    end fn

    # Maps Result<T, E> to U by applying f or returning default
    pub fn map_or<U>(self, default: U, f: fn(T) -> U) -> U
        match self
            Result.Ok(value) -> return f(value)
            Result.Error(_) -> return default
        end match
    end fn

    # Maps Result<T, E> to U by applying f or computing default
    pub fn map_or_else<U>(self, default_fn: fn(E) -> U, f: fn(T) -> U) -> U
        match self
            Result.Ok(value) -> return f(value)
            Result.Error(err) -> return default_fn(err)
        end match
    end fn

    # Maps Result<T, E> to Result<T, F> by applying f to Error value
    pub fn map_error<F>(self, f: fn(E) -> F) -> Result<T, F>
        match self
            Result.Ok(value) -> return Result.Ok(value)
            Result.Error(err) -> return Result.Error(f(err))
        end match
    end fn

    # Alias for map_error
    pub fn map_err<F>(self, f: fn(E) -> F) -> Result<T, F>
        return self.map_error(f)
    end fn

    # Applies function that returns Result, flattens result
    pub fn and_then<U>(self, f: fn(T) -> Result<U, E>) -> Result<U, E>
        match self
            Result.Ok(value) -> return f(value)
            Result.Error(err) -> return Result.Error(err)
        end match
    end fn

    # Alias for and_then (monadic bind)
    pub fn flat_map<U>(self, f: fn(T) -> Result<U, E>) -> Result<U, E>
        return self.and_then(f)
    end fn

    # Returns Error if Error, otherwise returns other
    pub fn and<U>(self, other: Result<U, E>) -> Result<U, E>
        match self
            Result.Ok(_) -> return other
            Result.Error(err) -> return Result.Error(err)
        end match
    end fn

    # Returns self if Ok, otherwise returns other
    pub fn or<F>(self, other: Result<T, F>) -> Result<T, F>
        match self
            Result.Ok(value) -> return Result.Ok(value)
            Result.Error(_) -> return other
        end match
    end fn

    # Returns self if Ok, otherwise computes from closure
    pub fn or_else<F>(self, f: fn(E) -> Result<T, F>) -> Result<T, F>
        match self
            Result.Ok(value) -> return Result.Ok(value)
            Result.Error(err) -> return f(err)
        end match
    end fn

    # Inspects Ok value without consuming
    pub fn inspect(self, f: fn(T) -> void) -> Result<T, E>
        match self
            Result.Ok(value) ->
                f(value)
                return self
            Result.Error(_) -> return self
        end match
    end fn

    # Inspects Error value without consuming
    pub fn inspect_error(self, f: fn(E) -> void) -> Result<T, E>
        match self
            Result.Ok(_) -> return self
            Result.Error(err) ->
                f(err)
                return self
        end match
    end fn

    # ---------------------------------------------------------------------
    # Conversion Methods
    # ---------------------------------------------------------------------

    # Converts Result<T, E> to Option<T>, discarding error
    pub fn ok(self) -> Option<T>
        match self
            Result.Ok(value) -> return Option.Some(value)
            Result.Error(_) -> return Option.None
        end match
    end fn

    # Converts Result<T, E> to Option<E>, discarding ok value
    pub fn err(self) -> Option<E>
        match self
            Result.Ok(_) -> return Option.None
            Result.Error(err) -> return Option.Some(err)
        end match
    end fn

    # ---------------------------------------------------------------------
    # Iteration Methods
    # ---------------------------------------------------------------------

    # Returns iterator over Ok value (0 or 1 items)
    pub fn iter(self) -> ResultIterator<T, E>
        return ResultIterator { result: self, done: false }
    end fn
end impl

# =============================================================================
# Result Iterator
# =============================================================================

pub struct ResultIterator<T, E>
    result: Result<T, E>
    done: bool
end struct

pub impl<T, E> ResultIterator<T, E>
    pub fn next(mut self) -> Option<T>
        if self.done then
            return Option.None
        end if
        self.done = true
        match self.result
            Result.Ok(value) -> return Option.Some(value)
            Result.Error(_) -> return Option.None
        end match
    end fn

    pub fn has_next(self) -> bool
        return not self.done and self.result.is_ok()
    end fn
end impl

# =============================================================================
# Trait Implementations
# =============================================================================

pub impl<T: Clone, E: Clone> Clone for Result<T, E>
    fn clone(self) -> Result<T, E>
        match self
            Result.Ok(value) -> return Result.Ok(value.clone())
            Result.Error(err) -> return Result.Error(err.clone())
        end match
    end fn
end impl

pub impl<T: Default, E> Default for Result<T, E>
    # The bound belongs to the impl, where it is checked and available to the
    # method body. A method-local where clause cannot constrain an outer T.
    pub fn unwrap_or_default(self) -> T
        match self
            Result.Ok(value) -> return value
            Result.Error(_) -> return T.default()
        end match
    end fn

    fn default() -> Result<T, E>
        return Result.Ok(T.default())
    end fn
end impl

pub impl<T: Eq, E: Eq> Eq for Result<T, E>
    fn eq(self, other: Result<T, E>) -> bool
        match self
            Result.Ok(a) ->
                match other
                    Result.Ok(b) -> return a.eq(b)
                    Result.Error(_) -> return false
                end match
            Result.Error(ea) ->
                match other
                    Result.Ok(_) -> return false
                    Result.Error(eb) -> return ea.eq(eb)
                end match
        end match
    end fn

    fn ne(self, other: Result<T, E>) -> bool
        return not self.eq(other)
    end fn
end impl

pub impl<T: Debug, E: Debug> Debug for Result<T, E>
    fn debug(self) -> text
        match self
            Result.Ok(value) -> return "Ok(" + value.debug() + ")"
            Result.Error(err) -> return "Error(" + err.debug() + ")"
        end match
    end fn
end impl

pub impl<T: Display, E: Display> Display for Result<T, E>
    fn to_string(self) -> text
        match self
            Result.Ok(value) -> return value.to_string()
            Result.Error(err) -> return "Error: " + err.to_string()
        end match
    end fn
end impl

# =============================================================================
# Convenience Type Aliases
# =============================================================================

# Common Result type with text error
pub type IoResult<T> = Result<T, text>

# Unit result (no value on success)
pub type VoidResult<E> = Result<void, E>
