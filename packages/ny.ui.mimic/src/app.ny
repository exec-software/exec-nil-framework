module ny.ui.mimic

@interop("ml_mimic")
fn ml_mimic_load_bmp(path: text) -> i64
    return 0 as i64
end fn

@interop("ml_mimic")
fn ml_mimic_draw_bmp(handle: i64, bitmap: i64, x: i32, y: i32)
end fn

@interop("ml_mimic")
fn ml_mimic_init() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_loop() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_poll() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_get_p1() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_get_p2() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_sleep(ms: i32)
end fn

@interop("ml_mimic")
fn ml_mimic_get_ticks() -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_set_title(handle: i64, title: text)
end fn

@interop("ml_mimic")
fn ml_mimic_draw_line(handle: i64, x1: i32, y1: i32, x2: i32, y2: i32, color: i32)
end fn

@interop("ml_mimic")
fn ml_mimic_draw_circle(handle: i64, x: i32, y: i32, radius: i32, color: i32)
end fn

@interop("ml_mimic")
fn ml_mimic_is_key_down(key: i32) -> bool
    return false
end fn

@interop("ml_mimic")
fn ml_mimic_get_mouse_x(handle: i64) -> i32
    return 0
end fn

@interop("ml_mimic")
fn ml_mimic_get_mouse_y(handle: i64) -> i32
    return 0
end fn

pub fn get_mouse_x(handle: i64) -> i32
    return ml_mimic_get_mouse_x(handle)
end fn

pub fn get_mouse_y(handle: i64) -> i32
    return ml_mimic_get_mouse_y(handle)
end fn

pub fn draw_bmp(handle: i64, bitmap: i64, x: i32, y: i32)
    ml_mimic_draw_bmp(handle, bitmap, x, y)
end fn

pub fn is_key_down(key: i32) -> bool
    return ml_mimic_is_key_down(key)
end fn

pub fn sleep(ms: i32)
    ml_mimic_sleep(ms)
end fn

pub fn get_ticks() -> i32
    return ml_mimic_get_ticks()
end fn



pub fn init() -> i32
    return ml_mimic_init()
end fn

pub fn poll() -> i32
    return ml_mimic_poll()
end fn

pub fn get_p1() -> i32
    return ml_mimic_get_p1()
end fn

pub fn get_p2() -> i32
    return ml_mimic_get_p2()
end fn

pub fn unpack_low(packed: i32) -> i32
    let div: i32 = 65536
    let d = packed / div
    let rem = d * div
    return packed - rem
end fn

pub fn unpack_high(packed: i32) -> i32
    let div: i32 = 65536
    return packed / div
end fn

pub fn run()
    ml_mimic_loop()
end fn
