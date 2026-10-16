module ny.ui.mimic

@interop("ml_mimic")
pub fn ml_mimic_create_window(title: text, width: i32, height: i32) -> i64
    return (0 as i64)
end fn

@interop("ml_mimic")
pub fn ml_mimic_set_title(handle: i64, title: text)
end fn

@interop("ml_mimic")
pub fn ml_mimic_fill_rect(handle: i64, x: i32, y: i32, w: i32, h: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_text(handle: i64, x: i32, y: i32, text_str: text, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_clear(handle: i64, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_rect(handle: i64, x: i32, y: i32, w: i32, h: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_line(handle: i64, x1: i32, y1: i32, x2: i32, y2: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_circle(handle: i64, x: i32, y: i32, radius: i32, color: i32)
end fn

pub struct MimicWindow
    handle: i64
    title: text
    width: i32
    height: i32
end struct

pub fn create(title: text, width: i32, height: i32) -> MimicWindow
    let h = ml_mimic_create_window(title, width, height)
    return MimicWindow {
        handle: h,
        title: title,
        width: width,
        height: height
    }
end fn

impl MimicWindow
    pub fn set_title(mut self, title: text)
        ml_mimic_set_title(self.handle, title)
        self.title = title
    end fn

    pub fn fill_rect(mut self, x: i32, y: i32, w: i32, h: i32, color: i32)
        ml_mimic_fill_rect(self.handle, x, y, w, h, color)
    end fn

    pub fn draw_text(self, x: i32, y: i32, text_str: text, color: i32)
        ml_mimic_draw_text(self.handle, x, y, text_str, color)
    end fn

    pub fn clear(self, color: i32)
        ml_mimic_clear(self.handle, color)
    end fn

    pub fn draw_rect(self, x: i32, y: i32, w: i32, h: i32, color: i32)
        ml_mimic_draw_rect(self.handle, x, y, w, h, color)
    end fn

    pub fn draw_line(self, x1: i32, y1: i32, x2: i32, y2: i32, color: i32)
        ml_mimic_draw_line(self.handle, x1, y1, x2, y2, color)
    end fn

    pub fn draw_circle(self, x: i32, y: i32, radius: i32, color: i32)
        ml_mimic_draw_circle(self.handle, x, y, radius, color)
    end fn

    pub fn fill_circle(self, x: i32, y: i32, radius: i32, color: i32)
        ml_mimic_fill_circle(self.handle, x, y, radius, color)
    end fn

    pub fn draw_pixel(self, x: i32, y: i32, color: i32)
        ml_mimic_draw_pixel(self.handle, x, y, color)
    end fn

    pub fn get_width(self) -> i32
        return ml_mimic_get_width(self.handle)
    end fn

    pub fn get_height(self) -> i32
        return ml_mimic_get_height(self.handle)
    end fn

    pub fn get_mouse_x(self) -> i32
        return mimic_app.get_mouse_x(self.handle)
    end fn

    pub fn get_mouse_y(self) -> i32
        return mimic_app.get_mouse_y(self.handle)
    end fn

    pub fn draw_bitmap(self, bitmap: i64, x: i32, y: i32)
        mimic_app.draw_bmp(self.handle, bitmap, x, y)
    end fn

    # Double Buffering Methods
    pub fn begin_paint(self) -> i64
        return ml_mimic_begin_paint(self.handle)
    end fn

    pub fn end_paint(self)
        ml_mimic_end_paint(self.handle)
    end fn

    pub fn clear_buffer(self, dc: i64, color: i32)
        ml_mimic_clear_buffer(dc, color)
    end fn

    pub fn fill_rect_buffer(self, dc: i64, x: i32, y: i32, w: i32, h: i32, color: i32)
        ml_mimic_fill_rect_buffer(dc, x, y, w, h, color)
    end fn

    pub fn fill_circle_buffer(self, dc: i64, x: i32, y: i32, radius: i32, color: i32)
        ml_mimic_fill_circle_buffer(dc, x, y, radius, color)
    end fn

    pub fn draw_circle_buffer(self, dc: i64, x: i32, y: i32, radius: i32, color: i32)
        ml_mimic_draw_circle_buffer(dc, x, y, radius, color)
    end fn

    pub fn draw_line_buffer(self, dc: i64, x1: i32, y1: i32, x2: i32, y2: i32, color: i32)
        ml_mimic_draw_line_buffer(dc, x1, y1, x2, y2, color)
    end fn

    pub fn draw_text_buffer(self, dc: i64, x: i32, y: i32, text_str: text, color: i32)
        ml_mimic_draw_text_buffer(dc, x, y, text_str, color)
    end fn

    pub fn fill_triangle_buffer(self, dc: i64, x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32)
        ml_mimic_fill_triangle_buffer(dc, x1, y1, x2, y2, x3, y3, color)
    end fn
end impl


@interop("ml_mimic")
pub fn ml_mimic_fill_circle(handle: i64, x: i32, y: i32, radius: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_pixel(handle: i64, x: i32, y: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_get_width(handle: i64) -> i32
    return 0
end fn

@interop("ml_mimic")
pub fn ml_mimic_get_height(handle: i64) -> i32
    return 0
end fn

# Double Buffering Interops
@interop("ml_mimic")
pub fn ml_mimic_begin_paint(handle: i64) -> i64
    return (0 as i64)
end fn

@interop("ml_mimic")
pub fn ml_mimic_end_paint(handle: i64)
end fn

@interop("ml_mimic")
pub fn ml_mimic_clear_buffer(dc: i64, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_fill_rect_buffer(dc: i64, x: i32, y: i32, w: i32, h: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_fill_circle_buffer(dc: i64, x: i32, y: i32, radius: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_circle_buffer(dc: i64, x: i32, y: i32, radius: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_line_buffer(dc: i64, x1: i32, y1: i32, x2: i32, y2: i32, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_draw_text_buffer(dc: i64, x: i32, y: i32, text_str: text, color: i32)
end fn

@interop("ml_mimic")
pub fn ml_mimic_fill_triangle_buffer(dc: i64, x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32)
end fn

