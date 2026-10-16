@pragma once
# =============================================================================
# Ny.Language.Core.Functional
# =============================================================================
# Functional programming utilities including function composition,
# partial application, and common higher-order functions.
# =============================================================================

module nil.std.core

import nil.std.core
import nil.std.core
import nil.std.core

# =============================================================================
# Function Type Aliases
# =============================================================================

# Common function types
pub type Predicate<T> = fn(T) -> bool
pub type Consumer<T> = fn(T) -> void
pub type Supplier<T> = fn() -> T
pub type UnaryOperator<T> = fn(T) -> T
pub type BinaryOperator<T> = fn(T, T) -> T
pub type Function<T, R> = fn(T) -> R
pub type BiFunction<T1, T2, R> = fn(T1, T2) -> R

# =============================================================================
# Identity and Constant Functions
# =============================================================================

# Returns its argument unchanged
pub fn identity<T>(x: T) -> T
    return x
end fn

# Returns a function that always returns the same value
pub fn constant<T, U>(value: T) -> fn(U) -> T where T: Clone
    return |_| value.clone()
end fn

# Returns a function that ignores its argument and returns unit
pub fn ignore<T>() -> fn(T) -> void
    return |_| { }
end fn

# =============================================================================
# Function Composition
# =============================================================================

# Compose two functions: (f ∘ g)(x) = f(g(x))
pub fn compose<A, B, C>(f: fn(B) -> C, g: fn(A) -> B) -> fn(A) -> C
    return |x| f(g(x))
end fn

# Pipe (reverse compose): (f |> g)(x) = g(f(x))
pub fn pipe<A, B, C>(f: fn(A) -> B, g: fn(B) -> C) -> fn(A) -> C
    return |x| g(f(x))
end fn

# Apply a function to a value
pub fn apply<T, R>(f: fn(T) -> R, x: T) -> R
    return f(x)
end fn

# Flip arguments of a binary function
pub fn flip<A, B, C>(f: fn(A, B) -> C) -> fn(B, A) -> C
    return |b, a| f(a, b)
end fn

# =============================================================================
# Partial Application
# =============================================================================

# Partially apply first argument to binary function
pub fn partial1<A, B, R>(f: fn(A, B) -> R, a: A) -> fn(B) -> R where A: Clone
    return |b| f(a.clone(), b)
end fn

# Partially apply second argument to binary function
pub fn partial2<A, B, R>(f: fn(A, B) -> R, b: B) -> fn(A) -> R where B: Clone
    return |a| f(a, b.clone())
end fn

# Curry a binary function
pub fn curry<A, B, R>(f: fn(A, B) -> R) -> fn(A) -> fn(B) -> R
    return |a| |b| f(a, b)
end fn

# Uncurry a curried function
pub fn uncurry<A, B, R>(f: fn(A) -> fn(B) -> R) -> fn(A, B) -> R
    return |a, b| f(a)(b)
end fn

# =============================================================================
# Predicate Combinators
# =============================================================================

# Negate a predicate
pub fn not_pred<T>(p: Predicate<T>) -> Predicate<T>
    return |x| not p(x)
end fn

# Combine predicates with AND
pub fn and_pred<T>(p1: Predicate<T>, p2: Predicate<T>) -> Predicate<T>
    return |x| p1(x) and p2(x)
end fn

# Combine predicates with OR
pub fn or_pred<T>(p1: Predicate<T>, p2: Predicate<T>) -> Predicate<T>
    return |x| p1(x) or p2(x)
end fn

# Always true predicate
pub fn always_true<T>() -> Predicate<T>
    return |_| true
end fn

# Always false predicate
pub fn always_false<T>() -> Predicate<T>
    return |_| false
end fn

# =============================================================================
# Memoization
# =============================================================================

# Memoize a function (caches results)
# Note: This is a simple implementation using a map
pub struct Memoized<A, R> where A: Hash + Eq
    func: fn(A) -> R
    cache: Map<A, R>
end struct

pub impl<A, R> Memoized<A, R> where A: Hash + Eq, R: Clone
    pub fn new(f: fn(A) -> R) -> Memoized<A, R>
        return Memoized { func: f, cache: Map.new() }
    end fn

    pub fn call(mut self, arg: A) -> R where A: Clone
        match self.cache.get(arg.clone())
            Option.Some(result) -> return result.clone()
            Option.None ->
                let result = self.func(arg.clone())
                self.cache.insert(arg, result.clone())
                return result
        end match
    end fn

    pub fn clear_cache(mut self)
        self.cache.clear()
    end fn
end impl

# =============================================================================
# Lazy Evaluation
# =============================================================================

# A lazily evaluated value
pub struct Lazy<T>
    initializer: fn() -> T
    value: Option<T>
    initialized: bool
end struct

pub impl<T> Lazy<T>
    # Create a new lazy value
    pub fn new(init: fn() -> T) -> Lazy<T>
        return Lazy {
            initializer: init,
            value: Option.None,
            initialized: false
        }
    end fn

    # Get the value, initializing if needed
    pub fn get(mut self) -> ref<T>
        if not self.initialized then
            self.value = Option.Some(self.initializer())
            self.initialized = true
        end if
        return self.value.unwrap()
    end fn

    # Check if initialized
    pub fn is_initialized(self) -> bool
        return self.initialized
    end fn

    # Force initialization
    pub fn force(mut self)
        if not self.initialized then
            self.value = Option.Some(self.initializer())
            self.initialized = true
        end if
    end fn

    # Map over lazy value
    pub fn map<U>(self, f: fn(T) -> U) -> Lazy<U>
        let this = self
        return Lazy.new(|| f(this.get()))
    end fn
end impl

# =============================================================================
# Either Type (Left or Right)
# =============================================================================

pub enum Either<L, R>
    Left(L)
    Right(R)
end enum

pub impl<L, R> Either<L, R>
    pub fn left(value: L) -> Either<L, R>
        return Either.Left(value)
    end fn

    pub fn right(value: R) -> Either<L, R>
        return Either.Right(value)
    end fn

    pub fn is_left(self) -> bool
        match self
            Either.Left(_) -> return true
            Either.Right(_) -> return false
        end match
    end fn

    pub fn is_right(self) -> bool
        match self
            Either.Left(_) -> return false
            Either.Right(_) -> return true
        end match
    end fn

    pub fn left_or(self, default: L) -> L
        match self
            Either.Left(l) -> return l
            Either.Right(_) -> return default
        end match
    end fn

    pub fn right_or(self, default: R) -> R
        match self
            Either.Left(_) -> return default
            Either.Right(r) -> return r
        end match
    end fn

    pub fn map_left<L2>(self, f: fn(L) -> L2) -> Either<L2, R>
        match self
            Either.Left(l) -> return Either.Left(f(l))
            Either.Right(r) -> return Either.Right(r)
        end match
    end fn

    pub fn map_right<R2>(self, f: fn(R) -> R2) -> Either<L, R2>
        match self
            Either.Left(l) -> return Either.Left(l)
            Either.Right(r) -> return Either.Right(f(r))
        end match
    end fn

    pub fn swap(self) -> Either<R, L>
        match self
            Either.Left(l) -> return Either.Right(l)
            Either.Right(r) -> return Either.Left(r)
        end match
    end fn

    # Convert to Result (Right is Ok, Left is Error)
    pub fn to_result(self) -> Result<R, L>
        match self
            Either.Left(l) -> return Result.Error(l)
            Either.Right(r) -> return Result.Ok(r)
        end match
    end fn
end impl

# =============================================================================
# Unit Type
# =============================================================================

# The unit type (like void but a real type)
pub struct Unit end struct

pub impl Unit
    pub fn unit() -> Unit
        return Unit {}
    end fn
end impl

pub impl Default for Unit
    fn default() -> Unit
        return Unit {}
    end fn
end impl

pub impl Eq for Unit
    fn eq(self, other: Unit) -> bool
        return true
    end fn
end impl

pub impl Debug for Unit
    fn debug(self) -> text
        return "()"
    end fn
end impl

# =============================================================================
# Tuple Operations
# =============================================================================

pub struct Tuple2<A, B>
    pub _0: A
    pub _1: B
end struct

pub struct Tuple3<A, B, C>
    pub _0: A
    pub _1: B
    pub _2: C
end struct

pub struct Tuple4<A, B, C, D>
    pub _0: A
    pub _1: B
    pub _2: C
    pub _3: D
end struct

pub impl<A, B> Tuple2<A, B>
    pub fn new(a: A, b: B) -> Tuple2<A, B>
        return Tuple2 { _0: a, _1: b }
    end fn

    pub fn first(self) -> A
        return self._0
    end fn

    pub fn second(self) -> B
        return self._1
    end fn

    pub fn swap(self) -> Tuple2<B, A>
        return Tuple2 { _0: self._1, _1: self._0 }
    end fn

    pub fn map_first<A2>(self, f: fn(A) -> A2) -> Tuple2<A2, B>
        return Tuple2 { _0: f(self._0), _1: self._1 }
    end fn

    pub fn map_second<B2>(self, f: fn(B) -> B2) -> Tuple2<A, B2>
        return Tuple2 { _0: self._0, _1: f(self._1) }
    end fn
end impl

pub impl<A, B, C> Tuple3<A, B, C>
    pub fn new(a: A, b: B, c: C) -> Tuple3<A, B, C>
        return Tuple3 { _0: a, _1: b, _2: c }
    end fn
end impl

pub impl<A, B, C, D> Tuple4<A, B, C, D>
    pub fn new(a: A, b: B, c: C, d: D) -> Tuple4<A, B, C, D>
        return Tuple4 { _0: a, _1: b, _2: c, _3: d }
    end fn
end impl

# =============================================================================
# Comparison Functions
# =============================================================================

# Compare by key
pub fn comparing<T, K>(key_fn: fn(T) -> K) -> fn(T, T) -> Ordering where K: Ord
    return |a, b| key_fn(a).cmp(key_fn(b))
end fn

# Reverse comparison
pub fn reverse_order<T>(cmp: fn(T, T) -> Ordering) -> fn(T, T) -> Ordering
    return |a, b| cmp(b, a)
end fn

# Chain comparisons (use second if first is Equal)
pub fn then_comparing<T>(
    first: fn(T, T) -> Ordering,
    second: fn(T, T) -> Ordering
) -> fn(T, T) -> Ordering
    return |a, b|
        match first(a, b)
            Ordering.Equal -> second(a, b)
            other -> other
        end match
end fn

# =============================================================================
# Common Numeric Functions
# =============================================================================

# Maximum of two values
pub fn max<T>(a: T, b: T) -> T where T: Ord
    if a.cmp(b).is_greater() then
        return a
    end if
    return b
end fn

# Minimum of two values
pub fn min<T>(a: T, b: T) -> T where T: Ord
    if a.cmp(b).is_less() then
        return a
    end if
    return b
end fn

# Clamp value between bounds
pub fn clamp<T>(value: T, min_val: T, max_val: T) -> T where T: Ord
    if value.cmp(min_val).is_less() then
        return min_val
    end if
    if value.cmp(max_val).is_greater() then
        return max_val
    end if
    return value
end fn

