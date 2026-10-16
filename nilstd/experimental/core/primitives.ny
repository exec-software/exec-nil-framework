@pragma once
# =============================================================================
# Ny.Language.Core.Primitives
# =============================================================================
# Primitive type definitions, wrappers, and extensions.
# Provides:
#   - Numeric type aliases (i8, i16, i32, i64, u8, u16, u32, u64, f32, f64)
#   - Boolean type
#   - Character and text types
#   - Pointer and reference types
#   - Type conversion traits
# =============================================================================

module nil.std.core

# =============================================================================
# Numeric Types
# =============================================================================

# Signed integers
pub type i8   # 8-bit signed integer (-128 to 127)
pub type i16  # 16-bit signed integer
pub type i32  # 32-bit signed integer (default integer)
pub type i64  # 64-bit signed integer

# Unsigned integers
pub type u8   # 8-bit unsigned integer (0 to 255, byte)
pub type u16  # 16-bit unsigned integer
pub type u32  # 32-bit unsigned integer
pub type u64  # 64-bit unsigned integer

# Size types (platform-dependent)
pub type isize  # Signed pointer-sized integer
pub type usize  # Unsigned pointer-sized integer

# Floating point
pub type f32  # 32-bit IEEE 754 float
pub type f64  # 64-bit IEEE 754 double (default float)

# Boolean
pub type bool  # true or false

# Character (Unicode scalar value)
pub type char  # 32-bit Unicode character

# Text (UTF-8 string)
pub type text  # Immutable UTF-8 string

# =============================================================================
# Pointer and Reference Types
# =============================================================================

# Raw pointer (unsafe)
pub type ptr<T>  # Raw pointer to T

# Reference (safe, non-null)
pub type ref<T>  # Reference to T

# =============================================================================
# Byte Type Alias
# =============================================================================
pub type byte = u8

# =============================================================================
# Numeric Limits
# =============================================================================

pub struct I8Limits end struct
pub impl I8Limits
    pub const MIN: i8 = -128
    pub const MAX: i8 = 127
end impl

pub struct I16Limits end struct
pub impl I16Limits
    pub const MIN: i16 = -32768
    pub const MAX: i16 = 32767
end impl

pub struct I32Limits end struct
pub impl I32Limits
    pub const MIN: i32 = -2147483648
    pub const MAX: i32 = 2147483647
end impl

pub struct I64Limits end struct
pub impl I64Limits
    pub const MIN: i64 = -9223372036854775808
    pub const MAX: i64 = 9223372036854775807
end impl

pub struct U8Limits end struct
pub impl U8Limits
    pub const MIN: u8 = 0
    pub const MAX: u8 = 255
end impl

pub struct U16Limits end struct
pub impl U16Limits
    pub const MIN: u16 = 0
    pub const MAX: u16 = 65535
end impl

pub struct U32Limits end struct
pub impl U32Limits
    pub const MIN: u32 = 0
    pub const MAX: u32 = 4294967295
end impl

pub struct U64Limits end struct
pub impl U64Limits
    pub const MIN: u64 = 0
    pub const MAX: u64 = 18446744073709551615
end impl

pub struct F32Limits end struct
pub impl F32Limits
    pub const MIN: f32 = -3.40282347e+38
    pub const MAX: f32 = 3.40282347e+38
    pub const EPSILON: f32 = 1.19209290e-07
    pub const NAN: f32 = 0.0 / 0.0
    pub const INFINITY: f32 = 1.0 / 0.0
    pub const NEG_INFINITY: f32 = -1.0 / 0.0
end impl

pub struct F64Limits end struct
pub impl F64Limits
    pub const MIN: f64 = -1.7976931348623157e+308
    pub const MAX: f64 = 1.7976931348623157e+308
    pub const EPSILON: f64 = 2.2204460492503131e-16
    pub const NAN: f64 = 0.0 / 0.0
    pub const INFINITY: f64 = 1.0 / 0.0
    pub const NEG_INFINITY: f64 = -1.0 / 0.0
end impl

# =============================================================================
# Conversion Traits
# =============================================================================

pub trait Into<T>
    fn into(self) -> T
end trait

pub trait From<T>
    fn from(value: T) -> Self
end trait

pub trait TryInto<T>
    fn try_into(self) -> Option<T>
end trait

pub trait TryFrom<T>
    fn try_from(value: T) -> Option<Self>
end trait

# =============================================================================
# Numeric Extension Methods
# =============================================================================

extension i32
    # Convert to text representation
    pub fn to_string(self) -> text
    end fn

    # Absolute value
    pub fn abs(self) -> i32
        if self < 0 then
            return -self
        end if
        return self
    end fn

    # Clamp value between min and max
    pub fn clamp(self, min: i32, max: i32) -> i32
        if self < min then return min end if
        if self > max then return max end if
        return self
    end fn

    # Check if value is in range [min, max]
    pub fn in_range(self, min: i32, max: i32) -> bool
        return self >= min and self <= max
    end fn

    # Sign of value: -1, 0, or 1
    pub fn sign(self) -> i32
        if self < 0 then return -1 end if
        if self > 0 then return 1 end if
        return 0
    end fn
end extension

extension i64
    pub fn to_string(self) -> text
    end fn

    pub fn abs(self) -> i64
        if self < 0 then
            return -self
        end if
        return self
    end fn

    pub fn clamp(self, min: i64, max: i64) -> i64
        if self < min then return min end if
        if self > max then return max end if
        return self
    end fn
end extension

extension f64
    pub fn to_string(self) -> text
    end fn

    pub fn abs(self) -> f64
    end fn

    pub fn floor(self) -> f64
    end fn

    pub fn ceil(self) -> f64
    end fn

    pub fn round(self) -> f64
    end fn

    pub fn truncate(self) -> f64
    end fn

    pub fn clamp(self, min: f64, max: f64) -> f64
        if self < min then return min end if
        if self > max then return max end if
        return self
    end fn

    pub fn is_nan(self) -> bool
    end fn

    pub fn is_infinite(self) -> bool
    end fn

    pub fn is_finite(self) -> bool
        return not self.is_nan() and not self.is_infinite()
    end fn
end extension

extension bool
    pub fn to_string(self) -> text
        if self then
            return "true"
        end if
        return "false"
    end fn

    pub fn to_i32(self) -> i32
        if self then return 1 end if
        return 0
    end fn
end extension

# =============================================================================
# Text Extension Methods
# =============================================================================

extension text
    # Get length in bytes
    pub fn len(self) -> i32
    end fn

    # Check if empty
    pub fn is_empty(self) -> bool
        return self.len() == 0
    end fn

    # Check if not empty
    pub fn is_not_empty(self) -> bool
        return self.len() > 0
    end fn

    # Get substring
    pub fn substr(self, start: i32, length: i32) -> text
    end fn

    # Get character at index
    pub fn char_at(self, index: i32) -> i32
    end fn

    # Check if starts with prefix
    pub fn starts_with(self, prefix: text) -> bool
    end fn

    # Check if ends with suffix
    pub fn ends_with(self, suffix: text) -> bool
    end fn

    # Find index of substring (-1 if not found)
    pub fn index_of(self, substr: text) -> i32
    end fn

    # Check if contains substring
    pub fn contains(self, substr: text) -> bool
        return self.index_of(substr) >= 0
    end fn

    # Convert to uppercase
    pub fn to_upper(self) -> text
    end fn

    # Convert to lowercase
    pub fn to_lower(self) -> text
    end fn

    # Trim whitespace from both ends
    pub fn trim(self) -> text
    end fn

    # Trim whitespace from start
    pub fn trim_start(self) -> text
    end fn

    # Trim whitespace from end
    pub fn trim_end(self) -> text
    end fn

    # Replace all occurrences
    pub fn replace(self, from: text, to: text) -> text
    end fn

    # Repeat string n times
    pub fn repeat(self, count: i32) -> text
        let mut result: text = ""
        let mut i = 0
        while i < count
            result = result + self
            i = i + 1
        end while
        return result
    end fn

    # Pad left to specified width
    pub fn pad_left(self, width: i32, pad_char: text) -> text
        let len = self.len()
        if len >= width then return self end if
        return pad_char.repeat(width - len) + self
    end fn

    # Pad right to specified width
    pub fn pad_right(self, width: i32, pad_char: text) -> text
        let len = self.len()
        if len >= width then return self end if
        return self + pad_char.repeat(width - len)
    end fn
end extension

