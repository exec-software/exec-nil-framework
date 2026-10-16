# Render procedural mesh to PPM image for validation
import ny.lang.core
import ny.lang.system
import scene.mesh.*
import scene.procedural_mesh.*

fn main() -> i32
    let width = 256
    let height = 192
    
    # Create mesh with procedural primitives
    let mesh = new_mesh()
    
    # Add a terrain plane
    plane(mesh, 0, -50, 0, 10, 10, 200, 200)
    
    # Add a pyramid on top
    pyramid(mesh, 0, 0, 0, 50, 100)
    
    # Add a box offset to the side
    box_simple(mesh, -100, 0, 0, 40, 40, 40)
    
    # Allocate render buffers
    let count = width * height
    let rgba: ptr<i32> = allocate<i32>(count)
    let depth: ptr<i64> = allocate<i64>(count)
    let rgb: ptr<u8> = allocate<u8>(count * 3)
    let row_hashes: ptr<i64> = allocate<i64>(height)
    
    # Render
    reset_frame(width, height, rgba, depth)
    let fragments = render_frame(mesh, width, height, rgba, depth)
    let frame_digest = pack_frame(width, height, rgba, rgb, row_hashes)
    let valid = valid_frame(mesh, count, rgba, rgb)
    
    if valid then
        println("P3")
        println("# Ny v1 procedural mesh renderer")
        println("# mesh vertices=" + Convert.i32_to_text(mesh.x.len()) + " triangles=" + Convert.i32_to_text(mesh.indices.len() / 3))
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
        
        println("Render complete - PPM output above")
    end if
    
    deallocate(row_hashes)
    deallocate(rgb)
    deallocate(depth)
    deallocate(rgba)
    return if valid then 0 else 3 end if
end fn
