@pragma once
module ny.godot.math

# =============================================================================
# ny.godot.math — Godot Math Types
# =============================================================================
# Mirrors Godot's built-in math types. Structs are passed by decomposing into
# primitives when crossing the native boundary (i64/f64 only ABI).
# =============================================================================

# ── Vector2 ───────────────────────────────────────────────────────────────────

pub struct Vector2
    x: f64
    y: f64
end struct

impl Vector2
    pub fn new(x: f64, y: f64) -> Vector2
        return Vector2 { x: x, y: y }
    end fn

    pub fn zero() -> Vector2
        return Vector2 { x: 0.0, y: 0.0 }
    end fn

    pub fn one() -> Vector2
        return Vector2 { x: 1.0, y: 1.0 }
    end fn

    pub fn length(self) -> f64
        return (self.x * self.x + self.y * self.y) ^ 0.5
    end fn

    pub fn normalized(self) -> Vector2
        let len = self.length()
        if len == 0.0 then return Vector2.zero() end if
        return Vector2 { x: self.x / len, y: self.y / len }
    end fn

    pub fn dot(self, other: Vector2) -> f64
        return self.x * other.x + self.y * other.y
    end fn

    pub fn add(self, other: Vector2) -> Vector2
        return Vector2 { x: self.x + other.x, y: self.y + other.y }
    end fn

    pub fn sub(self, other: Vector2) -> Vector2
        return Vector2 { x: self.x - other.x, y: self.y - other.y }
    end fn

    pub fn scale(self, s: f64) -> Vector2
        return Vector2 { x: self.x * s, y: self.y * s }
    end fn

    pub fn distance_to(self, other: Vector2) -> f64
        return self.sub(other).length()
    end fn

    pub fn lerp(self, other: Vector2, t: f64) -> Vector2
        return Vector2 {
            x: self.x + (other.x - self.x) * t,
            y: self.y + (other.y - self.y) * t
        }
    end fn
end impl

# ── Vector3 ───────────────────────────────────────────────────────────────────

pub struct Vector3
    x: f64
    y: f64
    z: f64
end struct

impl Vector3
    pub fn new(x: f64, y: f64, z: f64) -> Vector3
        return Vector3 { x: x, y: y, z: z }
    end fn

    pub fn zero() -> Vector3
        return Vector3 { x: 0.0, y: 0.0, z: 0.0 }
    end fn

    pub fn one() -> Vector3
        return Vector3 { x: 1.0, y: 1.0, z: 1.0 }
    end fn

    pub fn up() -> Vector3
        return Vector3 { x: 0.0, y: 1.0, z: 0.0 }
    end fn

    pub fn forward() -> Vector3
        return Vector3 { x: 0.0, y: 0.0, z: -1.0 }
    end fn

    pub fn right() -> Vector3
        return Vector3 { x: 1.0, y: 0.0, z: 0.0 }
    end fn

    pub fn length(self) -> f64
        return (self.x * self.x + self.y * self.y + self.z * self.z) ^ 0.5
    end fn

    pub fn length_squared(self) -> f64
        return self.x * self.x + self.y * self.y + self.z * self.z
    end fn

    pub fn normalized(self) -> Vector3
        let len = self.length()
        if len == 0.0 then return Vector3.zero() end if
        return Vector3 { x: self.x / len, y: self.y / len, z: self.z / len }
    end fn

    pub fn dot(self, other: Vector3) -> f64
        return self.x * other.x + self.y * other.y + self.z * other.z
    end fn

    pub fn cross(self, other: Vector3) -> Vector3
        return Vector3 {
            x: self.y * other.z - self.z * other.y,
            y: self.z * other.x - self.x * other.z,
            z: self.x * other.y - self.y * other.x
        }
    end fn

    pub fn add(self, other: Vector3) -> Vector3
        return Vector3 { x: self.x + other.x, y: self.y + other.y, z: self.z + other.z }
    end fn

    pub fn sub(self, other: Vector3) -> Vector3
        return Vector3 { x: self.x - other.x, y: self.y - other.y, z: self.z - other.z }
    end fn

    pub fn scale(self, s: f64) -> Vector3
        return Vector3 { x: self.x * s, y: self.y * s, z: self.z * s }
    end fn

    pub fn negate(self) -> Vector3
        return Vector3 { x: -self.x, y: -self.y, z: -self.z }
    end fn

    pub fn distance_to(self, other: Vector3) -> f64
        return self.sub(other).length()
    end fn

    pub fn lerp(self, other: Vector3, t: f64) -> Vector3
        return Vector3 {
            x: self.x + (other.x - self.x) * t,
            y: self.y + (other.y - self.y) * t,
            z: self.z + (other.z - self.z) * t
        }
    end fn

    pub fn move_toward(self, target: Vector3, delta: f64) -> Vector3
        let diff = target.sub(self)
        let dist = diff.length()
        if dist <= delta then return target end if
        return self.add(diff.normalized().scale(delta))
    end fn

    pub fn angle_to(self, other: Vector3) -> f64
        let d = self.dot(other)
        let l = self.length() * other.length()
        if l == 0.0 then return 0.0 end if
        return math_acos(d / l)
    end fn
end impl

# ── Color ─────────────────────────────────────────────────────────────────────

pub struct Color
    r: f64
    g: f64
    b: f64
    a: f64
end struct

impl Color
    pub fn new(r: f64, g: f64, b: f64, a: f64) -> Color
        return Color { r: r, g: g, b: b, a: a }
    end fn

    pub fn rgb(r: f64, g: f64, b: f64) -> Color
        return Color { r: r, g: g, b: b, a: 1.0 }
    end fn

    pub fn white() -> Color   return Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 } end fn
    pub fn black() -> Color   return Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 } end fn
    pub fn red() -> Color     return Color { r: 1.0, g: 0.0, b: 0.0, a: 1.0 } end fn
    pub fn green() -> Color   return Color { r: 0.0, g: 1.0, b: 0.0, a: 1.0 } end fn
    pub fn blue() -> Color    return Color { r: 0.0, g: 0.0, b: 1.0, a: 1.0 } end fn
    pub fn transparent() -> Color return Color { r: 0.0, g: 0.0, b: 0.0, a: 0.0 } end fn

    pub fn lerp(self, other: Color, t: f64) -> Color
        return Color {
            r: self.r + (other.r - self.r) * t,
            g: self.g + (other.g - self.g) * t,
            b: self.b + (other.b - self.b) * t,
            a: self.a + (other.a - self.a) * t
        }
    end fn

    pub fn to_hex(self) -> i64
        let ri = (self.r * 255.0) as i64
        let gi = (self.g * 255.0) as i64
        let bi = (self.b * 255.0) as i64
        let ai = (self.a * 255.0) as i64
        return (ri * 16777216) + (gi * 65536) + (bi * 256) + ai
    end fn
end impl

# ── Native math helpers (syscall shims) ───────────────────────────────────────

@interop("ml_godot")
pub fn ml_godot_math_acos(v: f64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_math_asin(v: f64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_math_atan2(y: f64, x: f64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_math_sin(v: f64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_math_cos(v: f64) -> f64
    return 0.0
end fn

@interop("ml_godot")
pub fn ml_godot_math_sqrt(v: f64) -> f64
    return 0.0
end fn

pub fn math_acos(v: f64) -> f64
    return ml_godot_math_acos(v)
end fn

pub fn math_atan2(y: f64, x: f64) -> f64
    return ml_godot_math_atan2(y, x)
end fn

pub fn math_sin(v: f64) -> f64
    return ml_godot_math_sin(v)
end fn

pub fn math_cos(v: f64) -> f64
    return ml_godot_math_cos(v)
end fn

pub fn math_sqrt(v: f64) -> f64
    return ml_godot_math_sqrt(v)
end fn

pub fn clampf(v: f64, lo: f64, hi: f64) -> f64
    if v < lo then return lo end if
    if v > hi then return hi end if
    return v
end fn

pub fn lerpf(a: f64, b: f64, t: f64) -> f64
    return a + (b - a) * t
end fn
