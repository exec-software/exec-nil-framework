@pragma once

module nil.std.core

# =============================================================================
# Ordering Enum
# =============================================================================

pub enum ordering
    less
    equal
    greater
end enum

impl ordering
    # Reverse the ordering
    pub fn reverse(self) -> ordering
        match self
            ordering.less -> return ordering.greater
            ordering.equal -> return ordering.equal
            ordering.greater -> return ordering.less
        end match
    end fn

    # Check if is less
    pub fn is_less(self) -> bool
        match self
            ordering.less -> return true
            ordering.equal -> return false
            ordering.greater -> return false
        end match
    end fn

    # Check if is equal
    pub fn is_eq(self) -> bool
        match self
            ordering.less -> return false
            ordering.equal -> return true
            ordering.greater -> return false
        end match
    end fn

    # Check if is greater
    pub fn is_gt(self) -> bool
        match self
            ordering.less -> return false
            ordering.equal -> return false
            ordering.greater -> return true
        end match
    end fn
end impl

# =============================================================================
# Comparison Utilities
# =============================================================================

# Compare two i32 values
pub fn compare_i32(a: i32, b: i32) -> ordering
    if a < b then
        return ordering.less
    else if a > b then
        return ordering.greater
    else
        return ordering.equal
    end if
end fn

# Compare two i64 values
pub fn compare_i64(a: i64, b: i64) -> ordering
    if a < b then
        return ordering.less
    else if a > b then
        return ordering.greater
    else
        return ordering.equal
    end if
end fn

# Library ordering: NaNs follow all numbers; two NaNs and signed zeros
# compare equal. This does not change the language's IEEE comparison operators.
pub fn compare_f64(a: f64, b: f64) -> ordering
    if a != a then
        if b != b then
            return ordering.equal
        end if
        return ordering.greater
    end if
    if b != b then
        return ordering.less
    end if
    if a < b then
        return ordering.less
    else if a > b then
        return ordering.greater
    else
        return ordering.equal
    end if
end fn

# Compare two strings (text)
pub fn compare_text(a: text, b: text) -> ordering
    if a < b then
        return ordering.less
    else if a > b then
        return ordering.greater
    else
        return ordering.equal
    end if
end fn
