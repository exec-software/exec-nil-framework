module ny.engine
import ny.lang.core
import ny.lang.math

# =============================================================================
# Physics & Collision System
# Simple physics for 3D games without external physics engine
# =============================================================================

# Axis-aligned bounding box
pub struct AABB
    min: math.vector.vec3
    max: math.vector.vec3
end struct

impl AABB
    pub fn new(min: math.vector.vec3, max: math.vector.vec3) -> AABB
        return AABB { min: min, max: max }
    end fn

    # Create AABB centered at position with half-extents
    pub fn from_center(center: math.vector.vec3, half_extents: math.vector.vec3) -> AABB
        return AABB {
            min: math.vector.vec3 {
                x: center.x - half_extents.x,
                y: center.y - half_extents.y,
                z: center.z - half_extents.z
            },
            max: math.vector.vec3 {
                x: center.x + half_extents.x,
                y: center.y + half_extents.y,
                z: center.z + half_extents.z
            }
        }
    end fn

    # Check if two AABBs intersect
    pub fn intersects(self, other: AABB) -> bool
        if self.max.x < other.min.x then return false end if
        if self.min.x > other.max.x then return false end if
        if self.max.y < other.min.y then return false end if
        if self.min.y > other.max.y then return false end if
        if self.max.z < other.min.z then return false end if
        if self.min.z > other.max.z then return false end if
        return true
    end fn

    # Check if point is inside AABB
    pub fn contains_point(self, p: math.vector.vec3) -> bool
        return p.x >= self.min.x and p.x <= self.max.x and
               p.y >= self.min.y and p.y <= self.max.y and
               p.z >= self.min.z and p.z <= self.max.z
    end fn

    # Get center of AABB
    pub fn center(self) -> math.vector.vec3
        return math.vector.vec3 {
            x: (self.min.x + self.max.x) / 2.0,
            y: (self.min.y + self.max.y) / 2.0,
            z: (self.min.z + self.max.z) / 2.0
        }
    end fn

    # Move AABB by delta
    pub fn translate(self, delta: math.vector.vec3) -> AABB
        return AABB {
            min: self.min.add(delta),
            max: self.max.add(delta)
        }
    end fn
end impl

# Sphere collider
pub struct Sphere
    center: math.vector.vec3
    radius: f64
end struct

impl Sphere
    pub fn new(center: math.vector.vec3, radius: f64) -> Sphere
        return Sphere { center: center, radius: radius }
    end fn

    # Check if two spheres intersect
    pub fn intersects(self, other: Sphere) -> bool
        let dist = self.center.distance(other.center)
        return dist <= (self.radius + other.radius)
    end fn

    # Check if point is inside sphere
    pub fn contains_point(self, p: math.vector.vec3) -> bool
        return self.center.distance(p) <= self.radius
    end fn

    # Move sphere by delta
    pub fn translate(self, delta: math.vector.vec3) -> Sphere
        return Sphere {
            center: self.center.add(delta),
            radius: self.radius
        }
    end fn
end impl

# Ray for raycasting
pub struct Ray
    origin: math.vector.vec3
    direction: math.vector.vec3  # Should be normalized
end struct

impl Ray
    pub fn new(origin: math.vector.vec3, direction: math.vector.vec3) -> Ray
        return Ray { origin: origin, direction: direction.normalize() }
    end fn

    # Get point along ray at distance t
    pub fn point_at(self, t: f64) -> math.vector.vec3
        return self.origin.add(self.direction.scale(t))
    end fn
end impl

# Raycast result
pub struct RaycastHit
    hit: bool
    distance: f64
    point: math.vector.vec3
    normal: math.vector.vec3
end struct

impl RaycastHit
    pub fn miss() -> RaycastHit
        return RaycastHit {
            hit: false,
            distance: 0.0,
            point: math.vector.vec3.zero(),
            normal: math.vector.vec3.zero()
        }
    end fn

    pub fn create(distance: f64, point: math.vector.vec3, normal: math.vector.vec3) -> RaycastHit
        return RaycastHit {
            hit: true,
            distance: distance,
            point: point,
            normal: normal
        }
    end fn
end impl

# =============================================================================
# Rigid Body Physics
# =============================================================================

pub struct RigidBody
    position: math.vector.vec3
    velocity: math.vector.vec3
    acceleration: math.vector.vec3
    mass: f64
    drag: f64                   # Air resistance factor
    is_kinematic: bool          # If true, not affected by physics
end struct

impl RigidBody
    pub fn new(position: math.vector.vec3, mass: f64) -> RigidBody
        return RigidBody {
            position: position,
            velocity: math.vector.vec3.zero(),
            acceleration: math.vector.vec3.zero(),
            mass: mass,
            drag: 0.01,
            is_kinematic: false
        }
    end fn

    # Apply force (F = ma)
    pub fn apply_force(self, force: math.vector.vec3) -> RigidBody
        if self.is_kinematic then return self end if
        let accel = force.scale(1.0 / self.mass)
        return RigidBody {
            position: self.position,
            velocity: self.velocity,
            acceleration: self.acceleration.add(accel),
            mass: self.mass,
            drag: self.drag,
            is_kinematic: self.is_kinematic
        }
    end fn

    # Apply impulse (instant velocity change)
    pub fn apply_impulse(self, impulse: math.vector.vec3) -> RigidBody
        if self.is_kinematic then return self end if
        let vel_change = impulse.scale(1.0 / self.mass)
        return RigidBody {
            position: self.position,
            velocity: self.velocity.add(vel_change),
            acceleration: self.acceleration,
            mass: self.mass,
            drag: self.drag,
            is_kinematic: self.is_kinematic
        }
    end fn

    # Integrate physics over delta time
    pub fn update(self, dt: f64, gravity: math.vector.vec3) -> RigidBody
        if self.is_kinematic then return self end if

        # Apply gravity
        let total_accel = self.acceleration.add(gravity)

        # Update velocity: v = v + a*dt
        let new_vel = self.velocity.add(total_accel.scale(dt))

        # Apply drag: v = v * (1 - drag)
        let damped_vel = new_vel.scale(1.0 - self.drag)

        # Update position: p = p + v*dt
        let new_pos = self.position.add(damped_vel.scale(dt))

        return RigidBody {
            position: new_pos,
            velocity: damped_vel,
            acceleration: math.vector.vec3.zero(),  # Reset for next frame
            mass: self.mass,
            drag: self.drag,
            is_kinematic: self.is_kinematic
        }
    end fn
end impl

# World gravity constant
pub let GRAVITY: f64 = -9.81
