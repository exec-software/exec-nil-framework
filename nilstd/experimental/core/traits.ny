@pragma once
# =============================================================================
# Ny.Language.Core.Traits
# =============================================================================
# Core traits that define fundamental behaviors for types.
# These traits form the basis of Ny's polymorphism and type system.
#
# Traits:
#   - Clone: Create a copy of a value
#   - Display: Convert to human-readable text
#   - Debug: Convert to debug representation
#   - Default: Provide a default value
#   - Eq: Equality comparison
#   - Ord: Ordering comparison
#   - Hash: Compute hash values
#   - Drop: Cleanup when value goes out of scope
# =============================================================================

module nil.std.core

# =============================================================================
# Clone - Create Copies
# =============================================================================

# Types that can be duplicated
pub trait Clone
    # Create a clone (deep copy) of this value
    fn clone(self) -> Self
end trait

# =============================================================================
# Display & Debug - String Representation
# =============================================================================

# Types that can be displayed to users
pub trait Display
    # Convert to human-readable text representation
    fn to_string(self) -> text
end trait

# Types that can produce debug output
pub trait Debug
    # Convert to debug representation (for developers)
    fn debug(self) -> text
end trait

# =============================================================================
# Default - Default Values
# =============================================================================

# Types that have a default value
pub trait Default
    # Returns the default value for this type
    fn default() -> Self
end trait

# =============================================================================
# Eq - Equality Comparison
# =============================================================================

# Types that can be compared for equality
pub trait Eq
    # Check if two values are equal
    fn eq(self, other: Self) -> bool

    # Check if two values are not equal (default implementation)
    fn ne(self, other: Self) -> bool
        return not self.eq(other)
    end fn
end trait

# =============================================================================
# Ord - Ordering Comparison
# =============================================================================

# Ordering result enumeration
pub enum Ordering
    Less
    Equal
    Greater
end enum

pub impl Ordering
    pub fn is_less(self) -> bool
        match self
            Ordering.Less -> return true
            _ -> return false
        end match
    end fn

    pub fn is_equal(self) -> bool
        match self
            Ordering.Equal -> return true
            _ -> return false
        end match
    end fn

    pub fn is_greater(self) -> bool
        match self
            Ordering.Greater -> return true
            _ -> return false
        end match
    end fn

    pub fn reverse(self) -> Ordering
        match self
            Ordering.Less -> return Ordering.Greater
            Ordering.Equal -> return Ordering.Equal
            Ordering.Greater -> return Ordering.Less
        end match
    end fn

    pub fn then(self, other: Ordering) -> Ordering
        match self
            Ordering.Equal -> return other
            _ -> return self
        end match
    end fn
end impl

# Types that have a total ordering
pub trait Ord where Self: Eq
    # Compare two values, returns Ordering
    fn cmp(self, other: Self) -> Ordering

    # Less than comparison
    fn lt(self, other: Self) -> bool
        return self.cmp(other).is_less()
    end fn

    # Less than or equal comparison
    fn le(self, other: Self) -> bool
        return not self.cmp(other).is_greater()
    end fn

    # Greater than comparison
    fn gt(self, other: Self) -> bool
        return self.cmp(other).is_greater()
    end fn

    # Greater than or equal comparison
    fn ge(self, other: Self) -> bool
        return not self.cmp(other).is_less()
    end fn

    # Returns the maximum of self and other
    fn max(self, other: Self) -> Self
        if self.cmp(other).is_greater() then
            return self
        end if
        return other
    end fn

    # Returns the minimum of self and other
    fn min(self, other: Self) -> Self
        if self.cmp(other).is_less() then
            return self
        end if
        return other
    end fn

    # Clamps self between min and max
    fn clamp(self, min: Self, max: Self) -> Self
        if self.cmp(min).is_less() then
            return min
        end if
        if self.cmp(max).is_greater() then
            return max
        end if
        return self
    end fn
end trait

# Partial ordering for types that may not have total ordering (e.g., floats)
pub trait PartialOrd where Self: Eq
    # Compare two values, returns Option<Ordering> (None if incomparable)
    fn partial_cmp(self, other: Self) -> Option<Ordering>
end trait

# =============================================================================
# Hash - Hashing
# =============================================================================

# Types that can be hashed
pub trait Hash
    # Compute a hash value for this instance
    fn hash(self) -> i64
end trait

# Hasher interface for custom hashing algorithms
pub trait Hasher
    # Add bytes to the hash state
    fn write(mut self, bytes: ptr<u8>, len: i32) -> void

    # Add a single byte
    fn write_byte(mut self, byte: u8) -> void

    # Add an i32
    fn write_i32(mut self, value: i32) -> void

    # Add an i64
    fn write_i64(mut self, value: i64) -> void

    # Finalize and return hash value
    fn finish(self) -> i64
end trait

# =============================================================================
# Drop - Cleanup
# =============================================================================

# Types that need cleanup when they go out of scope
pub trait Drop
    # Called when value is dropped
    fn drop(mut self) -> void
end trait

# =============================================================================
# Copy - Implicit Copying
# =============================================================================

# Types that can be copied implicitly (no ownership transfer)
# This is a marker trait - types implementing Copy are always cloned
pub trait Copy where Self: Clone
end trait

# =============================================================================
# Sized - Known Size
# =============================================================================

# Marker trait for types with known size at compile time
# Most types are Sized by default
pub trait Sized
end trait

# =============================================================================
# Send & Sync - Thread Safety
# =============================================================================

# Types that can be transferred across thread boundaries
pub trait Send
end trait

# Types that can be shared across thread boundaries (via reference)
pub trait Sync
end trait

# =============================================================================
# AsRef & AsMut - Reference Conversions
# =============================================================================

# Types that can be borrowed as a reference to T
pub trait AsRef<T>
    fn as_ref(self) -> ref<T>
end trait

# Types that can be borrowed as a mutable reference to T
pub trait AsMut<T>
    fn as_mut(mut self) -> ref<T>
end trait

# =============================================================================
# Borrow & BorrowMut
# =============================================================================

# Similar to AsRef but with stricter semantics for Eq/Ord/Hash
pub trait Borrow<T>
    fn borrow(self) -> ref<T>
end trait

pub trait BorrowMut<T> where Self: Borrow<T>
    fn borrow_mut(mut self) -> ref<T>
end trait

# =============================================================================
# ToOwned - Create Owned Version
# =============================================================================

# Types that can create an owned version from a borrowed reference
pub trait ToOwned
    type Owned
    fn to_owned(self) -> Owned
end trait

# =============================================================================
# Deref & DerefMut - Smart Pointer Dereferencing
# =============================================================================

# Types that can be dereferenced
pub trait Deref
    type Target
    fn deref(self) -> ref<Target>
end trait

# Types that can be mutably dereferenced
pub trait DerefMut where Self: Deref
    fn deref_mut(mut self) -> ref<Target>
end trait

# =============================================================================
# Index & IndexMut - Indexing Operations
# =============================================================================

# Types that support indexing with []
pub trait Index<Idx>
    type Output
    fn index(self, idx: Idx) -> ref<Output>
end trait

# Types that support mutable indexing
pub trait IndexMut<Idx> where Self: Index<Idx>
    fn index_mut(mut self, idx: Idx) -> ref<Output>
end trait

# =============================================================================
# Add, Sub, Mul, Div - Arithmetic Operators
# =============================================================================

pub trait Add<Rhs = Self>
    type Output
    fn add(self, rhs: Rhs) -> Output
end trait

pub trait Sub<Rhs = Self>
    type Output
    fn sub(self, rhs: Rhs) -> Output
end trait

pub trait Mul<Rhs = Self>
    type Output
    fn mul(self, rhs: Rhs) -> Output
end trait

pub trait Div<Rhs = Self>
    type Output
    fn div(self, rhs: Rhs) -> Output
end trait

pub trait Rem<Rhs = Self>
    type Output
    fn rem(self, rhs: Rhs) -> Output
end trait

pub trait Neg
    type Output
    fn neg(self) -> Output
end trait

# =============================================================================
# BitAnd, BitOr, BitXor, Shl, Shr - Bitwise Operators
# =============================================================================

pub trait BitAnd<Rhs = Self>
    type Output
    fn bitand(self, rhs: Rhs) -> Output
end trait

pub trait BitOr<Rhs = Self>
    type Output
    fn bitor(self, rhs: Rhs) -> Output
end trait

pub trait BitXor<Rhs = Self>
    type Output
    fn bitxor(self, rhs: Rhs) -> Output
end trait

pub trait Not
    type Output
    fn not(self) -> Output
end trait

pub trait Shl<Rhs = i32>
    type Output
    fn shl(self, rhs: Rhs) -> Output
end trait

pub trait Shr<Rhs = i32>
    type Output
    fn shr(self, rhs: Rhs) -> Output
end trait

