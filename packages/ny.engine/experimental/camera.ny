module ny.engine
import ny.lang.core
import ny.lang.math
import engine_math
import engine_input

# =============================================================================
# First-Person Camera
# =============================================================================

pub struct Camera
    position: math.vector.vec3
    yaw: f64       # Rotation around Y axis (horizontal)
    pitch: f64     # Rotation around X axis (vertical)
    fov: f64       # Field of view in radians
    aspect: f64    # Aspect ratio (width/height)
    near: f64      # Near clipping plane
    far: f64       # Far clipping plane
end struct

impl Camera
    # Create new camera at origin looking forward (-Z)
    pub fn new() -> Camera
        return Camera {
            position: math.vector.vec3.zero(),
            yaw: 0.0,
            pitch: 0.0,
            fov: 1.0472,     # 60 degrees
            aspect: 1.333,   # 4:3
            near: 0.1,
            far: 1000.0
        }
    end fn

    # Create camera with specific settings
    pub fn create(pos: math.vector.vec3, fov: f64, aspect: f64) -> Camera
        return Camera {
            position: pos,
            yaw: 0.0,
            pitch: 0.0,
            fov: fov,
            aspect: aspect,
            near: 0.1,
            far: 1000.0
        }
    end fn

    # Get forward direction vector
    pub fn forward(self) -> math.vector.vec3
        let cy = math.cos(self.yaw)
        let sy = math.sin(self.yaw)
        let cp = math.cos(self.pitch)
        let sp = math.sin(self.pitch)
        return math.vector.vec3 {
            x: sy * cp,
            y: sp,
            z: -cy * cp
        }
    end fn

    # Get right direction vector
    pub fn right(self) -> math.vector.vec3
        let cy = math.cos(self.yaw)
        let sy = math.sin(self.yaw)
        return math.vector.vec3 {
            x: cy,
            y: 0.0,
            z: sy
        }
    end fn

    # Get up direction vector (world up = Y)
    pub fn up(self) -> math.vector.vec3
        return math.vector.vec3 { x: 0.0, y: 1.0, z: 0.0 }
    end fn

    # Move camera forward/backward
    pub fn move_forward(self, distance: f64) -> Camera
        let dir = self.forward()
        return Camera {
            position: self.position.add(dir.scale(distance)),
            yaw: self.yaw,
            pitch: self.pitch,
            fov: self.fov,
            aspect: self.aspect,
            near: self.near,
            far: self.far
        }
    end fn

    # Strafe left/right
    pub fn move_right(self, distance: f64) -> Camera
        let dir = self.right()
        return Camera {
            position: self.position.add(dir.scale(distance)),
            yaw: self.yaw,
            pitch: self.pitch,
            fov: self.fov,
            aspect: self.aspect,
            near: self.near,
            far: self.far
        }
    end fn

    # Move up/down (world Y axis)
    pub fn move_up(self, distance: f64) -> Camera
        let dir = self.up()
        return Camera {
            position: self.position.add(dir.scale(distance)),
            yaw: self.yaw,
            pitch: self.pitch,
            fov: self.fov,
            aspect: self.aspect,
            near: self.near,
            far: self.far
        }
    end fn

    # Rotate camera with mouse delta (yaw = horizontal, pitch = vertical)
    pub fn rotate(self, delta_yaw: f64, delta_pitch: f64) -> Camera
        let new_pitch = self.pitch + delta_pitch
        # Clamp pitch to avoid gimbal lock
        let max_pitch = 1.5    # ~85 degrees
        let mut clamped_pitch = new_pitch
        if clamped_pitch > max_pitch then
            clamped_pitch = max_pitch
        end if
        if clamped_pitch < -max_pitch then
            clamped_pitch = -max_pitch
        end if
        return Camera {
            position: self.position,
            yaw: self.yaw + delta_yaw,
            pitch: clamped_pitch,
            fov: self.fov,
            aspect: self.aspect,
            near: self.near,
            far: self.far
        }
    end fn

    # Get view matrix
    pub fn view_matrix(self) -> engine_math.mat4
        let target = self.position.add(self.forward())
        let world_up = self.up()
        return engine_math.mat4.look_at(self.position, target, world_up)
    end fn

    # Get projection matrix
    pub fn projection_matrix(self) -> engine_math.mat4
        return engine_math.mat4.perspective(self.fov, self.aspect, self.near, self.far)
    end fn

    # Get combined view-projection matrix
    pub fn view_projection_matrix(self) -> engine_math.mat4
        let view = self.view_matrix()
        let proj = self.projection_matrix()
        return proj.mul(view)
    end fn

    # Update camera based on keyboard input (WASD)
    pub fn update_from_input(self, delta_time: f64, move_speed: f64) -> Camera
        let mut cam = self

        if engine_input.is_key_down(engine_input.KEY_W) then
            cam = cam.move_forward(move_speed * delta_time)
        end if
        if engine_input.is_key_down(engine_input.KEY_S) then
            cam = cam.move_forward(-move_speed * delta_time)
        end if
        if engine_input.is_key_down(engine_input.KEY_A) then
            cam = cam.move_right(-move_speed * delta_time)
        end if
        if engine_input.is_key_down(engine_input.KEY_D) then
            cam = cam.move_right(move_speed * delta_time)
        end if
        if engine_input.is_key_down(engine_input.KEY_SPACE) then
            cam = cam.move_up(move_speed * delta_time)
        end if

        return cam
    end fn
end impl
