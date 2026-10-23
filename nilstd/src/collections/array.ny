module nil.std.collections

# Runtime-backed array primitives. Intrinsic declarations intentionally have
# no fallback body; their annotations provide the implementation.
# Element kinds include i32, i64, u8, bool, f64, text, and supported structural
# tuples. The f32 declarations are reserved until their ABI is defined.
# Sizes are nonnegative i32 values. Out-of-bounds scalar reads yield zero;
# tuple reads fail because there is no default tuple. Writes outside the array
# are no-ops, matching built-in array indexing.

@intrinsic("array.new_with_value")
pub fn new_with_value<T>(size: i32, value: T) -> array<T>
end fn

@intrinsic("array.new_f32")
pub fn new_f32(size: i32, value: f32) -> array<f32>
end fn

@intrinsic("array.new_u8")
pub fn new_u8(size: i32, value: u8) -> array<u8>
end fn

@intrinsic("array.get")
pub fn get<T>(arr: array<T>, idx: i32) -> T
end fn

@intrinsic("array.set")
pub fn array_set<T>(arr: array<T>, idx: i32, value: T)
end fn

@intrinsic("array.get_u8")
pub fn get_u8(arr: array<u8>, idx: i32) -> u8
end fn

@intrinsic("array.set_u8")
pub fn set_u8(arr: array<u8>, idx: i32, value: u8)
end fn

@intrinsic("array.get_f32")
pub fn get_f32(arr: array<f32>, idx: i32) -> f32
end fn

@intrinsic("array.set_f32")
pub fn set_f32(arr: array<f32>, idx: i32, value: f32)
end fn

# Helper functions
@intrinsic("array.length")
pub fn length<T>(arr: array<T>) -> i32
    return 0
end fn
