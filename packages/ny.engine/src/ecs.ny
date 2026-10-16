module ny.engine

import ny.lang.core
import engine_math

# =============================================================================
# Entity Component System
# =============================================================================

pub let MAX_ENTITIES: i32 = 10000

pub struct Entity
    id: i32
    generation: i32
end struct

impl Entity
    pub fn new(id: i32, gen: i32) -> Entity
        return Entity { id: id, generation: gen }
    end fn

    pub fn is_valid(self) -> bool
        return self.id >= 0
    end fn
end impl

# For now, we manually manage component arrays.
# In a robust ECS, this would be dynamic.

pub struct Transform
    position: engine_math.vec4 # Using vec4 for alignment padding/simd potential
    rotation: engine_math.quat
    scale: engine_math.vec4
end struct

impl Transform
    pub fn identity() -> Transform
        return Transform {
            position: engine_math.vec4.zero(),
            rotation: engine_math.quat.identity(),
            scale: engine_math.vec4.new(1.0, 1.0, 1.0, 1.0)
        }
    end fn
end impl

pub struct World
    # Entity Management
    next_id: i32
    entities: i32 # array of generations? Simpler: just counter for now.

    # Component Storage (Parallel Arrays / Sparse Sets)
    # Using fixed size arrays for bootstrap simplicity.
    # Large inline array implementations are not mature in Ny yet.
    # We will use 'vec' from core.

    transforms: any # core.vec<Transform>
    has_transform: any # core.vec<bool> # fast lookup
end struct

pub impl World
    pub fn new() -> World
        return World {
            next_id: 0,
            entities: 0,
            transforms: core.vec.new(),
            has_transform: core.vec.new()
        }
    end fn

    pub fn spawn(self: ref<Self>) -> Entity
        let id = self.next_id
        self.next_id = self.next_id + 1

        # Grow component flags
        # self.has_transform.push(false) # Needs to match ID

        # Hack: Resize vectors to ID+1?
        # Standard vector push logic for now.

        return Entity { id: id, generation: 0 }
    end fn

    pub fn add_transform(self: ref<Self>, e: Entity, t: Transform)
        # Ensure vectors are big enough
        # self.ensure_capacity(e.id)
        # self.transforms[e.id] = t
        # self.has_transform[e.id] = true
    end fn
end impl

pub fn init_world() -> World
    return World {
        next_id: 0,
        entities: 0,
        transforms: 0,
        has_transform: 0
    }
end fn
