module ny.engine
import ny.lang.core

# Simple Input State
# For bootstrapping, we might just track a few keys or use a fixed array if supported.
# Since we use 'any' for arrays in ECS, we can use 'any' here too or just specific global variables.

pub let mut last_key_pressed: i32 = 0
pub let mut key_states: i32 = 0 # Placeholder for array

pub fn init()
    # Initialize key_states array
end fn

pub fn is_key_down(code: i32) -> bool
    return mimic.is_key_down(code)
end fn

pub fn set_key(code: i32, down: bool)
   # Intentionally empty, we use polling now
end fn

# Key Constants (Virtual Key Codes)
pub let KEY_ESCAPE: i32 = 27
pub let KEY_SPACE: i32 = 32
pub let KEY_W: i32 = 87
pub let KEY_A: i32 = 65
pub let KEY_S: i32 = 83
pub let KEY_D: i32 = 68
