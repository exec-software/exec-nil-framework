module ny.engine
import console
import mimic_window

# =============================================================================
# Immediate Mode UI System
# =============================================================================
# Renders UI elements directly using mimic_window draw functions.
# This is a minimal bootstrap; full system per spec/16_user_interface.md.

# UI Context - passed to all functions
pub struct UIContext
    window_handle: i64
    cursor_x: i32
    cursor_y: i32
end struct

# Colors (Decimal to avoid VM Hex literal issue)
pub let COLOR_WHITE: i32 = 16777215  # 0xFFFFFF
pub let COLOR_BLACK: i32 = 0
pub let COLOR_RED: i32 = 16711680    # 0xFF0000
pub let COLOR_GREEN: i32 = 65280     # 0x00FF00
pub let COLOR_BLUE: i32 = 255        # 0x0000FF
pub let COLOR_GOLD: i32 = 16766720   # 0xFFD700
pub let COLOR_DARK_RED: i32 = 5570560 # 0x550000

# -----------------------------------------------------------------------------
# Frame Management
# -----------------------------------------------------------------------------

pub fn begin_frame(window_handle: i64) -> UIContext
    return UIContext {
        window_handle: window_handle,
        cursor_x: 10,
        cursor_y: 10
    }
end fn

pub fn end_frame(ctx: ref<UIContext>)
    # No-op for now. Future: flush draw commands.
end fn

# -----------------------------------------------------------------------------
# Primitives
# -----------------------------------------------------------------------------

pub fn text(ctx: ref<UIContext>, content: text, color: i32)
    mimic_window.ml_mimic_draw_text(ctx.window_handle, ctx.cursor_x, ctx.cursor_y, content, color)
    ctx.cursor_y = ctx.cursor_y + 16 # Line height
end fn

pub fn text_at(ctx: UIContext, x: i32, y: i32, content: text, color: i32)
    mimic_window.ml_mimic_draw_text(ctx.window_handle, x, y, content, color)
end fn

pub fn rect(ctx: UIContext, x: i32, y: i32, width: i32, height: i32, color: i32)
    mimic_window.ml_mimic_draw_rect(ctx.window_handle, x, y, width, height, color)
end fn

pub fn fill_rect(ctx: UIContext, x: i32, y: i32, width: i32, height: i32, color: i32)
    mimic_window.ml_mimic_fill_rect(ctx.window_handle, x, y, width, height, color)
end fn

# -----------------------------------------------------------------------------
# Widgets
# -----------------------------------------------------------------------------

# Simple Health Bar (per spec 16, section 2.2)
# Colors hardcoded as literals to bypass VM constant resolution bug
pub fn health_bar(ctx: UIContext, x: i32, y: i32, width: i32, height: i32, current: i32, max: i32)
    # Background (Dark Red)
    fill_rect(ctx, x, y, width, height, 5570560)

    # Foreground (proportional fill - Red)
    if max > 0 then
        let fill_width = (current * width) / max
        fill_rect(ctx, x, y, fill_width, height, 16711680)
    end if

    # Border (Gold)
    rect(ctx, x, y, width, height, 16766720)
end fn

# Button (returns true if hovered - simplified, no click detection yet)
pub fn button(ctx: UIContext, x: i32, y: i32, label: text) -> bool
    let width = 100
    let height = 24

    # Background (Blue)
    fill_rect(ctx, x, y, width, height, 255)
    # Border (White)
    rect(ctx, x, y, width, height, 16777215)

    # Label (White)
    text_at(ctx, x + 4, y + 4, label, 16777215)

    return false # Placeholder - no input integration yet
end fn
