# Ny v1 consumer workload: deterministic 3D ray tracing with raw typed buffers.
# Run with [width height]. Stdout is a complete ASCII PPM (P3) image.
# CPU ray/sphere geometry, hard shadows, specular light, reflection and fog.
# File.write_bytes is tested separately: its native route is not implemented.
import ny.lang.core
import ny.lang.math
import ny.lang.system

struct V3
    x: f64
    y: f64
    z: f64
end struct

struct Hit
    t: f64
    nx: f64
    ny: f64
    nz: f64
    color: i32
    flags: i32
end struct

fn v(x: f64, y: f64, z: f64) -> V3
    return V3 {x: x, y: y, z: z}
end fn

fn add(a: V3, b: V3) -> V3
    return v(a.x + b.x, a.y + b.y, a.z + b.z)
end fn

fn mul(a: V3, k: f64) -> V3
    return v(a.x * k, a.y * k, a.z * k)
end fn

fn dot(a: V3, b: V3) -> f64
    return a.x * b.x + a.y * b.y + a.z * b.z
end fn

fn normalize(a: V3) -> V3
    return mul(a, 1.0 / sqrt(dot(a, a)))
end fn

fn unpack(packed: i32) -> V3
    return v(((packed >> 16) & 255) as f64, ((packed >> 8) & 255) as f64, (packed & 255) as f64)
end fn

fn channel(value: f64) -> i32
    return clamp(value, 0.0, 255.0) as i32
end fn

fn pack(color: V3) -> i32
    # Opaque RGBA: alpha sets the sign bit, exercising signed shifts/masking.
    return (255 << 24) | (channel(color.x) << 16) | (channel(color.y) << 8) | channel(color.z)
end fn

fn sky(direction: V3) -> V3
    let t = clamp(direction.y * 0.7 + 0.5, 0.0, 1.0)
    return v(26.0 + 52.0 * t, 41.0 + 69.0 * t, 69.0 + 86.0 * t)
end fn

fn sphere(scene: ptr<i64>, index: i32, x: i64, y: i64, z: i64, radius: i64, color: i32, flags: i32)
    let base = scene + index * 8
    *base = x
    base[1] = y
    base[2] = z
    base[3] = radius
    base[4] = color as i64
    base[5] = flags as i64
    base[6] = index as i64
    base[7] = (color as i64) ^ (flags as i64)
end fn

fn nearest(origin: V3, direction: V3, scene: ptr<i64>) -> Hit
    let mut hit = Hit {t: 100000.0, nx: 0.0, ny: 0.0, nz: 0.0, color: 0, flags: 0}
    for index in range(4)
        let data = scene + index * 8
        let center = v((*data as f64) / 1000.0, (data[1] as f64) / 1000.0, (data[2] as f64) / 1000.0)
        let radius = (data[3] as f64) / 1000.0
        let offset = add(origin, mul(center, -1.0))
        let b = dot(offset, direction)
        let c = dot(offset, offset) - radius * radius
        let discriminant = b * b - c
        if discriminant >= 0.0 then
            let distance = -b - sqrt(discriminant)
            if distance > 0.001 and distance < hit.t then
                let normal = mul(add(offset, mul(direction, distance)), 1.0 / radius)
                hit = Hit {t: distance, nx: normal.x, ny: normal.y, nz: normal.z, color: data[4] as i32, flags: data[5] as i32}
            end if
        end if
    end for
    if direction.y < -0.00001 then
        let distance = (-1.0 - origin.y) / direction.y
        if distance > 0.001 and distance < hit.t then
            let point = add(origin, mul(direction, distance))
            let checker = ((floor(point.x) as i32) ^ (floor(point.z) as i32)) & 1
            let checker_color = if checker == 0 then 0xC5D0D8 else 0x304657 end if
            hit = Hit {t: distance, nx: 0.0, ny: 1.0, nz: 0.0, color: checker_color, flags: 0}
        end if
    end if
    return hit
end fn

fn surface(origin: V3, direction: V3, hit: Hit, scene: ptr<i64>) -> V3
    if hit.t >= 100000.0 then
        return sky(direction)
    end if
    let normal = v(hit.nx, hit.ny, hit.nz)
    let point = add(origin, mul(direction, hit.t))
    let light = normalize(v(-0.6, 0.9, -0.5))
    let shadow = nearest(add(point, mul(normal, 0.004)), light, scene)
    let visible = if shadow.t < 100000.0 then 0.18 else 1.0 end if
    let diffuse = max(0.0, dot(normal, light))
    let half_direction = normalize(add(light, mul(direction, -1.0)))
    let specular_base = max(0.0, dot(normal, half_direction))
    # Repeated multiplication keeps the power calculation identical per route.
    let s2 = specular_base * specular_base
    let s4 = s2 * s2
    let s8 = s4 * s4
    let s16 = s8 * s8
    let shine = s16 * s16 * 110.0 * visible
    let diffuse_color = mul(unpack(hit.color), 0.18 + 0.76 * diffuse * visible)
    return add(diffuse_color, v(shine, shine, shine))
end fn

fn trace(origin: V3, direction: V3, scene: ptr<i64>) -> i32
    let hit = nearest(origin, direction, scene)
    let mut color = surface(origin, direction, hit, scene)
    if hit.t < 100000.0 then
        if (hit.flags & 1) != 0 then
            let normal = v(hit.nx, hit.ny, hit.nz)
            let point = add(origin, mul(direction, hit.t))
            let reflected = add(direction, mul(normal, -2.0 * dot(direction, normal)))
            let bounce_origin = add(point, mul(normal, 0.004))
            let bounce_hit = nearest(bounce_origin, reflected, scene)
            let bounce_color = surface(bounce_origin, reflected, bounce_hit, scene)
            color = add(mul(color, 0.76), mul(bounce_color, 0.24))
        end if
        let fog = min(hit.t * 0.018, 0.62)
        color = add(mul(color, 1.0 - fog), mul(v(73.0, 94.0, 124.0), fog))
    end if
    return pack(color)
end fn

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
    let count = width * height
    let scene: ptr<i64> = allocate<i64>(32)
    sphere(scene, 0, -1150, -150, 900, 850, 0xE84848, 1)
    sphere(scene, 1, 950, -350, 100, 650, 0x24BBC4, 1)
    sphere(scene, 2, 350, -450, 2400, 550, 0xF5B84D, 0)
    sphere(scene, 3, -200, 1100, 2300, 400, 0x9C77E0, 1)
    let rgba: ptr<i32> = allocate<i32>(count)
    let rgb: ptr<u8> = allocate<u8>(count * 3)
    let row_hashes: ptr<i64> = allocate<i64>(height)
    let origin = v(0.0, 1.1, -6.0)
    for y in range(height)
        let row = rgba + y * width
        let mut checksum: i64 = 2166136261
        for x in range(width)
            let sx = (((x as f64) + 0.5) / (width as f64) * 2.0 - 1.0) * (width as f64) / (height as f64)
            let sy = 1.0 - ((y as f64) + 0.5) / (height as f64) * 2.0
            let direction = normalize(v(sx, sy - 0.10, 1.5))
            let packed = trace(origin, direction, scene)
            *(row + x) = packed
            let destination = rgb + (y * width + x) * 3
            *destination = ((packed >> 16) & 255) as u8
            destination[1] = ((packed >> 8) & 255) as u8
            destination[2] = (packed & 255) as u8
            # Wide hash uses 32-bit wrap masked after every multiply.
            checksum = ((checksum ^ ((packed as i64) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
        end for
        row_hashes[y] = checksum
    end for
    # Validate every packed pixel against byte serialization before publishing.
    for index in range(count)
        let source = rgb + index * 3
        let restored = (255 << 24) | ((*source as i32) << 16) | ((source[1] as i32) << 8) | (source[2] as i32)
        if (restored ^ rgba[index]) != 0 or ((~rgba[index]) & (255 << 24)) != 0 then
            deallocate(row_hashes)
            deallocate(rgb)
            deallocate(rgba)
            deallocate(scene)
            return 3
        end if
    end for
    println("P3")
    println("# Ny v1 raw-pointer ray tracer")
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
    deallocate(row_hashes)
    deallocate(rgb)
    deallocate(rgba)
    deallocate(scene)
    return 0
end fn
