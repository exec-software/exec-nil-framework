@pragma once
# =============================================================================
# Ny.Language.Core.Memory
# =============================================================================
# Memory management types including Span, Memory, and disposal patterns.
# Provides safe abstractions over raw memory operations.
#
# Types:
#   - Span<T>: Contiguous region of memory (stack or heap)
#   - Memory<T>: Heap-allocated memory with lifetime management
#   - Box<T>: Owned heap allocation (smart pointer)
#   - Rc<T>: Reference counted pointer
#   - Arc<T>: Atomically reference counted pointer (thread-safe)
#   - Weak<T>: Weak reference (doesn't prevent deallocation)
# =============================================================================

module nil.std.core

import nil.std.core
import nil.std.core

# =============================================================================
# Span<T> - Contiguous Memory View
# =============================================================================

# A view into a contiguous region of memory
# Does not own the memory - borrowed reference
pub struct Span<T>
    data: ptr<T>
    length: i32
end struct

pub impl<T> Span<T>
    # Create an empty span
    pub fn empty() -> Span<T>
        return Span { data: null, length: 0 }
    end fn

    # Create span from pointer and length
    pub fn from_raw(ptr: ptr<T>, len: i32) -> Span<T>
        return Span { data: ptr, length: len }
    end fn

    # Get length
    pub fn len(self) -> i32
        return self.length
    end fn

    # Check if empty
    pub fn is_empty(self) -> bool
        return self.length == 0
    end fn

    # Get element at index
    pub fn get(self, index: i32) -> Option<T>
        if index < 0 or index >= self.length then
            return Option.None
        end if
        return Option.Some(self.data[index])
    end fn

    # Get element at index (unchecked)
    pub fn get_unchecked(self, index: i32) -> T
        return self.data[index]
    end fn

    # Set element at index
    pub fn set(mut self, index: i32, value: T) -> bool
        if index < 0 or index >= self.length then
            return false
        end if
        self.data[index] = value
        return true
    end fn

    # Get first element
    pub fn first(self) -> Option<T>
        if self.length == 0 then
            return Option.None
        end if
        return Option.Some(self.data[0])
    end fn

    # Get last element
    pub fn last(self) -> Option<T>
        if self.length == 0 then
            return Option.None
        end if
        return Option.Some(self.data[self.length - 1])
    end fn

    # Create a sub-span (slice)
    pub fn slice(self, start: i32, len: i32) -> Span<T>
        if start < 0 or start >= self.length then
            return Span.empty()
        end if
        let actual_len = if start + len > self.length then self.length - start else len
        return Span { data: self.data + start, length: actual_len }
    end fn

    # Slice from start to end
    pub fn slice_from(self, start: i32) -> Span<T>
        return self.slice(start, self.length - start)
    end fn

    # Slice from beginning to index
    pub fn slice_to(self, end: i32) -> Span<T>
        return self.slice(0, end)
    end fn

    # Split at index
    pub fn split_at(self, mid: i32) -> Tuple<Span<T>, Span<T>>
        return Tuple {
            first: self.slice_to(mid),
            second: self.slice_from(mid)
        }
    end fn

    # Get iterator
    pub fn iter(self) -> SpanIterator<T>
        return SpanIterator { span: self, index: 0 }
    end fn

    # Copy contents to another span
    pub fn copy_to(self, dest: Span<T>) -> i32
        let count = if self.length < dest.length then self.length else dest.length
        let mut i = 0
        while i < count
            dest.data[i] = self.data[i]
            i = i + 1
        end while
        return count
    end fn

    # Fill with value
    pub fn fill(mut self, value: T) where T: Clone
        let mut i = 0
        while i < self.length
            self.data[i] = value.clone()
            i = i + 1
        end while
    end fn

    # Reverse in place
    pub fn reverse(mut self)
        let mut i = 0
        let mut j = self.length - 1
        while i < j
            let temp = self.data[i]
            self.data[i] = self.data[j]
            self.data[j] = temp
            i = i + 1
            j = j - 1
        end while
    end fn

    # Check if contains value
    pub fn contains(self, value: T) -> bool where T: Eq
        let mut i = 0
        while i < self.length
            if self.data[i].eq(value) then
                return true
            end if
            i = i + 1
        end while
        return false
    end fn

    # Find index of value
    pub fn index_of(self, value: T) -> Option<i32> where T: Eq
        let mut i = 0
        while i < self.length
            if self.data[i].eq(value) then
                return Option.Some(i)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    # Binary search (span must be sorted)
    pub fn binary_search(self, value: T) -> Option<i32> where T: Ord
        let mut low = 0
        let mut high = self.length - 1

        while low <= high
            let mid = low + (high - low) / 2
            match self.data[mid].cmp(value)
                Ordering.Equal -> return Option.Some(mid)
                Ordering.Less -> low = mid + 1
                Ordering.Greater -> high = mid - 1
            end match
        end while

        return Option.None
    end fn
end impl

# Span iterator
pub struct SpanIterator<T>
    span: Span<T>
    index: i32
end struct

pub impl<T> Iterator<T> for SpanIterator<T>
    fn next(mut self) -> Option<T>
        if self.index >= self.span.length then
            return Option.None
        end if
        let value = self.span.data[self.index]
        self.index = self.index + 1
        return Option.Some(value)
    end fn

    fn has_next(self) -> bool
        return self.index < self.span.length
    end fn
end impl

# =============================================================================
# Box<T> - Owned Heap Allocation
# =============================================================================

# A heap-allocated value with single ownership
pub struct Box<T>
    ptr: ptr<T>
end struct

pub impl<T> Box<T>
    # Allocate and initialize with value
    pub fn new(value: T) -> Box<T>
        let p: ptr<T> = allocate<T>(1)
        p[0] = value
        return Box { ptr: p }
    end fn

    # Get reference to inner value
    pub fn get(self) -> ref<T>
        return self.ptr[0]
    end fn

    # Get mutable reference to inner value
    pub fn get_mut(mut self) -> ref<T>
        return self.ptr[0]
    end fn

    # Take ownership of inner value, deallocating box
    pub fn into_inner(mut self) -> T
        let value = self.ptr[0]
        deallocate(self.ptr)
        self.ptr = null
        return value
    end fn

    # Get raw pointer (unsafe)
    pub fn as_ptr(self) -> ptr<T>
        return self.ptr
    end fn
end impl

pub impl<T> Deref for Box<T>
    type Target = T
    fn deref(self) -> ref<T>
        return self.ptr[0]
    end fn
end impl

pub impl<T> DerefMut for Box<T>
    fn deref_mut(mut self) -> ref<T>
        return self.ptr[0]
    end fn
end impl

pub impl<T> Drop for Box<T>
    fn drop(mut self)
        if self.ptr != null then
            deallocate(self.ptr)
            self.ptr = null
        end if
    end fn
end impl

pub impl<T: Clone> Clone for Box<T>
    fn clone(self) -> Box<T>
        return Box.new(self.get().clone())
    end fn
end impl

# =============================================================================
# Rc<T> - Reference Counted Pointer
# =============================================================================

# A reference-counted pointer (single-threaded)
pub struct Rc<T>
    ptr: ptr<RcInner<T>>
end struct

struct RcInner<T>
    value: T
    strong_count: i32
    weak_count: i32
end struct

pub impl<T> Rc<T>
    # Create new Rc with value
    pub fn new(value: T) -> Rc<T>
        let inner: ptr<RcInner<T>> = allocate<RcInner<T>>(1)
        inner[0] = RcInner { value: value, strong_count: 1, weak_count: 0 }
        return Rc { ptr: inner }
    end fn

    # Get reference to inner value
    pub fn get(self) -> ref<T>
        return self.ptr[0].value
    end fn

    # Get strong reference count
    pub fn strong_count(self) -> i32
        return self.ptr[0].strong_count
    end fn

    # Get weak reference count
    pub fn weak_count(self) -> i32
        return self.ptr[0].weak_count
    end fn

    # Clone (increments reference count)
    pub fn clone(mut self) -> Rc<T>
        self.ptr[0].strong_count = self.ptr[0].strong_count + 1
        return Rc { ptr: self.ptr }
    end fn

    # Create a weak reference
    pub fn downgrade(self) -> Weak<T>
        self.ptr[0].weak_count = self.ptr[0].weak_count + 1
        return Weak { ptr: self.ptr }
    end fn

    # Try to get mutable access (only if single owner)
    pub fn get_mut(mut self) -> Option<ref<T>>
        if self.ptr[0].strong_count == 1 and self.ptr[0].weak_count == 0 then
            return Option.Some(self.ptr[0].value)
        end if
        return Option.None
    end fn

    # Try to unwrap (only if single owner)
    pub fn try_unwrap(mut self) -> Option<T>
        if self.ptr[0].strong_count == 1 then
            let value = self.ptr[0].value
            self.ptr[0].strong_count = 0
            if self.ptr[0].weak_count == 0 then
                deallocate(self.ptr)
            end if
            self.ptr = null
            return Option.Some(value)
        end if
        return Option.None
    end fn
end impl

pub impl<T> Deref for Rc<T>
    type Target = T
    fn deref(self) -> ref<T>
        return self.ptr[0].value
    end fn
end impl

pub impl<T> Drop for Rc<T>
    fn drop(mut self)
        if self.ptr != null then
            self.ptr[0].strong_count = self.ptr[0].strong_count - 1
            if self.ptr[0].strong_count == 0 then
                # Call destructor on value if it has one
                if self.ptr[0].weak_count == 0 then
                    deallocate(self.ptr)
                end if
            end if
            self.ptr = null
        end if
    end fn
end impl

# =============================================================================
# Weak<T> - Weak Reference
# =============================================================================

# A weak reference that doesn't prevent deallocation
pub struct Weak<T>
    ptr: ptr<RcInner<T>>
end struct

pub impl<T> Weak<T>
    # Create an empty weak reference
    pub fn new() -> Weak<T>
        return Weak { ptr: null }
    end fn

    # Try to upgrade to strong reference
    pub fn upgrade(self) -> Option<Rc<T>>
        if self.ptr == null then
            return Option.None
        end if
        if self.ptr[0].strong_count == 0 then
            return Option.None
        end if
        self.ptr[0].strong_count = self.ptr[0].strong_count + 1
        return Option.Some(Rc { ptr: self.ptr })
    end fn

    # Get strong count (0 if deallocated)
    pub fn strong_count(self) -> i32
        if self.ptr == null then
            return 0
        end if
        return self.ptr[0].strong_count
    end fn

    # Get weak count
    pub fn weak_count(self) -> i32
        if self.ptr == null then
            return 0
        end if
        return self.ptr[0].weak_count
    end fn
end impl

pub impl<T> Clone for Weak<T>
    fn clone(mut self) -> Weak<T>
        if self.ptr != null then
            self.ptr[0].weak_count = self.ptr[0].weak_count + 1
        end if
        return Weak { ptr: self.ptr }
    end fn
end impl

pub impl<T> Drop for Weak<T>
    fn drop(mut self)
        if self.ptr != null then
            self.ptr[0].weak_count = self.ptr[0].weak_count - 1
            if self.ptr[0].strong_count == 0 and self.ptr[0].weak_count == 0 then
                deallocate(self.ptr)
            end if
            self.ptr = null
        end if
    end fn
end impl

# =============================================================================
# IDisposable - Cleanup Pattern
# =============================================================================

# Types that hold resources that need cleanup
pub trait IDisposable
    # Release resources
    fn dispose(mut self) -> void

    # Check if already disposed
    fn is_disposed(self) -> bool
end trait

# =============================================================================
# Using Guard - RAII Pattern
# =============================================================================

# Automatically disposes when dropped
pub struct Using<T> where T: IDisposable
    resource: T
end struct

pub impl<T> Using<T> where T: IDisposable
    pub fn new(resource: T) -> Using<T>
        return Using { resource: resource }
    end fn

    pub fn get(self) -> ref<T>
        return self.resource
    end fn

    pub fn get_mut(mut self) -> ref<T>
        return self.resource
    end fn
end impl

pub impl<T> Drop for Using<T> where T: IDisposable
    fn drop(mut self)
        if not self.resource.is_disposed() then
            self.resource.dispose()
        end if
    end fn
end impl

# =============================================================================
# Memory Allocation Functions
# =============================================================================

# Allocate memory for n elements of type T
@intrinsic("memory.allocate")
pub fn allocate<T>(count: i32) -> ptr<T>

# Deallocate memory
@intrinsic("memory.deallocate")
pub fn deallocate<T>(ptr: ptr<T>) -> void

# Reallocate to new size
@intrinsic("memory.reallocate")
pub fn reallocate<T>(ptr: ptr<T>, new_count: i32) -> ptr<T>

# Zero-initialize memory
@intrinsic("memory.zero")
pub fn zero<T>(ptr: ptr<T>, count: i32) -> void

# Copy memory
@intrinsic("memory.copy")
pub fn copy<T>(dest: ptr<T>, src: ptr<T>, count: i32) -> void

# Move memory (handles overlapping)
@intrinsic("memory.move")
pub fn move<T>(dest: ptr<T>, src: ptr<T>, count: i32) -> void

# Compare memory
@intrinsic("memory.compare")
pub fn compare<T>(a: ptr<T>, b: ptr<T>, count: i32) -> i32

# =============================================================================
# Nullable<T> - Explicit Nullable Type
# =============================================================================

# Wrapper for values that might be null
pub struct Nullable<T>
    has_value: bool
    value: T
end struct

pub impl<T> Nullable<T>
    pub fn null() -> Nullable<T>
        return Nullable { has_value: false, value: undefined }
    end fn

    pub fn some(value: T) -> Nullable<T>
        return Nullable { has_value: true, value: value }
    end fn

    pub fn has_value(self) -> bool
        return self.has_value
    end fn

    pub fn get_value(self) -> T
        if not self.has_value then
            panic("Attempted to get value from null Nullable")
        end if
        return self.value
    end fn

    pub fn get_value_or_default(self, default: T) -> T
        if self.has_value then
            return self.value
        end if
        return default
    end fn

    pub fn to_option(self) -> Option<T>
        if self.has_value then
            return Option.Some(self.value)
        end if
        return Option.None
    end fn
end impl

# =============================================================================
# Helper Types
# =============================================================================

pub struct Tuple<T1, T2>
    pub first: T1
    pub second: T2
end struct

