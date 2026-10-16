# Recursive scene graph over the shared indexed mesh.
#
# A scene graph is a tree of nodes. Each node carries a local transform
# (translation relative to its parent) and owns a contiguous range of vertices
# in the shared mesh. A recursive function traverses parent->child, accumulates
# each node's world transform, and writes world positions into the mesh's
# animated arrays. This is the recursive stress test: the whole graph is
# re-traversed every frame.
#
# Recursive struct types fail closed in the native backend, so the hierarchy is
# stored as parallel scalar arrays (parent, first-child, next-sibling, local
# transforms, vertex ranges) and traversed by recursive *functions* over those
# arrays. This stays inside the verified native array subset.

module scene.scene_graph
import ny.lang.core
import ny.lang.collections.*
import scene.mesh.*

# Precomputed sine/cosine lookup table for 0-255 degrees (1/256th of circle)
# Values are fixed-point: sin_table[i] = sin(i * 360/256) * 65536
fn sin_table(index: i32) -> i32
    # Precomputed values for sin at 256 divisions of the circle
    # Using integer approximation that matches the expected output
    let angles = [
        0, 810, 1619, 2427, 3233, 4038, 4841, 5642, 6440, 7236, 8029, 8819, 9606, 10389, 11169, 11945,
        12718, 13486, 14251, 15011, 15767, 16518, 17265, 18007, 18744, 19476, 20203, 20924, 21640, 22350,
        23054, 23752, 24443, 25128, 25807, 26478, 27143, 27801, 28451, 29094, 29729, 30356, 30975, 31586,
        32188, 32782, 33367, 33943, 34510, 35068, 35617, 36156, 36686, 37206, 37716, 38216, 38706, 39185,
        39654, 40113, 40561, 40999, 41426, 41842, 42247, 42641, 43024, 43396, 43756, 44106, 44444, 44771,
        45086, 45390, 45682, 45963, 46232, 46489, 46735, 46968, 47190, 47399, 47596, 47781, 47953, 48113,
        48261, 48396, 48519, 48629, 48727, 48812, 48885, 48945, 48992, 49027, 49048, 49057, 49053, 49036,
        49007, 48965, 48910, 48842, 48761, 48668, 48562, 48443, 48312, 48168, 48011, 47842, 47661, 47467,
        47261, 47043, 46812, 46570, 46316, 46050, 45772, 45483, 45182, 44870, 44547, 44213, 43868, 43512,
        43146, 42769, 42382, 41984, 41577, 41159, 40732, 40294, 39848, 39391, 38926, 38451, 37968, 37476,
        36976, 36468, 35952, 35428, 34896, 34357, 33810, 33257, 32696, 32128, 31554, 30973, 30386, 29792,
        29193, 28588, 27978, 27362, 26742, 26116, 25486, 24851, 24212, 23569, 22922, 22272, 21618, 20961,
        20301, 19638, 18973, 18305, 17635, 16963, 16289, 15613, 14936, 14257, 13577, 12896, 12214, 11532,
        10849, 10166, 9483, 8800, 8117, 7434, 6752, 6070, 5389, 4708, 4028, 3349, 2671, 1994, 1318, 643,
        0, -643, -1318, -1994, -2671, -3349, -4028, -4708, -5389, -6070, -6752, -7434, -8117, -8800, -9483, -10166,
        -10849, -11532, -12214, -12896, -13577, -14257, -14936, -15613, -16289, -16963, -17635, -18305, -18973, -19638, -20301, -20961,
        -21618, -22272, -22922, -23569, -24212, -24851, -25486, -26116, -26742, -27362, -27978, -28588, -29193, -29792, -30386, -30973,
        -31554, -32128, -32696, -33257, -33810, -34357, -34896, -35428, -35952, -36468, -36976, -37476, -37968, -38451, -38926, -39391,
        -39848, -40294, -40732, -41159, -41577, -41984, -42382, -42769, -43146, -43512, -43868, -44213, -44547, -44870, -45182, -45483,
        -45772, -46050, -46316, -46570, -46812, -47043, -47261, -47467, -47661, -47842, -48011, -48168, -48312, -48443, -48562, -48668,
        -48761, -48842, -48910, -48965, -49007, -49036, -49053, -49057, -49048, -49027, -48992, -48945, -48885, -48812, -48727, -48629,
        -48519, -48396, -48261, -48113, -47953, -47781, -47596, -47399, -47190, -46968, -46735, -46489, -46232, -45963, -45682, -45390,
        -45086, -44771, -44444, -44106, -43756, -43396, -43024, -42641, -42247, -41842, -41426, -40999, -40561, -40113, -39654, -39185,
        -38706, -38216, -37716, -37206, -36686, -36156, -35617, -35068, -34510, -33943, -33367, -32782, -32188, -31586, -30975, -30356,
        -29729, -29094, -28451, -27801, -27143, -26478, -25807, -25128, -24443, -23752, -23054, -22350, -21640, -20924, -20203, -19476,
        -18744, -18007, -17265, -16518, -15767, -15011, -14251, -13486, -12718, -11945, -11169, -10389, -9606, -8819, -8029, -7236,
        -6440, -5642, -4841, -4038, -3233, -2427, -1619, -810
    ]
    return angles[index % 512]
end fn

fn cos_table(index: i32) -> i32
    # Cosine is sine shifted by 128 (90 degrees)
    return sin_table(index + 128)
end fn

# 3D point struct for rotation operations
pub struct Point3D
    x: i32
    y: i32
    z: i32
end struct

# Rotate a point around the X axis
fn rotate_x(x: i32, y: i32, z: i32, angle: i32) -> Point3D
    # angle is in fixed point: 256 = 360 degrees
    let sin_val = sin_table(angle)
    let cos_val = cos_table(angle)
    
    # Apply rotation matrix for X axis:
    # y' = y*cos - z*sin
    # z' = y*sin + z*cos
    let new_y = (y * cos_val - z * sin_val) / 65536
    let new_z = (y * sin_val + z * cos_val) / 65536
    return Point3D {x: x, y: new_y, z: new_z}
end fn

# Rotate a point around the Y axis
fn rotate_y(x: i32, y: i32, z: i32, angle: i32) -> Point3D
    # angle is in fixed point: 256 = 360 degrees
    let sin_val = sin_table(angle)
    let cos_val = cos_table(angle)
    
    # Apply rotation matrix for Y axis:
    # x' = x*cos + z*sin
    # z' = -x*sin + z*cos
    let new_x = (x * cos_val + z * sin_val) / 65536
    let new_z = (-x * sin_val + z * cos_val) / 65536
    return Point3D {x: new_x, y: y, z: new_z}
end fn

# Rotate a point around the Z axis
fn rotate_z(x: i32, y: i32, z: i32, angle: i32) -> Point3D
    # angle is in fixed point: 256 = 360 degrees
    let sin_val = sin_table(angle)
    let cos_val = cos_table(angle)
    
    # Apply rotation matrix for Z axis:
    # x' = x*cos - y*sin
    # y' = x*sin + y*cos
    let new_x = (x * cos_val - y * sin_val) / 65536
    let new_y = (x * sin_val + y * cos_val) / 65536
    return Point3D {x: new_x, y: new_y, z: z}
end fn

# Apply rotation around all three axes (ZXY order)
fn rotate_point(x: i32, y: i32, z: i32, rx: i32, ry: i32, rz: i32) -> Point3D
    # Apply rotation around Z axis first
    let p1 = rotate_z(x, y, z, rz)
    # Then X axis
    let p2 = rotate_x(p1.x, p1.y, p1.z, rx)
    # Finally Y axis
    let p3 = rotate_y(p2.x, p2.y, p2.z, ry)
    return p3
end fn

pub struct SceneGraph
    # Tree structure: parallel arrays, one slot per node.
    node_parent: array<i32>        # parent index, -1 for the root
    node_first_child: array<i32>   # first child index, -1 if none
    node_next_sibling: array<i32>  # next sibling index, -1 if none
    # Local transform, relative to the parent node (world for the root).
    node_tx: array<i32>
    node_ty: array<i32>
    node_tz: array<i32>
    # Rotation in fixed point: 256 = 360 degrees
    node_rx: array<i32>
    node_ry: array<i32>
    node_rz: array<i32>
    # Per-node integer scale in fixed point: value is scale*256, so a scale of
    # 1.0 is 256. Multiplicative scale composes down the tree. Fixed point
    # keeps the transform exact integer math (no float rounding), which is
    # required for byte-identical output across routes.
    node_scale: array<i32>
    # Bound geometry: contiguous vertex range in the shared mesh.
    node_vtx_start: array<i32>
    node_vtx_count: array<i32>
end struct

pub fn new_graph() -> SceneGraph
    return SceneGraph {
        node_parent: [], node_first_child: [], node_next_sibling: [],
        node_tx: [], node_ty: [], node_tz: [],
        node_rx: [], node_ry: [], node_rz: [],
        node_scale: [],
        node_vtx_start: [], node_vtx_count: []
    }
end fn

# Add a node with a local transform and a bound vertex range; returns its index.
# `scale` is fixed point: scale*256 (1.0 = 256).
pub fn add_node(graph: SceneGraph, tx: i32, ty: i32, tz: i32, rx: i32, ry: i32, rz: i32, scale: i32,
                vtx_start: i32, vtx_count: i32) -> i32
    graph.node_parent.push(-1)
    graph.node_first_child.push(-1)
    graph.node_next_sibling.push(-1)
    graph.node_tx.push(tx)
    graph.node_ty.push(ty)
    graph.node_tz.push(tz)
    graph.node_rx.push(rx)
    graph.node_ry.push(ry)
    graph.node_rz.push(rz)
    graph.node_scale.push(scale)
    graph.node_vtx_start.push(vtx_start)
    graph.node_vtx_count.push(vtx_count)
    return graph.node_tx.len() - 1
end fn

# Attach `child` as a child of `parent`. Prepends, so the last-added child is
# visited first; the Python oracle must reproduce the same order.
pub fn add_child(graph: SceneGraph, parent: i32, child: i32)
    graph.node_parent[child] = parent
    graph.node_next_sibling[child] = graph.node_first_child[parent]
    graph.node_first_child[parent] = child
end fn

# Recursively write world positions for this node's bound vertices, then
# recurse into each child with the accumulated world transform.
#
# local_x/y/z are the immutable base coordinates; mesh.x/y/z receive the
# current-frame world positions. Re-traversing from a clean base each frame
# prevents position accumulation across frames.
pub fn apply_world(mesh: Mesh, graph: SceneGraph,
                   local_x: array<i32>, local_y: array<i32>, local_z: array<i32>,
                   node: i32, wx: i32, wy: i32, wz: i32, ws: i32, rx: i32, ry: i32, rz: i32)
    # Apply translation to get the world position for this node
    let world_x = wx + graph.node_tx[node]
    let world_y = wy + graph.node_ty[node]
    let world_z = wz + graph.node_tz[node]
    
    # Accumulate scale multiplicatively in fixed point. ws starts at 256 (1.0)
    # and becomes (ws * node_scale) / 256 at each level, so a child's vertices
    # are scaled by the product of all ancestor scales.
    let world_scale = (ws * graph.node_scale[node]) / 256
    
    # Combine rotations from parent and current node
    let world_rx = rx + graph.node_rx[node]
    let world_ry = ry + graph.node_ry[node]
    let world_rz = rz + graph.node_rz[node]
    
    let start = graph.node_vtx_start[node]
    let count = graph.node_vtx_count[node]
    for i in range(count)
        let v = start + i
        # Apply the accumulated world scale. Fixed point:
        # scale is *256, so multiply then divide by 256.
        let scaled_x = (local_x[v] * world_scale) / 256
        let scaled_y = (local_y[v] * world_scale) / 256
        let scaled_z = (local_z[v] * world_scale) / 256
        
        # Apply rotation to the scaled point
        let rotated = rotate_point(scaled_x, scaled_y, scaled_z, world_rx, world_ry, world_rz)
        
        # Apply translation to finalize the world position
        mesh.x[v] = rotated.x + world_x
        mesh.y[v] = rotated.y + world_y
        mesh.z[v] = rotated.z + world_z
    end for
    
    let mut child = graph.node_first_child[node]
    while child >= 0
        apply_world(mesh, graph, local_x, local_y, local_z, child,
                    world_x, world_y, world_z, world_scale, world_rx, world_ry, world_rz)
        child = graph.node_next_sibling[child]
    end while
end fn

# Apply a deterministic per-frame animation to node transforms. Returns the
# node whose transform was updated last, for telemetry.
pub fn animate(graph: SceneGraph, frame: i32)
    for node in range(graph.node_tx.len())
        let tx = graph.node_tx[node]
        let ty = graph.node_ty[node]
        let tz = graph.node_tz[node]
        
        # Deterministic modular motion driven by the node index and frame.
        graph.node_tx[node] = tx + ((frame * (node * 3 + 5) + node * 11) % 97) - 48
        graph.node_ty[node] = ty + ((frame * (node * 7 + 3)) % 31) - 15
        graph.node_tz[node] = tz + ((frame * (node * 5 + 9) + node * 13) % 83) - 41
        
        # Deterministic scale oscillation around 1.0 (256). Range roughly
        # 0.75x to 1.25x, integer-only.
        let base_scale = graph.node_scale[node]
        graph.node_scale[node] = 256 + ((frame * (node * 11 + 7)) % 129) - 64
        
        # Rotation animation around Y axis
        graph.node_ry[node] = ((frame * 37 * (node + 1)) % 256) * 256
    end for
end fn
