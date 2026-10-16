@pragma once
# =============================================================================
# Ny.Language.Core.Exceptions
# =============================================================================
# Exception types and error handling infrastructure.
# Provides:
#   - Base Exception class
#   - Common exception types (ArgumentException, InvalidOperationException, etc.)
#   - panic() and assert() functions
# =============================================================================

module nil.std.core

import nil.std.core

# =============================================================================
# Base Exception Class
# =============================================================================

pub class Exception
    message: text
    inner_exception: Option<ref<Exception>>

    pub fn new(message: text) -> Exception
        return Exception {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub fn with_inner(message: text, inner: ref<Exception>) -> Exception
        return Exception {
            message: message,
            inner_exception: Option.Some(inner)
        }
    end fn

    pub virtual fn get_message(self) -> text
        return self.message
    end fn

    pub fn get_inner_exception(self) -> Option<ref<Exception>>
        return self.inner_exception
    end fn

    pub virtual fn to_string(self) -> text
        return "Exception: " + self.message
    end fn
end class

# =============================================================================
# Argument Exceptions
# =============================================================================

pub class ArgumentException inherits Exception
    param_name: text

    pub fn new(message: text, param_name: text) -> ArgumentException
        return ArgumentException {
            message: message,
            inner_exception: Option.None,
            param_name: param_name
        }
    end fn

    pub fn get_param_name(self) -> text
        return self.param_name
    end fn

    pub override fn to_string(self) -> text
        return "ArgumentException [" + self.param_name + "]: " + self.message
    end fn
end class

pub class ArgumentNullException inherits ArgumentException
    pub fn new(param_name: text) -> ArgumentNullException
        return ArgumentNullException {
            message: "Value cannot be null",
            inner_exception: Option.None,
            param_name: param_name
        }
    end fn

    pub override fn to_string(self) -> text
        return "ArgumentNullException: '" + self.param_name + "' cannot be null"
    end fn
end class

pub class ArgumentOutOfRangeException inherits ArgumentException
    actual_value: text

    pub fn new(param_name: text, actual_value: text, message: text) -> ArgumentOutOfRangeException
        return ArgumentOutOfRangeException {
            message: message,
            inner_exception: Option.None,
            param_name: param_name,
            actual_value: actual_value
        }
    end fn

    pub fn get_actual_value(self) -> text
        return self.actual_value
    end fn

    pub override fn to_string(self) -> text
        return "ArgumentOutOfRangeException [" + self.param_name + "=" + self.actual_value + "]: " + self.message
    end fn
end class

# =============================================================================
# State Exceptions
# =============================================================================

pub class InvalidOperationException inherits Exception
    pub fn new(message: text) -> InvalidOperationException
        return InvalidOperationException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "InvalidOperationException: " + self.message
    end fn
end class

pub class ObjectDisposedException inherits InvalidOperationException
    object_name: text

    pub fn new(object_name: text) -> ObjectDisposedException
        return ObjectDisposedException {
            message: "Cannot access disposed object",
            inner_exception: Option.None,
            object_name: object_name
        }
    end fn

    pub fn get_object_name(self) -> text
        return self.object_name
    end fn

    pub override fn to_string(self) -> text
        return "ObjectDisposedException: '" + self.object_name + "' has been disposed"
    end fn
end class

# =============================================================================
# Index/Key Exceptions
# =============================================================================

pub class IndexOutOfRangeException inherits Exception
    index: i32

    pub fn new(index: i32) -> IndexOutOfRangeException
        return IndexOutOfRangeException {
            message: "Index was outside the bounds of the array",
            inner_exception: Option.None,
            index: index
        }
    end fn

    pub fn get_index(self) -> i32
        return self.index
    end fn

    pub override fn to_string(self) -> text
        return "IndexOutOfRangeException: Index " + self.index.to_string() + " is out of range"
    end fn
end class

pub class KeyNotFoundException inherits Exception
    key: text

    pub fn new(key: text) -> KeyNotFoundException
        return KeyNotFoundException {
            message: "The given key was not present in the dictionary",
            inner_exception: Option.None,
            key: key
        }
    end fn

    pub fn get_key(self) -> text
        return self.key
    end fn

    pub override fn to_string(self) -> text
        return "KeyNotFoundException: Key '" + self.key + "' was not found"
    end fn
end class

# =============================================================================
# Format/Parse Exceptions
# =============================================================================

pub class FormatException inherits Exception
    pub fn new(message: text) -> FormatException
        return FormatException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "FormatException: " + self.message
    end fn
end class

pub class OverflowException inherits Exception
    pub fn new(message: text) -> OverflowException
        return OverflowException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "OverflowException: " + self.message
    end fn
end class

# =============================================================================
# I/O Exceptions
# =============================================================================

pub class IOException inherits Exception
    pub fn new(message: text) -> IOException
        return IOException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "IOException: " + self.message
    end fn
end class

pub class FileNotFoundException inherits IOException
    file_path: text

    pub fn new(file_path: text) -> FileNotFoundException
        return FileNotFoundException {
            message: "Could not find file",
            inner_exception: Option.None,
            file_path: file_path
        }
    end fn

    pub fn get_file_path(self) -> text
        return self.file_path
    end fn

    pub override fn to_string(self) -> text
        return "FileNotFoundException: '" + self.file_path + "'"
    end fn
end class

pub class DirectoryNotFoundException inherits IOException
    dir_path: text

    pub fn new(dir_path: text) -> DirectoryNotFoundException
        return DirectoryNotFoundException {
            message: "Could not find directory",
            inner_exception: Option.None,
            dir_path: dir_path
        }
    end fn

    pub fn get_dir_path(self) -> text
        return self.dir_path
    end fn

    pub override fn to_string(self) -> text
        return "DirectoryNotFoundException: '" + self.dir_path + "'"
    end fn
end class

pub class EndOfStreamException inherits IOException
    pub fn new() -> EndOfStreamException
        return EndOfStreamException {
            message: "Attempted to read past end of stream",
            inner_exception: Option.None
        }
    end fn
end class

# =============================================================================
# Not Supported/Implemented
# =============================================================================

pub class NotSupportedException inherits Exception
    pub fn new(message: text) -> NotSupportedException
        return NotSupportedException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub fn operation(operation: text) -> NotSupportedException
        return NotSupportedException.new("Operation '" + operation + "' is not supported")
    end fn

    pub override fn to_string(self) -> text
        return "NotSupportedException: " + self.message
    end fn
end class

pub class NotImplementedException inherits Exception
    pub fn new() -> NotImplementedException
        return NotImplementedException {
            message: "The method or operation is not implemented",
            inner_exception: Option.None
        }
    end fn

    pub fn with_message(message: text) -> NotImplementedException
        return NotImplementedException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "NotImplementedException: " + self.message
    end fn
end class

# =============================================================================
# Null Reference
# =============================================================================

pub class NullReferenceException inherits Exception
    pub fn new() -> NullReferenceException
        return NullReferenceException {
            message: "Object reference not set to an instance of an object",
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "NullReferenceException: " + self.message
    end fn
end class

# =============================================================================
# Timeout
# =============================================================================

pub class TimeoutException inherits Exception
    pub fn new() -> TimeoutException
        return TimeoutException {
            message: "The operation has timed out",
            inner_exception: Option.None
        }
    end fn

    pub fn with_message(message: text) -> TimeoutException
        return TimeoutException {
            message: message,
            inner_exception: Option.None
        }
    end fn

    pub override fn to_string(self) -> text
        return "TimeoutException: " + self.message
    end fn
end class

# =============================================================================
# Aggregate Exception (for multiple errors)
# =============================================================================

pub class AggregateException inherits Exception
    inner_exceptions: Vec<Exception>

    pub fn new(exceptions: Vec<Exception>) -> AggregateException
        return AggregateException {
            message: "One or more errors occurred",
            inner_exception: Option.None,
            inner_exceptions: exceptions
        }
    end fn

    pub fn get_inner_exceptions(self) -> Vec<Exception>
        return self.inner_exceptions
    end fn

    pub override fn to_string(self) -> text
        return "AggregateException: " + self.inner_exceptions.len().to_string() + " error(s) occurred"
    end fn
end class

# =============================================================================
# Panic and Assert Functions
# =============================================================================

# Terminate with error message
@intrinsic("panic")
pub fn panic(message: text) -> never
end fn

# Assert condition, panic if false
pub fn assert(condition: bool, message: text)
    if not condition then
        panic("Assertion failed: " + message)
    end if
end fn

# Assert condition with default message
pub fn assert_true(condition: bool)
    if not condition then
        panic("Assertion failed: expected true")
    end if
end fn

# Assert value is not null
pub fn assert_not_null<T>(value: ptr<T>, name: text)
    if value == null then
        panic("Assertion failed: '" + name + "' is null")
    end if
end fn

# Assert two values are equal
pub fn assert_eq<T>(actual: T, expected: T, message: text) where T: Eq + Debug
    if not actual.eq(expected) then
        panic("Assertion failed: " + message + " - expected " + expected.debug() + ", got " + actual.debug())
    end if
end fn

# Assert two values are not equal
pub fn assert_ne<T>(actual: T, expected: T, message: text) where T: Eq + Debug
    if actual.eq(expected) then
        panic("Assertion failed: " + message + " - values should not be equal: " + actual.debug())
    end if
end fn

# Debug assert (only in debug builds)
@if cfg(debug)
pub fn debug_assert(condition: bool, message: text)
    if not condition then
        panic("Debug assertion failed: " + message)
    end if
end fn
@else
pub fn debug_assert(condition: bool, message: text)
    # No-op in release builds
end fn
@endif

# Unreachable code marker
pub fn unreachable() -> never
    panic("Entered unreachable code")
end fn

# Todo marker (panics with message)
pub fn todo(message: text) -> never
    panic("Not yet implemented: " + message)
end fn

# Unimplemented marker
pub fn unimplemented() -> never
    panic("Not implemented")
end fn

