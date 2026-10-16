# Procedural mesh example: generates terrain with pyramids and towers
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

# Build procedural terrain with towers
fn build_mesh() -> Mesh
    let mesh = new_mesh()
    
    # Generate terrain (a large plane with some height variation)
    let terrain_size = 20
    let terrain_spacing = 4
    let base_y = 0
    
    # Add terrain vertices
    for z in range(terrain_size)
        for x in range(terrain_size)
            let px = x * terrain_spacing - (terrain_size * terrain_spacing) / 2
            let py = base_y + ((x * 7 + z * 3) & 31) - 15  # Simple wave pattern
            let pz = z * terrain_spacing - (terrain_size * terrain_spacing) / 2
            vertex(mesh, px, py, pz)
        end for
    end for
    
    # Add terrain triangles
    let terrain_vertices = terrain_size * terrain_size
    for z in range(terrain_size - 1)
        for x in range(terrain_size - 1)
            let i0 = z * terrain_size + x
            let i1 = i0 + 1
            let i2 = i0 + terrain_size
            let i3 = i2 + 1
            
            triangle(mesh, i0, i1, i2, 0)
            triangle(mesh, i2, i1, i3, 0)
        end for
    end for
    
    # Add pyramids at corners
    let pyramid_offsets = [
        [-60, 20, -60],
        [60, 20, -60],
        [-60, 20, 60],
        [60, 20, 60]
    ]
    
    for p in range(4)
        let px = pyramid_offsets[p][0]
        let py = pyramid_offsets[p][1]
        let pz = pyramid_offsets[p][2]
        
        # Add pyramid vertices
        let start_idx = mesh.x.len()
        let hs = 10  # half-size
        let h = 25   # height
        
        # Base vertices (clockwise from front-right)
        vertex(mesh, px + hs, py, pz + hs)  # 0
        vertex(mesh, px + hs, py, pz - hs)  # 1
        vertex(mesh, px - hs, py, pz - hs)  # 2
        vertex(mesh, px - hs, py, pz + hs)  # 3
        # Apex
        vertex(mesh, px, py + h, pz)  # 4
        
        # Faces
        triangle(mesh, start_idx + 0, start_idx + 1, start_idx + 4, 0)
        triangle(mesh, start_idx + 1, start_idx + 2, start_idx + 4, 0)
        triangle(mesh, start_idx + 2, start_idx + 3, start_idx + 4, 0)
        triangle(mesh, start_idx + 3, start_idx + 0, start_idx + 4, 0)
        # Base
        triangle(mesh, start_idx + 0, start_idx + 2, start_idx + 1, 0)
        triangle(mesh, start_idx + 0, start_idx + 3, start_idx + 2, 0)
    end for
    
    return mesh
end fn

# Build scene graph for animation
fn build_scene_graph() -> SceneGraph
    let graph = new_graph()
    let root = add_node(graph, 0, 0, 0, 0, 0, 0, 256, 0, mesh.x.len() - 1)
    return graph
end fn

fn main() -> i32
    let mesh = build_mesh()
    
    println("Mesh stats:")
    println("  Vertices: " + Convert.i32_to_text(mesh.x.len()))
    println("  Triangles: " + Convert.i32_to_text(mesh.indices.len() / 3))
    
    return 0
end fn
