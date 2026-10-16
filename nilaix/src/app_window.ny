module app_window
# =============================================================================
# app_window.ny — Platform window base class
# =============================================================================
# Concrete apps compose AppWindow and call run() from main().
# The run() loop is provided here.  Lifecycle callbacks are plain functions
# that default to no-ops; callers pass function pointers or subclass and
# shadow the methods directly.
# =============================================================================

import winplat

pub class AppWindow
    handle:  i32
    dc:      i32
    width:   i32
    height:  i32
    title:   text

    pub function get_width(self: AppWindow) -> i32
        return self.width
    end function

    pub function get_height(self: AppWindow) -> i32
        return self.height
    end function
end class
