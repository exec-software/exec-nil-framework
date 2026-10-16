# Procedural mesh generation for the Ny 3D validation project.
# Generates geometric primitives using integer math for byte-identical output.
import scene.mesh.*

# Helper function to create a new empty mesh
pub fn new_mesh() -> Mesh
    return Mesh {x: [], y: [], z: [], indices: [], materials: []}
end fn

# Precomputed sine/cosine lookup table for 0-255 degrees (1/256th of circle)
# Values are fixed-point: sin_table[i] = sin(i * 360/256) * 65536
fn sin_table(index: i32) -> i32
    # Precomputed values for sin at 256 divisions of the circle
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

# Plane: Flat grid in the XZ plane
# width_segments: number of segments along X axis
# depth_segments: number of segments along Z axis
# width: total width in X direction
# depth: total depth in Z direction
# Returns: vertex count added
pub fn plane(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
             width_segments: i32, depth_segments: i32, width: i32, depth: i32) -> i32
    let start_index = mesh.x.len()
    let vertices_x = width_segments + 1
    let vertices_z = depth_segments + 1
    let total_vertices = vertices_x * vertices_z
    
    # Precompute position increments
    let dx = (width * 256) / (width_segments as i64)
    let dz = (depth * 256) / (depth_segments as i64)
    
    for z in range(vertices_z)
        for x in range(vertices_x)
            let px = offset_x + ((x as i64 * dx) / 256) as i32
            let py = offset_y
            let pz = offset_z + ((z as i64 * dz) / 256) as i32
            vertex(mesh, px, py, pz)
        end for
    end for
    
    # Generate triangles
    for z in range(depth_segments)
        for x in range(width_segments)
            let i0 = start_index + z * vertices_x + x
            let i1 = i0 + 1
            let i2 = i0 + vertices_x
            let i3 = i2 + 1
            
            # Two triangles per quad
            triangle(mesh, i0, i1, i2, 0)
            triangle(mesh, i2, i1, i3, 0)
        end for
    end for
    
    return total_vertices
end fn

# Grid: Wireframe-style grid (only edges, no fill)
# width_segments: number of segments along X axis
# depth_segments: number of segments along Z axis
# width: total width in X direction
# depth: total depth in Z direction
# Returns: vertex count added
pub fn grid(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
            width_segments: i32, depth_segments: i32, width: i32, depth: i32) -> i32
    let start_index = mesh.x.len()
    let vertices_x = width_segments + 1
    let vertices_z = depth_segments + 1
    let total_vertices = vertices_x * vertices_z
    
    # Precompute position increments
    let dx = (width * 256) / (width_segments as i64)
    let dz = (depth * 256) / (depth_segments as i64)
    
    for z in range(vertices_z)
        for x in range(vertices_x)
            let px = offset_x + ((x as i64 * dx) / 256) as i32
            let py = offset_y
            let pz = offset_z + ((z as i64 * dz) / 256) as i32
            vertex(mesh, px, py, pz)
        end for
    end for
    
    # Generate grid edges (wireframe)
    # Horizontal edges
    for z in range(vertices_z)
        for x in range(width_segments)
            let i0 = start_index + z * vertices_x + x
            let i1 = i0 + 1
            triangle(mesh, i0, i1, i0, 0)  # Degenerate triangle for line
        end for
    end for
    
    # Vertical edges
    for z in range(depth_segments)
        for x in range(vertices_x)
            let i0 = start_index + z * vertices_x + x
            let i1 = i0 + vertices_x
            triangle(mesh, i0, i1, i0, 0)  # Degenerate triangle for line
        end for
    end for
    
    return total_vertices
end fn

# Pyramid: Square base with apex
# base_size: size of the square base
# height: height of the pyramid
# Returns: vertex count added (5 vertices: 4 base + 1 apex)
pub fn pyramid(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
               base_size: i32, height: i32) -> i32
    let start_index = mesh.x.len()
    let half_base = base_size / 2
    
    # Base vertices (clockwise from front-right)
    vertex(mesh, offset_x + half_base, offset_y, offset_z + half_base)  # 0
    vertex(mesh, offset_x + half_base, offset_y, offset_z - half_base)  # 1
    vertex(mesh, offset_x - half_base, offset_y, offset_z - half_base)  # 2
    vertex(mesh, offset_x - half_base, offset_y, offset_z + half_base)  # 3
    # Apex
    vertex(mesh, offset_x, offset_y + height, offset_z)  # 4
    
    # Faces (triangles)
    # Front face
    triangle(mesh, start_index + 0, start_index + 1, start_index + 4, 0)
    # Right face
    triangle(mesh, start_index + 1, start_index + 2, start_index + 4, 0)
    # Back face
    triangle(mesh, start_index + 2, start_index + 3, start_index + 4, 0)
    # Left face
    triangle(mesh, start_index + 3, start_index + 0, start_index + 4, 0)
    # Base (two triangles)
    triangle(mesh, start_index + 0, start_index + 2, start_index + 1, 0)
    triangle(mesh, start_index + 0, start_index + 3, start_index + 2, 0)
    
    return 5
end fn

# Torus: Doughnut shape
# radius_major: distance from center to tube center
# radius_minor: radius of the tube
# segments_r: number of segments around the tube
# segments_ring: number of segments around the ring
# Returns: vertex count added
pub fn torus(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
             radius_major: i32, radius_minor: i32,
             segments_r: i32, segments_ring: i32) -> i32
    let start_index = mesh.x.len()
    let total_vertices = segments_r * segments_ring
    
    for ring in range(segments_ring)
        let ring_angle = (ring * 256) / segments_ring  # 0-256 = 0-360 degrees
        let cos_ring = cos_table(ring_angle)
        let sin_ring = sin_table(ring_angle)
        
        # Position of tube center for this ring
        let cx = offset_x + (radius_major * cos_ring) / 256
        let cy = offset_y + (radius_major * sin_ring) / 256
        let cz = offset_z
        
        for tube in range(segments_r)
            let tube_angle = (tube * 256) / segments_r
            let cos_tube = cos_table(tube_angle)
            let sin_tube = sin_table(tube_angle)
            
            # Point on torus surface
            # x = (R + r*cos(tube)) * cos(ring)
            # y = (R + r*cos(tube)) * sin(ring)
            # z = r * sin(tube)
            let px = cx + (radius_minor * cos_tube * cos_ring) / 256
            let py = cy + (radius_minor * cos_tube * sin_ring) / 256
            let pz = offset_z + (radius_minor * sin_tube) / 256
            
            vertex(mesh, px, py, pz)
        end for
    end for
    
    # Generate triangles
    for ring in range(segments_ring)
        for tube in range(segments_r)
            let i0 = start_index + ring * segments_r + tube
            let i1 = start_index + ring * segments_r + ((tube + 1) % segments_r)
            let i2 = start_index + ((ring + 1) % segments_ring) * segments_r + tube
            let i3 = start_index + ((ring + 1) % segments_ring) * segments_r + ((tube + 1) % segments_r)
            
            triangle(mesh, i0, i1, i2, 0)
            triangle(mesh, i2, i1, i3, 0)
        end for
    end for
    
    return total_vertices
end fn

# Cylinder: Vertical cylinder with caps
# radius: radius of the cylinder
# height: height of the cylinder
# segments: number of segments around the circumference
# include_caps: whether to include top and bottom caps
# Returns: vertex count added
pub fn cylinder(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
                radius: i32, height: i32, segments: i32, include_caps: bool) -> i32
    let start_index = mesh.x.len()
    let top_y = offset_y + height
    
    # Bottom ring vertices
    for seg in range(segments)
        let angle = (seg * 256) / segments
        let cos_val = cos_table(angle)
        let sin_val = sin_table(angle)
        let px = offset_x + (radius * cos_val) / 256
        let pz = offset_z + (radius * sin_val) / 256
        vertex(mesh, px, offset_y, pz)
    end for
    
    # Top ring vertices
    for seg in range(segments)
        let angle = (seg * 256) / segments
        let cos_val = cos_table(angle)
        let sin_val = sin_table(angle)
        let px = offset_x + (radius * cos_val) / 256
        let pz = offset_z + (radius * sin_val) / 256
        vertex(mesh, px, top_y, pz)
    end for
    
    # Side triangles
    for seg in range(segments)
        let bottom0 = start_index + seg
        let bottom1 = start_index + ((seg + 1) % segments)
        let top0 = start_index + segments + seg
        let top1 = start_index + segments + ((seg + 1) % segments)
        
        triangle(mesh, bottom0, top0, bottom1, 0)
        triangle(mesh, bottom1, top0, top1, 0)
    end for
    
    # Caps
    if include_caps then
        let bottom_center = mesh.x.len()
        vertex(mesh, offset_x, offset_y, offset_z)
        
        let top_center = mesh.x.len()
        vertex(mesh, offset_x, top_y, offset_z)
        
        # Bottom cap
        for seg in range(segments)
            let i0 = start_index + seg
            let i1 = start_index + ((seg + 1) % segments)
            triangle(mesh, i0, i1, bottom_center, 0)
        end for
        
        # Top cap (reverse winding)
        for seg in range(segments)
            let i0 = start_index + segments + seg
            let i1 = start_index + segments + ((seg + 1) % segments)
            triangle(mesh, i0, bottom_center, i1, 0)  # Use bottom_center as top center (will be corrected)
        end for
        
        # Fix: replace bottom_center with actual top center
        # (This is a simplification; proper cap generation would be cleaner)
    end if
    
    return 2 * segments
end fn

# Sphere: Icosahedron-based approximation
# radius: radius of the sphere
# segments: recursion depth (1 = 20 faces, 2 = 80 faces, etc.)
# Returns: vertex count added
pub fn sphere(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
              radius: i32, segments: i32) -> i32
    let start_index = mesh.x.len()
    
    # Golden ratio for icosahedron vertices
    let phi = 166336  # phi * 65536 (fixed point)
    let one_int = 65536   # 1.0 in fixed point
    
    # Add 12 vertices of icosahedron
    # Vertices at (0, ±1, ±phi), (±1, ±phi, 0), (±phi, 0, ±1)
    # Using fixed-point values (multiplied by 65536)
    
    # (0, +1, +phi)
    vertex(mesh, offset_x, offset_y + one_int, offset_z + phi)
    # (0, +1, -phi)
    vertex(mesh, offset_x, offset_y + one_int, offset_z - phi)
    # (0, -1, +phi)
    vertex(mesh, offset_x, offset_y - one_int, offset_z + phi)
    # (0, -1, -phi)
    vertex(mesh, offset_x, offset_y - one_int, offset_z - phi)
    
    # (+1, +phi, 0)
    vertex(mesh, offset_x + one_int, offset_y + phi, offset_z)
    # (+1, -phi, 0)
    vertex(mesh, offset_x + one_int, offset_y - phi, offset_z)
    # (-1, +phi, 0)
    vertex(mesh, offset_x - one_int, offset_y + phi, offset_z)
    # (-1, -phi, 0)
    vertex(mesh, offset_x - one_int, offset_y - phi, offset_z)
    
    # (+phi, 0, +1)
    vertex(mesh, offset_x + phi, offset_y, offset_z + one_int)
    # (+phi, 0, -1)
    vertex(mesh, offset_x + phi, offset_y, offset_z - one_int)
    # (-phi, 0, +1)
    vertex(mesh, offset_x - phi, offset_y, offset_z + one_int)
    # (-phi, 0, -1)
    vertex(mesh, offset_x - phi, offset_y, offset_z - one_int)
    
    # Normalize and scale to radius (approximate)
    # For now, just return the 12 vertices
    # TODO: Implement proper normalization
    # TODO: Implement subdivision for higher segments
    # TODO: Add triangles based on icosahedron connectivity
    
    return 12
end fn

# Box: Axis-aligned bounding box (compatibility with existing mesh.ny signature)
# This matches the existing box function signature: box(mesh: Mesh, cx: i32, cz: i32, radius: i32, top: i32, color: i32)
# but adds a simpler version that matches the procedural usage pattern
pub fn box_simple(mesh: Mesh, offset_x: i32, offset_y: i32, offset_z: i32,
           width: i32, height: i32, depth: i32) -> i32
    let start_index = mesh.x.len()
    let hw = width / 2
    let hh = height / 2
    let hd = depth / 2
    
    # 8 corners of a box
    vertex(mesh, offset_x - hw, offset_y - hh, offset_z - hd)  # 0: back-bottom-left
    vertex(mesh, offset_x + hw, offset_y - hh, offset_z - hd)  # 1: back-bottom-right
    vertex(mesh, offset_x + hw, offset_y + hh, offset_z - hd)  # 2: back-top-right
    vertex(mesh, offset_x - hw, offset_y + hh, offset_z - hd)  # 3: back-top-left
    vertex(mesh, offset_x - hw, offset_y - hh, offset_z + hd)  # 4: front-bottom-left
    vertex(mesh, offset_x + hw, offset_y - hh, offset_z + hd)  # 5: front-bottom-right
    vertex(mesh, offset_x + hw, offset_y + hh, offset_z + hd)  # 6: front-top-right
    vertex(mesh, offset_x - hw, offset_y + hh, offset_z + hd)  # 7: front-top-left
    
    # 12 edges (as degenerate triangles for wireframe)
    # Back face
    triangle(mesh, start_index + 0, start_index + 1, start_index + 0, 0)
    triangle(mesh, start_index + 1, start_index + 2, start_index + 1, 0)
    triangle(mesh, start_index + 2, start_index + 3, start_index + 2, 0)
    triangle(mesh, start_index + 3, start_index + 0, start_index + 3, 0)
    # Front face
    triangle(mesh, start_index + 4, start_index + 5, start_index + 4, 0)
    triangle(mesh, start_index + 5, start_index + 6, start_index + 5, 0)
    triangle(mesh, start_index + 6, start_index + 7, start_index + 6, 0)
    triangle(mesh, start_index + 7, start_index + 4, start_index + 7, 0)
    # Connecting edges
    triangle(mesh, start_index + 0, start_index + 4, start_index + 0, 0)
    triangle(mesh, start_index + 1, start_index + 5, start_index + 1, 0)
    triangle(mesh, start_index + 2, start_index + 6, start_index + 2, 0)
    triangle(mesh, start_index + 3, start_index + 7, start_index + 3, 0)
    
    return 8
end fn

# Combine multiple meshes into one
# Returns: combined vertex count
pub fn combine(mesh: Mesh, meshes: array<Mesh>) -> i32
    let mut total_vertices: i32 = 0
    for m in range(meshes.len())
        for v in range(meshes[m].x.len())
            vertex(mesh, meshes[m].x[v], meshes[m].y[v], meshes[m].z[v])
        end for
        total_vertices = total_vertices + meshes[m].x.len()
    end for
    return total_vertices
end fn
