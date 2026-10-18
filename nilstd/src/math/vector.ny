@pragma once

module nil.std.math
import nil.std.core
import nil.std.math.norms

@intrinsic("math.sqrt")
fn vector_sqrt(x: f64) -> f64
end fn

# =============================================================================
# Vector 2D
# =============================================================================

pub struct vec2
    x: f64
    y: f64
end struct

impl vec2
    # Create new vec2
    pub fn new(x: f64, y: f64) -> vec2
        return vec2 { x: x, y: y }
    end fn

    # Zero vector
    pub fn zero() -> vec2
        return vec2 { x: 0.0, y: 0.0 }
    end fn

    # One vector
    pub fn one() -> vec2
        return vec2 { x: 1.0, y: 1.0 }
    end fn

    # Add two vectors
    pub fn add(self, other: vec2) -> vec2
        return vec2 { x: self.x + other.x, y: self.y + other.y }
    end fn

    # Subtract two vectors
    pub fn sub(self, other: vec2) -> vec2
        return vec2 { x: self.x - other.x, y: self.y - other.y }
    end fn

    # Multiply by scalar
    pub fn scale(self, s: f64) -> vec2
        return vec2 { x: self.x * s, y: self.y * s }
    end fn

    # Dot product
    pub fn dot(self, other: vec2) -> f64
        return self.x * other.x + self.y * other.y
    end fn

    # Ordinary dot product; squaring can overflow or underflow.
    pub fn length_sq(self) -> f64
        return self.dot(self)
    end fn

    # Length
    pub fn length(self) -> f64
        return hypot2(self.x, self.y)
    end fn

    pub fn is_finite(self) -> bool
        return is_finite(self.x) and is_finite(self.y)
    end fn

    # Some(unit vector) for finite nonzero inputs, even when length overflows.
    # Divide each component by the scale; its reciprocal may overflow.
    pub fn try_normalize(self) -> Option<vec2>
        if not self.is_finite() then return Option.None end if
        let scale = norm_scale2(self.x, self.y)
        if scale == 0.0 then return Option.None end if
        let x = self.x / scale
        let y = self.y / scale
        let len = vector_sqrt(x * x + y * y)
        return Option.Some(vec2.new(x / len, y / len))
    end fn

    # Invalid directions (zero or non-finite) retain the original value.
    # Use try_normalize to distinguish them from successful normalization.
    pub fn normalize(self) -> vec2
        match self.try_normalize()
            Option.Some(unit) -> return unit
            Option.None -> return self
        end match
    end fn

    # Distance to other vector
    pub fn distance(self, other: vec2) -> f64
        let diff = self.sub(other)
        return diff.length()
    end fn
end impl

# =============================================================================
# Vector 3D
# =============================================================================

pub struct vec3
    x: f64
    y: f64
    z: f64
end struct

impl vec3
    # Create new vec3
    pub fn new(x: f64, y: f64, z: f64) -> vec3
        return vec3 { x: x, y: y, z: z }
    end fn

    # Zero vector
    pub fn zero() -> vec3
        return vec3 { x: 0.0, y: 0.0, z: 0.0 }
    end fn

    # One vector
    pub fn one() -> vec3
        return vec3 { x: 1.0, y: 1.0, z: 1.0 }
    end fn

    # Add two vectors
    pub fn add(self, other: vec3) -> vec3
        return vec3 { x: self.x + other.x, y: self.y + other.y, z: self.z + other.z }
    end fn

    # Subtract two vectors
    pub fn sub(self, other: vec3) -> vec3
        return vec3 { x: self.x - other.x, y: self.y - other.y, z: self.z - other.z }
    end fn

    # Multiply by scalar
    pub fn scale(self, s: f64) -> vec3
        return vec3 { x: self.x * s, y: self.y * s, z: self.z * s }
    end fn

    # Dot product
    pub fn dot(self, other: vec3) -> f64
        return self.x * other.x + self.y * other.y + self.z * other.z
    end fn

    # Cross product
    pub fn cross(self, other: vec3) -> vec3
        return vec3 {
            x: self.y * other.z - self.z * other.y,
            y: self.z * other.x - self.x * other.z,
            z: self.x * other.y - self.y * other.x
        }
    end fn

    # Ordinary dot product; squaring can overflow or underflow.
    pub fn length_sq(self) -> f64
        return self.dot(self)
    end fn

    # Length
    pub fn length(self) -> f64
        return hypot3(self.x, self.y, self.z)
    end fn

    pub fn is_finite(self) -> bool
        return is_finite(self.x) and is_finite(self.y) and is_finite(self.z)
    end fn

    pub fn try_normalize(self) -> Option<vec3>
        if not self.is_finite() then return Option.None end if
        let scale = norm_scale3(self.x, self.y, self.z)
        if scale == 0.0 then return Option.None end if
        let x = self.x / scale
        let y = self.y / scale
        let z = self.z / scale
        let len = vector_sqrt(x * x + y * y + z * z)
        return Option.Some(vec3.new(x / len, y / len, z / len))
    end fn

    # Invalid directions retain the original value; try_normalize reports None.
    pub fn normalize(self) -> vec3
        match self.try_normalize()
            Option.Some(unit) -> return unit
            Option.None -> return self
        end match
    end fn

    # Distance to other vector
    pub fn distance(self, other: vec3) -> f64
        let diff = self.sub(other)
        return diff.length()
    end fn
end impl
