module ny.engine
import ny.lang.core
import engine_math
import engine_camera
import ny.lang.math

# =============================================================================
# Software Rendering Pipeline
# Simple 3D rendering without Vulkan for testing and demos
# =============================================================================

# Framebuffer pixel (RGBA as i32)
pub struct Pixel
    r: i32
    g: i32
    b: i32
    a: i32
end struct

impl Pixel
    pub fn new(r: i32, g: i32, b: i32) -> Pixel
        return Pixel { r: r, g: g, b: b, a: 255 }
    end fn

    pub fn black() -> Pixel
        return Pixel { r: 0, g: 0, b: 0, a: 255 }
    end fn

    pub fn white() -> Pixel
        return Pixel { r: 255, g: 255, b: 255, a: 255 }
    end fn

    pub fn red() -> Pixel
        return Pixel { r: 255, g: 0, b: 0, a: 255 }
    end fn

    pub fn green() -> Pixel
        return Pixel { r: 0, g: 255, b: 0, a: 255 }
    end fn

    pub fn blue() -> Pixel
        return Pixel { r: 0, g: 0, b: 255, a: 255 }
    end fn

    pub fn to_packed(self) -> i32
        return (self.a * 16777216) + (self.r * 65536) + (self.g * 256) + self.b
    end fn
end impl

# Vertex for 3D rendering
pub struct Vertex
    position: math.vector.vec3
    color: Pixel
end struct

impl Vertex
    pub fn new(x: f64, y: f64, z: f64, color: Pixel) -> Vertex
        return Vertex {
            position: math.vector.vec3 { x: x, y: y, z: z },
            color: color
        }
    end fn
end impl

# Simple triangle
pub struct Triangle
    v0: Vertex
    v1: Vertex
    v2: Vertex
end struct

impl Triangle
    pub fn new(v0: Vertex, v1: Vertex, v2: Vertex) -> Triangle
        return Triangle { v0: v0, v1: v1, v2: v2 }
    end fn
end impl

# Render context
pub struct RenderContext
    width: i32
    height: i32
    camera: engine_camera.Camera
    view_projection: engine_math.mat4
end struct

impl RenderContext
    pub fn new(width: i32, height: i32) -> RenderContext
        let cam = engine_camera.Camera.new()
        return RenderContext {
            width: width,
            height: height,
            camera: cam,
            view_projection: cam.view_projection_matrix()
        }
    end fn

    # Update camera and recalculate matrices
    pub fn set_camera(self, cam: engine_camera.Camera) -> RenderContext
        return RenderContext {
            width: self.width,
            height: self.height,
            camera: cam,
            view_projection: cam.view_projection_matrix()
        }
    end fn

    # Project a 3D point to 2D screen coordinates
    pub fn project(self, point: math.vector.vec3) -> math.vector.vec3
        let p = engine_math.vec4 {
            x: point.x,
            y: point.y,
            z: point.z,
            w: 1.0
        }
        let clip = self.view_projection.transform(p)

        # Perspective divide
        if clip.w != 0.0 then
            let ndc_x = clip.x / clip.w
            let ndc_y = clip.y / clip.w
            let ndc_z = clip.z / clip.w

            # Convert NDC (-1 to 1) to screen coordinates
            let screen_x = (ndc_x + 1.0) * 0.5 * (self.width as f64)
            let screen_y = (1.0 - ndc_y) * 0.5 * (self.height as f64)

            return math.vector.vec3 {
                x: screen_x,
                y: screen_y,
                z: ndc_z
            }
        else
            return math.vector.vec3.zero()
        end if
    end fn
end impl

# Line clipping (Cohen-Sutherland style, simplified)
pub fn clip_to_screen(x: f64, width: f64) -> f64
    if x < 0.0 then
        return 0.0
    end if
    if x >= width then
        return width - 1.0
    end if
    return x
end fn

# Simple depth buffer value
pub struct DepthValue
    z: f64
    valid: bool
end struct

# =============================================================================
# Rendering Utilities
# =============================================================================

pub fn linear_interpolate(a: f64, b: f64, t: f64) -> f64
    return a + (b - a) * t
end fn

pub fn clamp(value: f64, min: f64, max: f64) -> f64
    if value < min then
        return min
    end if
    if value > max then
        return max
    end if
    return value
end fn

pub fn clamp_i32(value: i32, min: i32, max: i32) -> i32
    if value < min then
        return min
    end if
    if value > max then
        return max
    end if
    return value
end fn

# =============================================================================
# ASCII Buffer for Wireframe Rendering
# =============================================================================

pub struct AsciiBuffer
    width: i32
    height: i32
    pixels: ptr<i32>  # Stores char codes
end struct

impl AsciiBuffer
    pub fn new(w: i32, h: i32) -> AsciiBuffer
        let size = w * h
        let p: ptr<i32> = allocate<i32>(size)

        # Initialize to space
        let mut i = 0
        while i < size
            p[i] = 32 # Space
            i = i + 1
        end while

        return AsciiBuffer { width: w, height: h, pixels: p }
    end fn

    pub fn destroy(self)
        deallocate(self.pixels)
    end fn

    pub fn clear(self)
        let size = self.width * self.height
        let mut i = 0
        while i < size
            self.pixels[i] = 32
            i = i + 1
        end while
    end fn

    pub fn set_pixel(self, x: i32, y: i32, char_code: i32)
        if x >= 0 then
            if x < self.width then
                if y >= 0 then
                    if y < self.height then
                        self.pixels[y * self.width + x] = char_code
                    end if
                end if
            end if
        end if
    end fn

    pub fn draw_line(self, x0: i32, y0: i32, x1: i32, y1: i32, char_code: i32)
       let dx = math.abs_i32(x1 - x0)
       let dy = -math.abs_i32(y1 - y0)
       let mut sx = -1
       if x0 < x1 then sx = 1 end if
       let mut sy = -1
       if y0 < y1 then sy = 1 end if
       let mut err = dx + dy

       let mut x = x0
       let mut y = y0

       while true
           self.set_pixel(x, y, char_code)
           if x == x1 then
               if y == y1 then break end if
           end if
           let e2 = 2 * err
           if e2 >= dy then
               if x == x1 then break end if
               err = err + dy
               x = x + sx
           end if
           if e2 <= dx then
               if y == y1 then break end if
               err = err + dx
               y = y + sy
           end if
       end while
    end fn

    pub fn print(self)
        let mut y = 0
        while y < self.height
            let mut line: text = ""
            let mut x = 0
            while x < self.width
                let code = self.pixels[y * self.width + x]
                if code == 32 then line = line + " "
                else
                    if code == 46 then line = line + "."
                    else
                        if code == 35 then line = line + "#"
                        else
                             if code == 42 then line = line + "*"
                             else line = line + "?"
                             end if
                        end if
                    end if
                end if
                x = x + 1
            end while
            println(line)
            y = y + 1
        end while
    end fn
end impl
