# Persistent Ny consumer workload: build once, animate indexed geometry, reuse
# four raw frame allocations, retain telemetry, and atomically checkpoint state.
# CLI: [width height frames]. Only the final image is emitted as P3 PPM.
import ny.lang.core
import ny.lang.system
import ny.lang.collections.*
import ny.lang.io.*
import scene.mesh.*
import scene.scene_graph.*

fn dimension(index: i64) -> i32
    match Convert.try_to_i32(arg(index))
        Result.Ok(value) -> return value
        Result.Error(error) -> return -1
    end match
    return -1
end fn

fn copy_coordinates(values: array<i32>) -> array<i32>
    let copy: array<i32> = []
    for index in range(values.len())
        copy.push(values[index])
    end for
    return copy
end fn

# Build the recursive scene graph over the shared mesh:
#   root (terrain, verts 0..289, identity)
#     tower-group-0 (transform-only, animated) -> tower-0 (verts 289..297)
#     tower-group-1 (transform-only, animated) -> tower-1 (verts 297..305)
#     tower-group-2 (transform-only, animated) -> tower-2 (verts 305..313)
# Each frame apply_world recurses root -> group -> tower. The groups carry the
# animated world offsets (the tower verts are already at their baked world
# positions, so groups start at identity); the towers carry no local offset.
fn build_scene_graph() -> SceneGraph
    let graph = new_graph()
    # Scale is fixed point *256 (1.0 = 256).
    # Rotation is in fixed point *256 where 256*256 = 65536 = 360 degrees
    let root = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 0, 289)    # terrain, identity
    let group0 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 289, 8) # tower 0 group (verts 289..297)
    let group1 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 297, 8) # tower 1 group (verts 297..305)
    let group2 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 305, 8) # tower 2 group (verts 305..313)
    let tower0 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 289, 8)
    let tower1 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 297, 8)
    let tower2 = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 305, 8)
    add_child(graph, root, group0)
    add_child(graph, root, group1)
    add_child(graph, root, group2)
    add_child(graph, group0, tower0)
    add_child(graph, group1, tower1)
    add_child(graph, group2, tower2)
    return graph
end fn

# Animate the tower groups with the original per-tower offset formulas:
#   tower k: dx = ((frame*(k+3) + k*17) % 97) - 48
#            dz = ((frame*(k+5) + k*13) % 83) - 41
# The graph recursion then composes these offsets with each tower's local
# transform to produce the world positions.
fn update_scene(mesh: Mesh, graph: SceneGraph,
                base_x: array<i32>, base_y: array<i32>, base_z: array<i32>, frame: i32)
    # Use the new animate function instead of manual updates
    animate(graph, frame)
    
    # Terrain height still animates per-vertex (kept for continuity of the
    # existing digest/fragment history); towers move only through the graph.
    for index in range(289)
        mesh.x[index] = base_x[index]
        mesh.y[index] = base_y[index] + ((frame * 7 + index * 3) & 31) - 15
        mesh.z[index] = base_z[index]
    end for
    # Recursive world-transform composition from the root (identity at root:
    # ws starts at 256 = 1.0).
    apply_world(mesh, graph, base_x, base_y, base_z, 0, 0, 0, 0, 256, 0, 0, 0)
end fn

fn binary_value(result: Result<i64, text>) -> i64
    match result
        Result.Ok(value) -> return value
        Result.Error(message) -> panic(message)
    end match
    return -1
end fn

fn decoded_i32(result: Result<i32, text>, expected: i32) -> bool
    match result
        Result.Ok(value) -> return value == expected
        Result.Error(message) -> return false
    end match
    return false
end fn

fn decoded_u32(result: Result<i64, text>, expected: i64) -> bool
    match result
        Result.Ok(value) -> return value == expected
        Result.Error(message) -> return false
    end match
    return false
end fn

# Exact file layout, all little-endian:
#   10 u32 words: NYSC magic, version, width, height, frame count, final frame,
#                 vertex count, triangle count, final digest, final fragments.
#   vertex count triples of signed i32: x, y, z.
#   frame count triples of u32: frame index, digest, fragments.
#   u32 CRC-32/ISO-HDLC of every preceding byte.
# For this scene the file size is 3800 + 12 * frame count.
fn checkpoint(mesh: Mesh, width: i32, height: i32, frames: i32, digests: array<i64>, fragments: array<i64>) -> i64
    let size: i32 = 44 + mesh.x.len() * 12 + frames * 12
    let limit: i64 = (size - 4) as i64
    let bytes: array<u8> = new_u8(size, 0 as u8)
    let header: array<i64> = [0x4353594E as i64, 1 as i64, width as i64, height as i64,
        frames as i64, (frames - 1) as i64, mesh.x.len() as i64,
        mesh.materials.len() as i64, digests[frames - 1], fragments[frames - 1]]
    let mut cursor: i64 = 0
    for index in range(header.len())
        cursor = binary_value(write_u32_le(bytes, cursor, limit, header[index]))
    end for
    for index in range(mesh.x.len())
        cursor = binary_value(write_i32_le(bytes, cursor, limit, mesh.x[index] as i64))
        cursor = binary_value(write_i32_le(bytes, cursor, limit, mesh.y[index] as i64))
        cursor = binary_value(write_i32_le(bytes, cursor, limit, mesh.z[index] as i64))
    end for
    for frame in range(frames)
        cursor = binary_value(write_u32_le(bytes, cursor, limit, frame as i64))
        cursor = binary_value(write_u32_le(bytes, cursor, limit, digests[frame]))
        cursor = binary_value(write_u32_le(bytes, cursor, limit, fragments[frame]))
    end for
    if cursor != limit then
        return -1
    end if
    let checksum = binary_value(crc32(bytes, 0, limit))
    cursor = binary_value(write_u32_le(bytes, cursor, size as i64, checksum))
    if cursor != (size as i64) then
        return -1
    end if
    match write_bytes_atomic("scene-state.bin", bytes, true)
        Result.Ok(ok) -> if not ok then return -1 end if
        Result.Error(message) -> return -1
    end match
    match read_regular_bytes("scene-state.bin", size as i64)
        Result.Ok(actual) ->
            if actual.len() != size then return -1 end if
            if not decoded_u32(crc32(actual, 0, limit), checksum) then return -1 end if
            if not decoded_u32(read_u32_le(actual, limit, size as i64), checksum) then return -1 end if
            cursor = 0
            for index in range(header.len())
                if not decoded_u32(read_u32_le(actual, cursor, limit), header[index]) then return -1 end if
                cursor = cursor + 4
            end for
            for index in range(mesh.x.len())
                if not decoded_i32(read_i32_le(actual, cursor, limit), mesh.x[index]) then return -1 end if
                if not decoded_i32(read_i32_le(actual, cursor + 4, limit), mesh.y[index]) then return -1 end if
                if not decoded_i32(read_i32_le(actual, cursor + 8, limit), mesh.z[index]) then return -1 end if
                cursor = cursor + 12
            end for
            for frame in range(frames)
                if not decoded_u32(read_u32_le(actual, cursor, limit), frame as i64) then return -1 end if
                if not decoded_u32(read_u32_le(actual, cursor + 4, limit), digests[frame]) then return -1 end if
                if not decoded_u32(read_u32_le(actual, cursor + 8, limit), fragments[frame]) then return -1 end if
                cursor = cursor + 12
            end for
            if cursor != limit then return -1 end if
        Result.Error(message) -> return -1
    end match
    return checksum
end fn

fn main() -> i32
    let mut width: i32 = 128
    let mut height: i32 = 96
    let mut frames: i32 = 12
    if arg_count() == 3 then
        width = dimension(0)
        height = dimension(1)
        frames = dimension(2)
    elseif arg_count() != 0 then
        return 2
    end if
    if width < 8 or width > 2048 or height < 8 or height > 2048 or frames < 1 or frames > 120 then
        return 2
    end if
    let mesh = build_mesh()
    if mesh.x.len() != 313 or mesh.y.len() != 313 or mesh.z.len() != 313 or mesh.indices.len() != 1644 or mesh.materials.len() != 548 then
        return 3
    end if
    let graph = build_scene_graph()
    # Array assignment shares storage; these copies must remain independent.
    let base_x = copy_coordinates(mesh.x)
    let base_y = copy_coordinates(mesh.y)
    let base_z = copy_coordinates(mesh.z)
    let count = width * height
    let rgba: ptr<i32> = allocate<i32>(count)
    let depth: ptr<i64> = allocate<i64>(count)
    let rgb: ptr<u8> = allocate<u8>(count * 3)
    let row_hashes: ptr<i64> = allocate<i64>(height)
    let digests: array<i64> = []
    let fragments: array<i64> = []
    let mut valid = true
    for frame in range(frames)
        update_scene(mesh, graph, base_x, base_y, base_z, frame)
        reset_frame(width, height, rgba, depth)
        let written = render_frame(mesh, width, height, rgba, depth)
        let digest = pack_frame(width, height, rgba, rgb, row_hashes)
        fragments.push(written)
        digests.push(digest)
        if not valid_frame(mesh, count, rgba, rgb) then
            valid = false
        end if
    end for
    let mut checksum: i64 = -1
    if valid then
        checksum = checkpoint(mesh, width, height, frames, digests, fragments)
    end if
    if valid and checksum >= 0 then
        println("P3")
        println("# Ny v1 persistent scene player")
        println("# mesh vertices=313 triangles=548")
        println("# frames=" + Convert.i32_to_text(frames))
        for frame in range(frames)
            println("# frame=" + Convert.i32_to_text(frame) + " fragments=" + Convert.i64_to_text(fragments[frame]) + " digest=" + Convert.i64_to_text(digests[frame]))
        end for
        println("# checkpoint bytes=" + Convert.i32_to_text(3800 + frames * 12) + " crc32=" + Convert.i64_to_text(checksum))
        println("# depth-passing fragments=" + Convert.i64_to_text(fragments[frames - 1]))
        println(width)
        println(height)
        println(255)
        for index in range(count * 3)
            println(rgb[index] as i32)
        end for
        println("# row-hashes fnv32 over packed RGBA")
        for y in range(height)
            println("# " + Convert.i64_to_text(row_hashes[y]))
        end for
    end if
    deallocate(row_hashes)
    deallocate(rgb)
    deallocate(depth)
    deallocate(rgba)
    if not valid then return 3 end if
    return if checksum >= 0 then 0 else 4 end if
end fn
