# Ny v1 consumer workload: indexed triangle terrain and three occluding towers.
# CLI: [width height]. Output is kept byte-identical to the original renderer.
import ny.lang.core
import ny.lang.system
import scene.mesh.*

fn dimension(index: i64) -> i32
    match Convert.try_to_i32(arg(index))
        Result.Ok(value) -> return value
        Result.Error(error) -> return -1
    end match
    return -1
end fn

fn main() -> i32
    let mut width: i32 = 128
    let mut height: i32 = 96
    if arg_count() == 2 then
        width = dimension(0)
        height = dimension(1)
    elseif arg_count() != 0 then
        return 2
    end if
    if width < 8 or width > 2048 or height < 8 or height > 2048 then
        return 2
    end if
    let mesh = build_mesh()
    if mesh.x.len() != 313 or mesh.y.len() != 313 or mesh.z.len() != 313 or mesh.indices.len() != 1644 or mesh.materials.len() != 548 then
        return 3
    end if
    let count = width * height
    let rgba: ptr<i32> = allocate<i32>(count)
    let depth: ptr<i64> = allocate<i64>(count)
    let rgb: ptr<u8> = allocate<u8>(count * 3)
    let row_hashes: ptr<i64> = allocate<i64>(height)
    reset_frame(width, height, rgba, depth)
    let fragments = render_frame(mesh, width, height, rgba, depth)
    let frame_digest = pack_frame(width, height, rgba, rgb, row_hashes)
    let valid = valid_frame(mesh, count, rgba, rgb)
    if valid then
        println("P3")
        println("# Ny v1 indexed integer mesh renderer")
        println("# mesh vertices=313 triangles=548")
        println("# depth-passing fragments=" + Convert.i64_to_text(fragments))
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
    return if valid then 0 else 3 end if
end fn
