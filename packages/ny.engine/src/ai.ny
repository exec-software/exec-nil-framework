module ny.engine

import console

# Simple Boid (flocking agent) structure
# Using integer position/velocity (scaled by 100 for precision)
pub struct Boid
    x: i32       # Position X (pixels)
    y: i32       # Position Y (pixels)
    vx: i32      # Velocity X (scaled by 10: vx=10 means 1 pixel/frame)
    vy: i32      # Velocity Y
end struct

# Create a new boid at position with random-ish velocity
pub fn create_boid(x: i32, y: i32, vx: i32, vy: i32) -> Boid
    let b: Boid = Boid {
        x: x,
        y: y,
        vx: vx,
        vy: vy
    }
    return b
end fn

# Update a single boid's position based on velocity
# Returns the updated boid
pub fn update_boid(b: Boid, screen_w: i32, screen_h: i32) -> Boid
    # Apply velocity (scaled down by 10)
    let new_x = b.x + (b.vx / 10)
    let new_y = b.y + (b.vy / 10)

    # Wrap around screen edges
    let mut wrapped_x = new_x
    let mut wrapped_y = new_y
    let mut new_vx = b.vx
    let mut new_vy = b.vy

    # Bounce off walls instead of wrapping (more visible behavior)
    if new_x < 10 then
        wrapped_x = 10
        new_vx = 0 - b.vx  # Reverse direction
    end if
    if new_x > screen_w - 10 then
        wrapped_x = screen_w - 10
        new_vx = 0 - b.vx
    end if
    if new_y < 10 then
        wrapped_y = 10
        new_vy = 0 - b.vy
    end if
    if new_y > screen_h - 10 then
        wrapped_y = screen_h - 10
        new_vy = 0 - b.vy
    end if

    let result: Boid = Boid {
        x: wrapped_x,
        y: wrapped_y,
        vx: new_vx,
        vy: new_vy
    }
    return result
end fn

# Calculate squared distance between two boids (avoids sqrt)
pub fn dist_sq(a: Boid, b: Boid) -> i32
    let dx = a.x - b.x
    let dy = a.y - b.y
    return (dx * dx) + (dy * dy)
end fn

# Apply separation force - steer away from nearby boids
# Returns delta velocity (vx, vy) packed as i32 (high=vx, low=vy)
pub fn separation_force(me: Boid, other: Boid) -> i32
    let sep_dist = 50  # Separation distance threshold
    let sep_dist_sq = sep_dist * sep_dist

    let d2 = dist_sq(me, other)
    if d2 < sep_dist_sq then
        if d2 > 0 then
            # Push away from other
            let dx = me.x - other.x
            let dy = me.y - other.y

            # Scale force inversely with distance (closer = stronger)
            let force = sep_dist_sq / (d2 + 1)
            let fx = (dx * force) / sep_dist
            let fy = (dy * force) / sep_dist

            # Pack into single i32: high 16 bits = fx, low 16 bits = fy
            return ((fx * 256) + fy)
        end if
    end if
    return 0
end fn

# Unpack velocity X from packed force
pub fn unpack_fx(packed: i32) -> i32
    return packed / 256
end fn

# Unpack velocity Y from packed force
pub fn unpack_fy(packed: i32) -> i32
    let fx = packed / 256
    return packed - (fx * 256)
end fn

# Apply cohesion - steer towards center of nearby boids
pub fn cohesion_force(me: Boid, center_x: i32, center_y: i32) -> i32
    let dx = center_x - me.x
    let dy = center_y - me.y

    # Weak attraction towards center
    let fx = dx / 50
    let fy = dy / 50

    return ((fx * 256) + fy)
end fn
