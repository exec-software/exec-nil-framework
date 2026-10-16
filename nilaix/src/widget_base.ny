module widget_base
# =============================================================================
# widget_base.ny — Widget base class
# =============================================================================
# Base class for UI widgets.  Concrete widgets override draw() and
# handle_event() via their own implementations.
# =============================================================================

import winplat

pub class Widget
    x:       i32
    y:       i32
    w:       i32
    h:       i32
    visible: bool
    enabled: bool

    pub function draw(self: Widget, dc: i32) -> void
    end function

    pub function handle_event(self: Widget, ev: winplat::event) -> bool
        return true
    end function

    pub function set_bounds(self: Widget, nx: i32, ny: i32, nw: i32, nh: i32) -> void
        self.x = nx
        self.y = ny
        self.w = nw
        self.h = nh
    end function

    pub function contains(self: Widget, mx: i32, my: i32) -> bool
        if mx < self.x then return false end if
        if mx >= self.x + self.w then return false end if
        if my < self.y then return false end if
        if my >= self.y + self.h then return false end if
        return true
    end function

    pub function set_visible(self: Widget, v: bool) -> void
        self.visible = v
    end function

    pub function set_enabled(self: Widget, e: bool) -> void
        self.enabled = e
    end function

    pub function get_x(self: Widget) -> i32
        return self.x
    end function

    pub function get_y(self: Widget) -> i32
        return self.y
    end function

    pub function get_w(self: Widget) -> i32
        return self.w
    end function

    pub function get_h(self: Widget) -> i32
        return self.h
    end function
end class
