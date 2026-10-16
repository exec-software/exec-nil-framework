module ny.engine
import ny.lang.core
import engine_math
import engine_camera
import ny.lang.math

# =============================================================================
# Scene Graph
# Manages 3D objects in a hierarchical structure
# =============================================================================

# Transform component for scene nodes
pub struct SceneTransform
    position: math.vector.vec3
    rotation: math.vector.vec3   # Euler angles (pitch, yaw, roll)
    scale: math.vector.vec3
end struct

impl SceneTransform
    pub fn identity() -> SceneTransform
        return SceneTransform {
            position: math.vector.vec3.zero(),
            rotation: math.vector.vec3.zero(),
            scale: math.vector.vec3.one()
        }
    end fn

    pub fn at(x: f64, y: f64, z: f64) -> SceneTransform
        return SceneTransform {
            position: math.vector.vec3 { x: x, y: y, z: z },
            rotation: math.vector.vec3.zero(),
            scale: math.vector.vec3.one()
        }
    end fn

    # Get world matrix for this transform
    pub fn to_matrix(self) -> engine_math.mat4
        let t = engine_math.mat4.translation(self.position)
        let rx = engine_math.mat4.rotation_x(self.rotation.x)
        let ry = engine_math.mat4.rotation_y(self.rotation.y)
        let rz = engine_math.mat4.rotation_z(self.rotation.z)
        let s = engine_math.mat4.scale(self.scale.x, self.scale.y, self.scale.z)

        # Order: Scale -> RotZ -> RotY -> RotX -> Translate
        return t.mul(rx.mul(ry.mul(rz.mul(s))))
    end fn

    # Move by delta
    pub fn translate(self, dx: f64, dy: f64, dz: f64) -> SceneTransform
        return SceneTransform {
            position: math.vector.vec3 {
                x: self.position.x + dx,
                y: self.position.y + dy,
                z: self.position.z + dz
            },
            rotation: self.rotation,
            scale: self.scale
        }
    end fn

    # Rotate by delta (radians)
    pub fn rotate(self, dpitch: f64, dyaw: f64, droll: f64) -> SceneTransform
        return SceneTransform {
            position: self.position,
            rotation: math.vector.vec3 {
                x: self.rotation.x + dpitch,
                y: self.rotation.y + dyaw,
                z: self.rotation.z + droll
            },
            scale: self.scale
        }
    end fn
end impl

# Scene node types
pub enum NodeType
    Empty
    Mesh
    Light
    Camera
end enum

# Scene node
pub struct SceneNode
    name: text
    node_type: i32          # NodeType as i32 for simplicity
    transform: SceneTransform
    visible: bool
    parent_id: i32          # -1 for root
    mesh_id: i32            # Reference to mesh asset (-1 for none)
end struct

impl SceneNode
    pub fn new(name: text) -> SceneNode
        return SceneNode {
            name: name,
            node_type: 0,    # Empty
            transform: SceneTransform.identity(),
            visible: true,
            parent_id: -1,
            mesh_id: -1
        }
    end fn

    pub fn with_transform(self, t: SceneTransform) -> SceneNode
        return SceneNode {
            name: self.name,
            node_type: self.node_type,
            transform: t,
            visible: self.visible,
            parent_id: self.parent_id,
            mesh_id: self.mesh_id
        }
    end fn

    pub fn set_mesh(self, mesh_id: i32) -> SceneNode
        return SceneNode {
            name: self.name,
            node_type: 1,    # Mesh
            transform: self.transform,
            visible: self.visible,
            parent_id: self.parent_id,
            mesh_id: mesh_id
        }
    end fn
end impl

# =============================================================================
# Game Loop
# =============================================================================

pub struct GameTime
    delta: f64              # Time since last frame (seconds)
    elapsed: f64            # Total elapsed time (seconds)
    frame_count: i64
end struct

impl GameTime
    pub fn new() -> GameTime
        return GameTime {
            delta: 0.0,
            elapsed: 0.0,
            frame_count: (0 as i64)
        }
    end fn

    pub fn tick(self, delta: f64) -> GameTime
        return GameTime {
            delta: delta,
            elapsed: self.elapsed + delta,
            frame_count: self.frame_count + (1 as i64)
        }
    end fn
end impl

# Scene containing all objects
pub struct Scene
    name: text
    camera: engine_camera.Camera
    time: GameTime
    node_count: i32
end struct

impl Scene
    pub fn new(name: text) -> Scene
        return Scene {
            name: name,
            camera: engine_camera.Camera.new(),
            time: GameTime.new(),
            node_count: 0
        }
    end fn

    pub fn set_camera(self, cam: engine_camera.Camera) -> Scene
        return Scene {
            name: self.name,
            camera: cam,
            time: self.time,
            node_count: self.node_count
        }
    end fn

    pub fn update(self, delta: f64) -> Scene
        return Scene {
            name: self.name,
            camera: self.camera,
            time: self.time.tick(delta),
            node_count: self.node_count
        }
    end fn
end impl
