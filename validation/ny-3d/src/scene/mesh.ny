# Shared indexed scene geometry, integer camera, and reusable raw frame buffers.
module scene.mesh
import ny.lang.core
import ny.lang.system

pub struct Mesh
    x: array<i32>
    y: array<i32>
    z: array<i32>
    indices: array<i32>
    materials: array<i32>
end struct

pub struct Projected
    x: i64
    y: i64
    z: i64
end struct

fn absi(value: i32) -> i32
    return if value < 0 then -value else value end if
end fn

fn abs64(value: i64) -> i64
    return if value < 0 then -value else value end if
end fn

fn lo(a: i32, b: i32) -> i32
    return if a < b then a else b end if
end fn

fn hi(a: i32, b: i32) -> i32
    return if a > b then a else b end if
end fn

pub fn vertex(mesh: Mesh, x: i32, y: i32, z: i32)
    mesh.x.push(x)
    mesh.y.push(y)
    mesh.z.push(z)
end fn

pub fn triangle(mesh: Mesh, a: i32, b: i32, c: i32, material: i32)
    mesh.indices.push(a)
    mesh.indices.push(b)
    mesh.indices.push(c)
    mesh.materials.push(material)
end fn

pub fn quad(mesh: Mesh, a: i32, b: i32, c: i32, d: i32, material: i32)
    triangle(mesh, a, b, c, material)
    triangle(mesh, a, c, d, material)
end fn

fn box(mesh: Mesh, cx: i32, cz: i32, radius: i32, top: i32, color: i32)
    let n = mesh.x.len()
    vertex(mesh, cx - radius, 0, cz - radius)
    vertex(mesh, cx + radius, 0, cz - radius)
    vertex(mesh, cx + radius, 0, cz + radius)
    vertex(mesh, cx - radius, 0, cz + radius)
    vertex(mesh, cx - radius, top, cz - radius)
    vertex(mesh, cx + radius, top, cz - radius)
    vertex(mesh, cx + radius, top, cz + radius)
    vertex(mesh, cx - radius, top, cz + radius)
    let side = (1 << 24) | color
    let roof = (5 << 24) | color
    quad(mesh, n, n+1, n+2, n+3, side)
    quad(mesh, n+4, n+7, n+6, n+5, roof)
    quad(mesh, n, n+4, n+5, n+1, side)
    quad(mesh, n+1, n+5, n+6, n+2, side)
    quad(mesh, n+2, n+6, n+7, n+3, side)
    quad(mesh, n+3, n+7, n+4, n, side)
end fn

pub fn build_mesh() -> Mesh
    # The SDK's native array subset supports scalar elements. Separate public
    # arrays retain indexed geometry without assuming an array<struct> ABI.
    let mesh = Mesh {x: [], y: [], z: [], indices: [], materials: []}
    for j in range(17)
        for i in range(17)
            let noise = ((i * 17) ^ (j * 29) ^ ((i * j) << 1)) & 63
            let altitude = 30 + (16 - absi(i - 8) - absi(j - 8)) * 12 + noise * 2
            vertex(mesh, (i - 8) * 96, altitude, (j - 8) * 96)
        end for
    end for
    for j in range(16)
        for i in range(16)
            let a = j * 17 + i
            let b = a + 1
            let d = a + 17
            let c = d + 1
            let color = if ((i ^ j) & 1) == 0 then 0x58845E else 0x71964B end if
            let material = (3 << 24) | color
            triangle(mesh, a, c, b, material)
            triangle(mesh, a, d, c, material)
        end for
    end for
    box(mesh, -336, -192, 120, 430, 0xE65D52)
    box(mesh, 288, 96, 144, 560, 0x36C6CF)
    box(mesh, -144, 384, 96, 360, 0xF2B84C)
    return mesh
end fn



fn project(mesh: Mesh, index: i32, width: i32, height: i32) -> Projected
    let x = mesh.x[index] as i64
    let y = mesh.y[index] as i64
    let z = mesh.z[index] as i64
    return Projected {
        x: (width as i64) * 8 + (4 * x - 4 * z) * (height as i64) / 640,
        y: (height as i64) * 9 + (2 * x + 2 * z - 6 * y) * (height as i64) / 960,
        z: 3 * x + 4 * y + 3 * z
    }
end fn

fn edge(a: Projected, b: Projected, x: i64, y: i64) -> i64
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)
end fn

fn light(mesh: Mesh, a: i32, b: i32, c: i32) -> i32
    let ux = (mesh.x[b] - mesh.x[a]) as i64
    let uy = (mesh.y[b] - mesh.y[a]) as i64
    let uz = (mesh.z[b] - mesh.z[a]) as i64
    let vx = (mesh.x[c] - mesh.x[a]) as i64
    let vy = (mesh.y[c] - mesh.y[a]) as i64
    let vz = (mesh.z[c] - mesh.z[a]) as i64
    let nx = uy * vz - uz * vy
    let ny = uz * vx - ux * vz
    let nz = ux * vy - uy * vx
    let norm = abs64(nx) + abs64(ny) + abs64(nz)
    if norm == 0 then
        return 95
    end if
    let incidence = -3 * nx + 7 * ny + 5 * nz
    let positive = if incidence > 0 then incidence else 0 as i64 end if
    return 95 + ((positive * 140) / (7 * norm)) as i32
end fn

fn shade(material: i32, illumination: i32, x: i32, y: i32, border: bool) -> i32
    let flags = (material >> 24) & 255
    let mut intensity = illumination
    if (flags & 4) != 0 then
        intensity = hi(intensity, 220)
    end if
    if (flags & 2) != 0 then
        intensity = intensity + ((x ^ y) & 1) * 6
    end if
    if (flags & 1) != 0 and border then
        intensity = intensity * 55 / 100
    end if
    let r = ((material >> 16) & 255) * intensity / 255
    let g = ((material >> 8) & 255) * intensity / 255
    let b = (material & 255) * intensity / 255
    return (255 << 24) | (r << 16) | (g << 8) | b
end fn

pub fn raster(mesh: Mesh, index: i32, width: i32, height: i32, rgba: ptr<i32>, depth: ptr<i64>) -> i64
    let ia = mesh.indices[index * 3]
    let ib = mesh.indices[index * 3 + 1]
    let ic = mesh.indices[index * 3 + 2]
    let a = project(mesh, ia, width, height)
    let mut b = project(mesh, ib, width, height)
    let mut c = project(mesh, ic, width, height)
    let mut area = edge(a, b, c.x, c.y)
    if area == 0 then
        return 0
    end if
    if area < 0 then
        let swap = b
        b = c
        c = swap
        area = -area
    end if
    let min_x = hi(0, lo(a.x as i32, lo(b.x as i32, c.x as i32)) / 16)
    let max_x = lo(width - 1, hi(a.x as i32, hi(b.x as i32, c.x as i32)) / 16)
    let min_y = hi(0, lo(a.y as i32, lo(b.y as i32, c.y as i32)) / 16)
    let max_y = lo(height - 1, hi(a.y as i32, hi(b.y as i32, c.y as i32)) / 16)
    let illumination = light(mesh, ia, ib, ic)
    let material = mesh.materials[index]
    let mut written: i64 = 0
    let mut y = min_y
    while y <= max_y
        let zrow = depth + y * width
        let row = rgba + y * width
        let mut x = min_x
        while x <= max_x
            let px = (x as i64) * 16 + 8
            let py = (y as i64) * 16 + 8
            let w0 = edge(b, c, px, py)
            let w1 = edge(c, a, px, py)
            let w2 = edge(a, b, px, py)
            if w0 >= 0 and w1 >= 0 and w2 >= 0 then
                let z = (w0 * a.z + w1 * b.z + w2 * c.z) / area
                if z > zrow[x] then
                    zrow[x] = z
                    let border = w0 * 32 < area or w1 * 32 < area or w2 * 32 < area
                    *(row + x) = shade(material, illumination, x, y, border)
                    written = written + 1
                end if
            end if
            x = x + 1
        end while
        y = y + 1
    end while
    return written
end fn




pub fn reset_frame(width: i32, height: i32, rgba: ptr<i32>, depth: ptr<i64>)
    for y in range(height)
        let r = 18 + y * 22 / height
        let g = 28 + y * 28 / height
        let b = 48 + y * 36 / height
        let background = (255 << 24) | (r << 16) | (g << 8) | b
        for x in range(width)
            rgba[y * width + x] = background
            depth[y * width + x] = -(1000000000 as i64)
        end for
    end for
end fn

pub fn render_frame(mesh: Mesh, width: i32, height: i32, rgba: ptr<i32>, depth: ptr<i64>) -> i64
    let mut fragments: i64 = 0
    for index in range(mesh.materials.len())
        fragments = fragments + raster(mesh, index, width, height, rgba, depth)
    end for
    return fragments
end fn

pub fn pack_frame(width: i32, height: i32, rgba: ptr<i32>, rgb: ptr<u8>, row_hashes: ptr<i64>) -> i64
    for y in range(height)
        let mut checksum: i64 = 2166136261
        for x in range(width)
            let packed = rgba[y * width + x]
            let destination = rgb + (y * width + x) * 3
            *destination = ((packed >> 16) & 255) as u8
            destination[1] = ((packed >> 8) & 255) as u8
            destination[2] = (packed & 255) as u8
            checksum = ((checksum ^ ((packed as i64) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
        end for
        row_hashes[y] = checksum
    end for
    let mut digest: i64 = 2166136261
    for y in range(height)
        digest = ((digest ^ row_hashes[y]) * 16777619) & 0xFFFFFFFF
    end for
    return digest
end fn

pub fn valid_frame(mesh: Mesh, count: i32, rgba: ptr<i32>, rgb: ptr<u8>) -> bool
    # Check byte packing, alpha, and every index before publishing the image.
    let mut valid = true
    for index in range(count)
        let source = rgb + index * 3
        let restored = (255 << 24) | ((*source as i32) << 16) | ((source[1] as i32) << 8) | (source[2] as i32)
        if (restored ^ rgba[index]) != 0 or ((~rgba[index]) & (255 << 24)) != 0 then
            valid = false
        end if
    end for
    for index in range(mesh.indices.len())
        if mesh.indices[index] < 0 or mesh.indices[index] >= mesh.x.len() then
            valid = false
        end if
    end for
    return valid
end fn
