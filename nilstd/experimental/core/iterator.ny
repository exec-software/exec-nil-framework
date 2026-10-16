@pragma once
# =============================================================================
# Ny.Language.Core.Iterator
# =============================================================================
# Iterator traits and combinators for lazy, composable sequences.
# Provides a rich set of operations for transforming and consuming iterators.
#
# Usage:
#   let numbers = [1, 2, 3, 4, 5]
#   let doubled = numbers.iter().map(|x| x * 2).filter(|x| x > 4).collect()
# =============================================================================

module nil.std.core

import nil.std.core
import nil.std.core

# =============================================================================
# Core Iterator Trait
# =============================================================================

# The fundamental iterator trait
pub trait Iterator<T>
    # Returns the next element or None if exhausted
    fn next(mut self) -> Option<T>

    # Returns true if there are more elements
    fn has_next(self) -> bool
end trait

# =============================================================================
# Iterator Extension Methods
# =============================================================================

# Extension providing combinator methods for all iterators
pub extension<I, T> I where I: Iterator<T>
    # -------------------------------------------------------------------------
    # Transformation Combinators
    # -------------------------------------------------------------------------

    # Map each element using function f
    pub fn map<U>(self, f: fn(T) -> U) -> MapIterator<I, T, U>
        return MapIterator { inner: self, mapper: f }
    end fn

    # Filter elements that satisfy predicate
    pub fn filter(self, predicate: fn(T) -> bool) -> FilterIterator<I, T>
        return FilterIterator { inner: self, predicate: predicate }
    end fn

    # Map and flatten in one step
    pub fn flat_map<U, J>(self, f: fn(T) -> J) -> FlatMapIterator<I, T, J, U> where J: Iterator<U>
        return FlatMapIterator { outer: self, mapper: f, inner: Option.None }
    end fn

    # Filter and map in one step (filters out None)
    pub fn filter_map<U>(self, f: fn(T) -> Option<U>) -> FilterMapIterator<I, T, U>
        return FilterMapIterator { inner: self, mapper: f }
    end fn

    # Take only the first n elements
    pub fn take(self, n: i32) -> TakeIterator<I, T>
        return TakeIterator { inner: self, remaining: n }
    end fn

    # Skip the first n elements
    pub fn skip(self, n: i32) -> SkipIterator<I, T>
        return SkipIterator { inner: self, remaining: n }
    end fn

    # Take while predicate is true
    pub fn take_while(self, predicate: fn(T) -> bool) -> TakeWhileIterator<I, T>
        return TakeWhileIterator { inner: self, predicate: predicate, done: false }
    end fn

    # Skip while predicate is true
    pub fn skip_while(self, predicate: fn(T) -> bool) -> SkipWhileIterator<I, T>
        return SkipWhileIterator { inner: self, predicate: predicate, skipping: true }
    end fn

    # Chain two iterators together
    pub fn chain<J>(self, other: J) -> ChainIterator<I, J, T> where J: Iterator<T>
        return ChainIterator { first: self, second: other, on_second: false }
    end fn

    # Zip two iterators into pairs
    pub fn zip<J, U>(self, other: J) -> ZipIterator<I, J, T, U> where J: Iterator<U>
        return ZipIterator { first: self, second: other }
    end fn

    # Add index to each element
    pub fn enumerate(self) -> EnumerateIterator<I, T>
        return EnumerateIterator { inner: self, index: 0 }
    end fn

    # Peek at next element without consuming
    pub fn peekable(self) -> PeekableIterator<I, T>
        return PeekableIterator { inner: self, peeked: Option.None }
    end fn

    # Interleave with separator between elements
    pub fn intersperse(self, separator: T) -> IntersperseIterator<I, T> where T: Clone
        return IntersperseIterator { inner: self, separator: separator, needs_sep: false, done: false }
    end fn

    # Call function for each element (for side effects)
    pub fn inspect(self, f: fn(T) -> void) -> InspectIterator<I, T>
        return InspectIterator { inner: self, inspector: f }
    end fn

    # Step by n (take every nth element)
    pub fn step_by(self, step: i32) -> StepByIterator<I, T>
        return StepByIterator { inner: self, step: step, first: true }
    end fn

    # -------------------------------------------------------------------------
    # Consuming Operations
    # -------------------------------------------------------------------------

    # Collect into a Vec
    pub fn collect(mut self) -> Vec<T>
        let mut result = Vec.new()
        while self.has_next()
            match self.next()
                Option.Some(value) -> result.push(value)
                Option.None -> break
            end match
        end while
        return result
    end fn

    # Reduce to single value using accumulator
    pub fn fold<A>(mut self, initial: A, f: fn(A, T) -> A) -> A
        let mut acc = initial
        while self.has_next()
            match self.next()
                Option.Some(value) -> acc = f(acc, value)
                Option.None -> break
            end match
        end while
        return acc
    end fn

    # Reduce without initial value (first element is initial)
    pub fn reduce(mut self, f: fn(T, T) -> T) -> Option<T>
        match self.next()
            Option.Some(first) ->
                return Option.Some(self.fold(first, f))
            Option.None -> return Option.None
        end match
    end fn

    # Execute function for each element
    pub fn for_each(mut self, f: fn(T) -> void) -> void
        while self.has_next()
            match self.next()
                Option.Some(value) -> f(value)
                Option.None -> break
            end match
        end while
    end fn

    # Count elements
    pub fn count(mut self) -> i32
        let mut n = 0
        while self.has_next()
            match self.next()
                Option.Some(_) -> n = n + 1
                Option.None -> break
            end match
        end while
        return n
    end fn

    # Get last element
    pub fn last(mut self) -> Option<T>
        let mut result: Option<T> = Option.None
        while self.has_next()
            result = self.next()
        end while
        return result
    end fn

    # Get nth element (0-indexed)
    pub fn nth(mut self, n: i32) -> Option<T>
        let mut i = 0
        while i < n and self.has_next()
            self.next()
            i = i + 1
        end while
        return self.next()
    end fn

    # Check if any element satisfies predicate
    pub fn any(mut self, predicate: fn(T) -> bool) -> bool
        while self.has_next()
            match self.next()
                Option.Some(value) ->
                    if predicate(value) then
                        return true
                    end if
                Option.None -> break
            end match
        end while
        return false
    end fn

    # Check if all elements satisfy predicate
    pub fn all(mut self, predicate: fn(T) -> bool) -> bool
        while self.has_next()
            match self.next()
                Option.Some(value) ->
                    if not predicate(value) then
                        return false
                    end if
                Option.None -> break
            end match
        end while
        return true
    end fn

    # Find first element satisfying predicate
    pub fn find(mut self, predicate: fn(T) -> bool) -> Option<T>
        while self.has_next()
            match self.next()
                Option.Some(value) ->
                    if predicate(value) then
                        return Option.Some(value)
                    end if
                Option.None -> break
            end match
        end while
        return Option.None
    end fn

    # Find position of first element satisfying predicate
    pub fn position(mut self, predicate: fn(T) -> bool) -> Option<i32>
        let mut i = 0
        while self.has_next()
            match self.next()
                Option.Some(value) ->
                    if predicate(value) then
                        return Option.Some(i)
                    end if
                    i = i + 1
                Option.None -> break
            end match
        end while
        return Option.None
    end fn

    # Find maximum element
    pub fn max(mut self) -> Option<T> where T: Ord
        match self.next()
            Option.Some(first) ->
                return Option.Some(self.fold(first, |a, b| if a.cmp(b).is_greater() then a else b))
            Option.None -> return Option.None
        end match
    end fn

    # Find minimum element
    pub fn min(mut self) -> Option<T> where T: Ord
        match self.next()
            Option.Some(first) ->
                return Option.Some(self.fold(first, |a, b| if a.cmp(b).is_less() then a else b))
            Option.None -> return Option.None
        end match
    end fn

    # Sum all elements
    pub fn sum(mut self) -> T where T: Add<T, Output=T> + Default
        return self.fold(T.default(), |acc, x| acc.add(x))
    end fn

    # Product of all elements
    pub fn product(mut self) -> T where T: Mul<T, Output=T> + Default
        return self.fold(T.default(), |acc, x| acc.mul(x))
    end fn
end extension

# =============================================================================
# Iterator Adapter Structs
# =============================================================================

pub struct MapIterator<I, T, U>
    inner: I
    mapper: fn(T) -> U
end struct

pub impl<I, T, U> Iterator<U> for MapIterator<I, T, U> where I: Iterator<T>
    fn next(mut self) -> Option<U>
        match self.inner.next()
            Option.Some(value) -> return Option.Some(self.mapper(value))
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct FilterIterator<I, T>
    inner: I
    predicate: fn(T) -> bool
end struct

pub impl<I, T> Iterator<T> for FilterIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        while self.inner.has_next()
            match self.inner.next()
                Option.Some(value) ->
                    if self.predicate(value) then
                        return Option.Some(value)
                    end if
                Option.None -> return Option.None
            end match
        end while
        return Option.None
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()  # Approximation - may have no matching elements
    end fn
end impl

pub struct TakeIterator<I, T>
    inner: I
    remaining: i32
end struct

pub impl<I, T> Iterator<T> for TakeIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        if self.remaining <= 0 then
            return Option.None
        end if
        self.remaining = self.remaining - 1
        return self.inner.next()
    end fn

    fn has_next(self) -> bool
        return self.remaining > 0 and self.inner.has_next()
    end fn
end impl

pub struct SkipIterator<I, T>
    inner: I
    remaining: i32
end struct

pub impl<I, T> Iterator<T> for SkipIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        while self.remaining > 0 and self.inner.has_next()
            self.inner.next()
            self.remaining = self.remaining - 1
        end while
        return self.inner.next()
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct TakeWhileIterator<I, T>
    inner: I
    predicate: fn(T) -> bool
    done: bool
end struct

pub impl<I, T> Iterator<T> for TakeWhileIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        if self.done then
            return Option.None
        end if
        match self.inner.next()
            Option.Some(value) ->
                if self.predicate(value) then
                    return Option.Some(value)
                else
                    self.done = true
                    return Option.None
                end if
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return not self.done and self.inner.has_next()
    end fn
end impl

pub struct SkipWhileIterator<I, T>
    inner: I
    predicate: fn(T) -> bool
    skipping: bool
end struct

pub impl<I, T> Iterator<T> for SkipWhileIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        while self.skipping and self.inner.has_next()
            match self.inner.next()
                Option.Some(value) ->
                    if not self.predicate(value) then
                        self.skipping = false
                        return Option.Some(value)
                    end if
                Option.None -> return Option.None
            end match
        end while
        return self.inner.next()
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct ChainIterator<I, J, T>
    first: I
    second: J
    on_second: bool
end struct

pub impl<I, J, T> Iterator<T> for ChainIterator<I, J, T> where I: Iterator<T>, J: Iterator<T>
    fn next(mut self) -> Option<T>
        if not self.on_second then
            match self.first.next()
                Option.Some(value) -> return Option.Some(value)
                Option.None -> self.on_second = true
            end match
        end if
        return self.second.next()
    end fn

    fn has_next(self) -> bool
        if not self.on_second then
            return self.first.has_next() or self.second.has_next()
        end if
        return self.second.has_next()
    end fn
end impl

pub struct ZipIterator<I, J, T, U>
    first: I
    second: J
end struct

pub struct Pair<T, U>
    pub first: T
    pub second: U
end struct

pub impl<I, J, T, U> Iterator<Pair<T, U>> for ZipIterator<I, J, T, U> where I: Iterator<T>, J: Iterator<U>
    fn next(mut self) -> Option<Pair<T, U>>
        match self.first.next()
            Option.Some(a) ->
                match self.second.next()
                    Option.Some(b) -> return Option.Some(Pair { first: a, second: b })
                    Option.None -> return Option.None
                end match
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return self.first.has_next() and self.second.has_next()
    end fn
end impl

pub struct EnumerateIterator<I, T>
    inner: I
    index: i32
end struct

pub struct Indexed<T>
    pub index: i32
    pub value: T
end struct

pub impl<I, T> Iterator<Indexed<T>> for EnumerateIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<Indexed<T>>
        match self.inner.next()
            Option.Some(value) ->
                let idx = self.index
                self.index = self.index + 1
                return Option.Some(Indexed { index: idx, value: value })
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct PeekableIterator<I, T>
    inner: I
    peeked: Option<Option<T>>
end struct

pub impl<I, T> PeekableIterator<I, T> where I: Iterator<T>
    pub fn peek(mut self) -> Option<T>
        match self.peeked
            Option.Some(cached) -> return cached
            Option.None ->
                let next_val = self.inner.next()
                self.peeked = Option.Some(next_val)
                return next_val
        end match
    end fn
end impl

pub impl<I, T> Iterator<T> for PeekableIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        match self.peeked
            Option.Some(cached) ->
                self.peeked = Option.None
                return cached
            Option.None -> return self.inner.next()
        end match
    end fn

    fn has_next(self) -> bool
        match self.peeked
            Option.Some(Option.Some(_)) -> return true
            Option.Some(Option.None) -> return false
            Option.None -> return self.inner.has_next()
        end match
    end fn
end impl

pub struct FilterMapIterator<I, T, U>
    inner: I
    mapper: fn(T) -> Option<U>
end struct

pub impl<I, T, U> Iterator<U> for FilterMapIterator<I, T, U> where I: Iterator<T>
    fn next(mut self) -> Option<U>
        while self.inner.has_next()
            match self.inner.next()
                Option.Some(value) ->
                    match self.mapper(value)
                        Option.Some(mapped) -> return Option.Some(mapped)
                        Option.None -> continue
                    end match
                Option.None -> return Option.None
            end match
        end while
        return Option.None
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct FlatMapIterator<I, T, J, U>
    outer: I
    mapper: fn(T) -> J
    inner: Option<J>
end struct

pub impl<I, T, J, U> Iterator<U> for FlatMapIterator<I, T, J, U> where I: Iterator<T>, J: Iterator<U>
    fn next(mut self) -> Option<U>
        loop
            match self.inner
                Option.Some(ref mut it) ->
                    match it.next()
                        Option.Some(value) -> return Option.Some(value)
                        Option.None -> self.inner = Option.None
                    end match
                Option.None ->
                    match self.outer.next()
                        Option.Some(value) ->
                            self.inner = Option.Some(self.mapper(value))
                        Option.None -> return Option.None
                    end match
            end match
        end loop
    end fn

    fn has_next(self) -> bool
        match self.inner
            Option.Some(it) -> return it.has_next() or self.outer.has_next()
            Option.None -> return self.outer.has_next()
        end match
    end fn
end impl

pub struct IntersperseIterator<I, T>
    inner: I
    separator: T
    needs_sep: bool
    done: bool
end struct

pub impl<I, T> Iterator<T> for IntersperseIterator<I, T> where I: Iterator<T>, T: Clone
    fn next(mut self) -> Option<T>
        if self.done then
            return Option.None
        end if

        if self.needs_sep then
            self.needs_sep = false
            return Option.Some(self.separator.clone())
        end if

        match self.inner.next()
            Option.Some(value) ->
                self.needs_sep = self.inner.has_next()
                return Option.Some(value)
            Option.None ->
                self.done = true
                return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return not self.done and (self.needs_sep or self.inner.has_next())
    end fn
end impl

pub struct InspectIterator<I, T>
    inner: I
    inspector: fn(T) -> void
end struct

pub impl<I, T> Iterator<T> for InspectIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        match self.inner.next()
            Option.Some(value) ->
                self.inspector(value)
                return Option.Some(value)
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

pub struct StepByIterator<I, T>
    inner: I
    step: i32
    first: bool
end struct

pub impl<I, T> Iterator<T> for StepByIterator<I, T> where I: Iterator<T>
    fn next(mut self) -> Option<T>
        if self.first then
            self.first = false
            return self.inner.next()
        end if

        let mut i = 0
        while i < self.step - 1 and self.inner.has_next()
            self.inner.next()
            i = i + 1
        end while
        return self.inner.next()
    end fn

    fn has_next(self) -> bool
        return self.inner.has_next()
    end fn
end impl

# =============================================================================
# Range Iterators
# =============================================================================

pub struct Range
    pub start: i32
    pub end: i32
    pub step: i32
end struct

pub impl Range
    pub fn new(start: i32, end: i32) -> Range
        return Range { start: start, end: end, step: 1 }
    end fn

    pub fn with_step(start: i32, end: i32, step: i32) -> Range
        return Range { start: start, end: end, step: step }
    end fn
end impl

pub impl Iterator<i32> for Range
    fn next(mut self) -> Option<i32>
        if self.step > 0 and self.start < self.end then
            let value = self.start
            self.start = self.start + self.step
            return Option.Some(value)
        else if self.step < 0 and self.start > self.end then
            let value = self.start
            self.start = self.start + self.step
            return Option.Some(value)
        end if
        return Option.None
    end fn

    fn has_next(self) -> bool
        if self.step > 0 then
            return self.start < self.end
        else if self.step < 0 then
            return self.start > self.end
        end if
        return false
    end fn
end impl

# Inclusive range
pub struct RangeInclusive
    pub start: i32
    pub end: i32
    pub step: i32
    done: bool
end struct

pub impl RangeInclusive
    pub fn new(start: i32, end: i32) -> RangeInclusive
        return RangeInclusive { start: start, end: end, step: 1, done: false }
    end fn
end impl

pub impl Iterator<i32> for RangeInclusive
    fn next(mut self) -> Option<i32>
        if self.done then
            return Option.None
        end if

        if self.start == self.end then
            self.done = true
            return Option.Some(self.start)
        end if

        if self.step > 0 and self.start < self.end then
            let value = self.start
            self.start = self.start + self.step
            return Option.Some(value)
        else if self.step < 0 and self.start > self.end then
            let value = self.start
            self.start = self.start + self.step
            return Option.Some(value)
        end if

        return Option.None
    end fn

    fn has_next(self) -> bool
        return not self.done
    end fn
end impl

# =============================================================================
# Convenience Functions
# =============================================================================

# Create empty iterator
pub fn empty<T>() -> EmptyIterator<T>
    return EmptyIterator {}
end fn

pub struct EmptyIterator<T> end struct

pub impl<T> Iterator<T> for EmptyIterator<T>
    fn next(mut self) -> Option<T>
        return Option.None
    end fn

    fn has_next(self) -> bool
        return false
    end fn
end impl

# Create single-element iterator
pub fn once<T>(value: T) -> OnceIterator<T>
    return OnceIterator { value: Option.Some(value) }
end fn

pub struct OnceIterator<T>
    value: Option<T>
end struct

pub impl<T> Iterator<T> for OnceIterator<T>
    fn next(mut self) -> Option<T>
        let result = self.value
        self.value = Option.None
        return result
    end fn

    fn has_next(self) -> bool
        return self.value.is_some()
    end fn
end impl

# Create repeating iterator
pub fn repeat<T>(value: T) -> RepeatIterator<T> where T: Clone
    return RepeatIterator { value: value }
end fn

pub struct RepeatIterator<T>
    value: T
end struct

pub impl<T> Iterator<T> for RepeatIterator<T> where T: Clone
    fn next(mut self) -> Option<T>
        return Option.Some(self.value.clone())
    end fn

    fn has_next(self) -> bool
        return true  # Infinite iterator
    end fn
end impl

# Create iterator from function
pub fn from_fn<T>(f: fn() -> Option<T>) -> FromFnIterator<T>
    return FromFnIterator { generator: f }
end fn

pub struct FromFnIterator<T>
    generator: fn() -> Option<T>
end struct

pub impl<T> Iterator<T> for FromFnIterator<T>
    fn next(mut self) -> Option<T>
        return self.generator()
    end fn

    fn has_next(self) -> bool
        return true  # Unknown until called
    end fn
end impl

# Create iterator that generates values using state
pub fn successors<T>(first: Option<T>, succ: fn(T) -> Option<T>) -> SuccessorsIterator<T>
    return SuccessorsIterator { next_val: first, successor: succ }
end fn

pub struct SuccessorsIterator<T>
    next_val: Option<T>
    successor: fn(T) -> Option<T>
end struct

pub impl<T> Iterator<T> for SuccessorsIterator<T>
    fn next(mut self) -> Option<T>
        match self.next_val
            Option.Some(value) ->
                self.next_val = self.successor(value)
                return Option.Some(value)
            Option.None -> return Option.None
        end match
    end fn

    fn has_next(self) -> bool
        return self.next_val.is_some()
    end fn
end impl

