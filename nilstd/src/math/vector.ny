@pragma once

module nil.std.math
import nil.std.core

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

    # Length squared
    pub fn length_sq(self) -> f64
        return self.dot(self)
    end fn

    # Length
    pub fn length(self) -> f64
        return vector_sqrt(self.length_sq())
    end fn

    # Normalize
    pub fn normalize(self) -> vec2
        let len = self.length()
        if len > 0.0 then
            return self.scale(1.0 / len)
        else
            return self
        end if
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

    # Length squared
    pub fn length_sq(self) -> f64
        return self.dot(self)
    end fn

    # Length
    pub fn length(self) -> f64
        return vector_sqrt(self.length_sq())
    end fn

    # Normalize
    pub fn normalize(self) -> vec3
        let len = self.length()
        if len > 0.0 then
            return self.scale(1.0 / len)
        else
            return self
        end if
    end fn

    # Distance to other vector
    pub fn distance(self, other: vec3) -> f64
        let diff = self.sub(other)
        return diff.length()
    end fn
end impl
