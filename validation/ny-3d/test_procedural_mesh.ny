import ny.lang.system
import ny.lang.io.*
import scene.mesh.*
import scene.procedural_mesh.*

fn main() -> i32
    println("Testing procedural mesh generation...")
    
    # Create a mesh with procedural primitives
    let mesh = new_mesh()
    
    # Add a plane (terrain-like)
    plane(mesh, 0, 0, 0, 10, 10, 100, 100)
    println("Added plane: " + Convert.i32_to_text(mesh.x.len()) + " vertices")
    
    # Add a pyramid
    pyramid(mesh, 50, 0, 50, 30, 40)
    println("Added pyramid: " + Convert.i32_to_text(mesh.x.len()) + " vertices")
    
    # Add a box (using box_simple which matches our procedural signature)
    box_simple(mesh, -50, 0, -50, 20, 20, 20)
    println("Added box: " + Convert.i32_to_text(mesh.x.len()) + " vertices")
    
    # Print summary
    println("Total vertices: " + Convert.i32_to_text(mesh.x.len()))
    println("Total triangles: " + Convert.i32_to_text(mesh.indices.len() / 3))
    
    # Verify some expected values
    if mesh.x.len() < 10 then
        println("ERROR: Not enough vertices generated")
        return 1
    end if
    
    println("SUCCESS: Procedural mesh generation test passed")
    return 0
end fn
