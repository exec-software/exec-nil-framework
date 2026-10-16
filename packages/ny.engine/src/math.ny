module ny.engine
import ny.lang.math

# =============================================================================
# Vector 4
# =============================================================================
pub struct vec4
    x: f64
    y: f64
    z: f64
    w: f64
end struct

impl vec4
    pub fn new(x: f64, y: f64, z: f64, w: f64) -> vec4
        return vec4 { x: x, y: y, z: z, w: w }
    end fn

    pub fn zero() -> vec4
        return vec4 { x: 0.0, y: 0.0, z: 0.0, w: 0.0 }
    end fn
end impl

# =============================================================================
# Quaternion
# =============================================================================
pub struct quat
    x: f64
    y: f64
    z: f64
    w: f64
end struct

impl quat
    pub fn identity() -> quat
        return quat { x: 0.0, y: 0.0, z: 0.0, w: 1.0 }
    end fn

    pub fn new(x: f64, y: f64, z: f64, w: f64) -> quat
        return quat { x: x, y: y, z: z, w: w }
    end fn
end impl

# =============================================================================
# Matrix 4x4 (Row Major Storage for simplicity, usually Column Major for GPU)
# We use named fields to guarantee layout sans array support
# =============================================================================
pub struct mat4
    m00: f64, m01: f64, m02: f64, m03: f64
    m10: f64, m11: f64, m12: f64, m13: f64
    m20: f64, m21: f64, m22: f64, m23: f64
    m30: f64, m31: f64, m32: f64, m33: f64
end struct

impl mat4
    pub fn identity() -> mat4
        return mat4 {
            m00: 1.0, m01: 0.0, m02: 0.0, m03: 0.0,
            m10: 0.0, m11: 1.0, m12: 0.0, m13: 0.0,
            m20: 0.0, m21: 0.0, m22: 1.0, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    pub fn zero() -> mat4
         return mat4 {
            m00: 0.0, m01: 0.0, m02: 0.0, m03: 0.0,
            m10: 0.0, m11: 0.0, m12: 0.0, m13: 0.0,
            m20: 0.0, m21: 0.0, m22: 0.0, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 0.0
        }
    end fn

    pub fn translation(t: math.vector.vec3) -> mat4
        return mat4 {
            m00: 1.0, m01: 0.0, m02: 0.0, m03: t.x,
            m10: 0.0, m11: 1.0, m12: 0.0, m13: t.y,
            m20: 0.0, m21: 0.0, m22: 1.0, m23: t.z,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Perspective projection matrix (fov in radians, aspect = width/height)
    pub fn perspective(fov: f64, aspect: f64, near: f64, far: f64) -> mat4
        let tan_half_fov = math.tan(fov / 2.0)
        let f = 1.0 / tan_half_fov
        let range_inv = 1.0 / (near - far)
        return mat4 {
            m00: f / aspect, m01: 0.0, m02: 0.0, m03: 0.0,
            m10: 0.0, m11: f, m12: 0.0, m13: 0.0,
            m20: 0.0, m21: 0.0, m22: (far + near) * range_inv, m23: 2.0 * far * near * range_inv,
            m30: 0.0, m31: 0.0, m32: -1.0, m33: 0.0
        }
    end fn

    # Look-at view matrix (camera at eye, looking at target, up vector)
    pub fn look_at(eye: math.vector.vec3, target: math.vector.vec3, up: math.vector.vec3) -> mat4
        let f = target.sub(eye).normalize()
        let s = f.cross(up).normalize()
        let u = s.cross(f)
        return mat4 {
            m00: s.x, m01: s.y, m02: s.z, m03: -s.dot(eye),
            m10: u.x, m11: u.y, m12: u.z, m13: -u.dot(eye),
            m20: -f.x, m21: -f.y, m22: -f.z, m23: f.dot(eye),
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Rotation around X axis (angle in radians)
    pub fn rotation_x(angle: f64) -> mat4
        let c = math.cos(angle)
        let s = math.sin(angle)
        return mat4 {
            m00: 1.0, m01: 0.0, m02: 0.0, m03: 0.0,
            m10: 0.0, m11: c, m12: -s, m13: 0.0,
            m20: 0.0, m21: s, m22: c, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Rotation around Y axis (angle in radians)
    pub fn rotation_y(angle: f64) -> mat4
        let c = math.cos(angle)
        let s = math.sin(angle)
        return mat4 {
            m00: c, m01: 0.0, m02: s, m03: 0.0,
            m10: 0.0, m11: 1.0, m12: 0.0, m13: 0.0,
            m20: -s, m21: 0.0, m22: c, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Rotation around Z axis (angle in radians)
    pub fn rotation_z(angle: f64) -> mat4
        let c = math.cos(angle)
        let s = math.sin(angle)
        return mat4 {
            m00: c, m01: -s, m02: 0.0, m03: 0.0,
            m10: s, m11: c, m12: 0.0, m13: 0.0,
            m20: 0.0, m21: 0.0, m22: 1.0, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Scale matrix
    pub fn scale(sx: f64, sy: f64, sz: f64) -> mat4
        return mat4 {
            m00: sx, m01: 0.0, m02: 0.0, m03: 0.0,
            m10: 0.0, m11: sy, m12: 0.0, m13: 0.0,
            m20: 0.0, m21: 0.0, m22: sz, m23: 0.0,
            m30: 0.0, m31: 0.0, m32: 0.0, m33: 1.0
        }
    end fn

    # Matrix multiplication (self * other)
    pub fn mul(self, other: mat4) -> mat4
        return mat4 {
            m00: self.m00*other.m00 + self.m01*other.m10 + self.m02*other.m20 + self.m03*other.m30,
            m01: self.m00*other.m01 + self.m01*other.m11 + self.m02*other.m21 + self.m03*other.m31,
            m02: self.m00*other.m02 + self.m01*other.m12 + self.m02*other.m22 + self.m03*other.m32,
            m03: self.m00*other.m03 + self.m01*other.m13 + self.m02*other.m23 + self.m03*other.m33,
            m10: self.m10*other.m00 + self.m11*other.m10 + self.m12*other.m20 + self.m13*other.m30,
            m11: self.m10*other.m01 + self.m11*other.m11 + self.m12*other.m21 + self.m13*other.m31,
            m12: self.m10*other.m02 + self.m11*other.m12 + self.m12*other.m22 + self.m13*other.m32,
            m13: self.m10*other.m03 + self.m11*other.m13 + self.m12*other.m23 + self.m13*other.m33,
            m20: self.m20*other.m00 + self.m21*other.m10 + self.m22*other.m20 + self.m23*other.m30,
            m21: self.m20*other.m01 + self.m21*other.m11 + self.m22*other.m21 + self.m23*other.m31,
            m22: self.m20*other.m02 + self.m21*other.m12 + self.m22*other.m22 + self.m23*other.m32,
            m23: self.m20*other.m03 + self.m21*other.m13 + self.m22*other.m23 + self.m23*other.m33,
            m30: self.m30*other.m00 + self.m31*other.m10 + self.m32*other.m20 + self.m33*other.m30,
            m31: self.m30*other.m01 + self.m31*other.m11 + self.m32*other.m21 + self.m33*other.m31,
            m32: self.m30*other.m02 + self.m31*other.m12 + self.m32*other.m22 + self.m33*other.m32,
            m33: self.m30*other.m03 + self.m31*other.m13 + self.m32*other.m23 + self.m33*other.m33
        }
    end fn

    # Transform a vec4 by this matrix
    pub fn transform(self, v: vec4) -> vec4
        return vec4 {
            x: self.m00*v.x + self.m01*v.y + self.m02*v.z + self.m03*v.w,
            y: self.m10*v.x + self.m11*v.y + self.m12*v.z + self.m13*v.w,
            z: self.m20*v.x + self.m21*v.y + self.m22*v.z + self.m23*v.w,
            w: self.m30*v.x + self.m31*v.y + self.m32*v.z + self.m33*v.w
        }
    end fn
end impl
