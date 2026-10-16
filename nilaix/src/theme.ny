module theme
import string_utils
# Theme system for nilaix widgets
# Supports dark and light themes with widget-specific colors

// Theme mode: 0 = dark, 1 = light
let mut current_theme: i32 = 0

pub function set_theme(mode: i32)
    current_theme = mode
end function

pub function get_theme() -> i32
    return current_theme
end function

pub function THEME_DARK() -> i32
    return 0
end function

pub function THEME_LIGHT() -> i32
    return 1
end function

// =========================================================================
// Core colors
// =========================================================================

pub function bg_editor() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x000000
end function

pub function bg_side_bar() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x1a1a1a
end function

pub function border_primary() -> i32
    if current_theme == 1 then return 0xCCCCCC end if
    return 0x333333
end function

pub function text_primary() -> i32
    if current_theme == 1 then return 0x1a1a1a end if
    return 0xffffff
end function

pub function cursor_color() -> i32
    if current_theme == 1 then return 0x000000 end if
    return 0x00ff00
end function

pub function selection_bg() -> i32
    if current_theme == 1 then return 0xADD8E6 end if
    return 0x003366
end function

pub function bg_status_bar() -> i32
    if current_theme == 1 then return 0xE0E0E0 end if
    return 0x1a1a1a
end function

pub function bg_primary() -> i32
    if current_theme == 1 then return 0xF5F5F5 end if
    return 0x000000
end function

pub function text_secondary() -> i32
    if current_theme == 1 then return 0x666666 end if
    return 0xaaaaaa
end function

pub function text_selected() -> i32
    if current_theme == 1 then return 0x0000CC end if
    return 0xffff00
end function

// =========================================================================
// Widget-specific colors
// =========================================================================

pub function button_bg() -> i32
    if current_theme == 1 then return 0xE1E1E1 end if
    return 0x2D2D2D
end function

pub function button_bg_hover() -> i32
    if current_theme == 1 then return 0xD0D0D0 end if
    return 0x3E3E3E
end function

pub function button_bg_active() -> i32
    if current_theme == 1 then return 0xC0C0C0 end if
    return 0x505050
end function

pub function button_border() -> i32
    if current_theme == 1 then return 0xADADAD end if
    return 0x555555
end function

pub function textfield_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E
end function

pub function textfield_border() -> i32
    if current_theme == 1 then return 0xABADB3 end if
    return 0x3F3F46
end function

pub function focus_ring() -> i32
    return 0x0078D7
end function

pub function disabled_bg() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x2A2A2A
end function

pub function disabled_text() -> i32
    if current_theme == 1 then return 0xA0A0A0 end if
    return 0x555555
end function

pub function checkbox_check_color() -> i32
    return 0x0078D7
end function

pub function radio_fill_color() -> i32
    return 0x0078D7
end function

pub function slider_track() -> i32
    if current_theme == 1 then return 0xCCCCCC end if
    return 0x3F3F3F
end function

pub function slider_thumb() -> i32
    return 0x0078D7
end function

pub function slider_track_fill() -> i32
    return 0x0050A0
end function

pub function progress_bg() -> i32
    if current_theme == 1 then return 0xE0E0E0 end if
    return 0x2D2D2D
end function

pub function progress_fill() -> i32
    return 0x0078D7
end function

pub function listbox_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E
end function

pub function listbox_selected_bg() -> i32
    if current_theme == 1 then return 0xCCE8FF end if
    return 0x094771
end function

pub function toolbar_bg() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x252526
end function

pub function panel_bg() -> i32
    if current_theme == 1 then return 0xFAFAFA end if
    return 0x1E1E1E
end function

pub function panel_header_bg() -> i32
    if current_theme == 1 then return 0xE8E8E8 end if
    return 0x333333
end function

pub function menu_bg() -> i32
    if current_theme == 1 then return 0xF2F2F2 end if
    return 0x252526
end function

pub function menu_hover() -> i32
    if current_theme == 1 then return 0xD6EBFF end if
    return 0x094771
end function

pub function dropdown_arrow() -> i32
    if current_theme == 1 then return 0x333333 end if
    return 0xCCCCCC
end function

// =========================================================================
// Text Editor colors
// =========================================================================

pub function editor_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E
end function

pub function editor_gutter_bg() -> i32
    if current_theme == 1 then return 0xF5F5F5 end if
    return 0x252526
end function

pub function editor_line_number() -> i32
    if current_theme == 1 then return 0x858585 end if
    return 0x858585
end function

pub function editor_current_line() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x2A2A2A
end function

// =========================================================================
// Tab Bar colors
// =========================================================================

pub function tab_bar_bg() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x252526
end function

pub function tab_active_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E
end function

pub function tab_inactive_bg() -> i32
    if current_theme == 1 then return 0xE5E5E5 end if
    return 0x2D2D2D
end function

// =========================================================================
// Tree View colors
// =========================================================================

pub function treeview_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E
end function

pub function treeview_selected_bg() -> i32
    if current_theme == 1 then return 0xCCE8FF end if
    return 0x094771
end function

pub function treeview_arrow() -> i32
    if current_theme == 1 then return 0x666666 end if
    return 0xAAAAAA
end function

pub function treeview_indent_guide() -> i32
    if current_theme == 1 then return 0xDDDDDD end if
    return 0x404040
end function

// =========================================================================
// Syntax Highlighting Colors
// =========================================================================

pub function syntax_keyword() -> i32
    if current_theme == 1 then return 0x0000FF end if
    return 0x569CD6  // Blue for keywords
end function

pub function syntax_string() -> i32
    if current_theme == 1 then return 0xA31515 end if
    return 0xCE9178  // Brown/orange for strings
end function

pub function syntax_comment() -> i32
    if current_theme == 1 then return 0x008000 end if
    return 0x6A9955  // Green for comments
end function

pub function syntax_number() -> i32
    if current_theme == 1 then return 0x098658 end if
    return 0xB5CEA8  // Light green for numbers
end function

pub function syntax_operator() -> i32
    if current_theme == 1 then return 0x000000 end if
    return 0xD4D4D4  // Light gray for operators
end function

pub function syntax_function() -> i32
    if current_theme == 1 then return 0x795E26 end if
    return 0xDCDCAA  // Yellow for functions
end function

pub function syntax_type() -> i32
    if current_theme == 1 then return 0x0000FF end if
    return 0x4EC9B0  // Teal for types
end function

pub function syntax_builtin() -> i32
    if current_theme == 1 then return 0x0000FF end if
    return 0xD19A66  // Orange for builtins
end function

pub function syntax_directive() -> i32
    if current_theme == 1 then return 0x800080 end if
    return 0xC586C0  // Purple for directives
end function

pub function editor_line_even() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x1E1E1E  // Default editor background
end function

pub function editor_line_odd() -> i32
    if current_theme == 1 then return 0xF8F8F8 end if
    return 0x252526  // Slightly different for alternating lines
end function

// =========================================================================
// Popup and Dialog Colors
// =========================================================================

pub function popup_bg() -> i32
    if current_theme == 1 then return 0xFFFFFF end if
    return 0x2D2D30
end function

// =========================================================================
// Split Pane Colors
// =========================================================================

pub function splitter_bg() -> i32
    if current_theme == 1 then return 0xE1E1E1 end if
    return 0x3E3E42
end function

// =========================================================================
// Tooltip Colors
// =========================================================================

pub function tooltip_bg() -> i32
    if current_theme == 1 then return 0xFFFFE1 end if
    return 0x2D2D30
end function

pub function tooltip_border() -> i32
    if current_theme == 1 then return 0xCCCCCC end if
    return 0x555555
end function

pub function tooltip_text() -> i32
    if current_theme == 1 then return 0x000000 end if
    return 0xFFFFFF
end function

pub function tooltip_shadow() -> i32
    if current_theme == 1 then return 0x40CCCCCC end if
    return 0x40000000
end function

// =========================================================================
// Dialog Colors
// =========================================================================

pub function dialog_overlay() -> i32
    // Semi-dark overlay for modal dialogs
    if current_theme == 1 then return 0x40808080 end if
    return 0x40000000
end function

pub function dialog_title_bg() -> i32
    if current_theme == 1 then return 0xE8E8E8 end if
    return 0x333333
end function

// =========================================================================
// Scroll Container Colors
// =========================================================================

pub function scrollbar_track() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x1E1E1E
end function

pub function scrollbar_thumb() -> i32
    if current_theme == 1 then return 0xC0C0C0 end if
    return 0x505050
end function

pub function scrollbar_thumb_hover() -> i32
    if current_theme == 1 then return 0xA0A0A0 end if
    return 0x686868
end function

// =========================================================================
// Context Menu Colors
// =========================================================================

pub function menu_background() -> i32
    if current_theme == 1 then return 0xF0F0F0 end if
    return 0x2D2D30
end function

pub function menu_selected() -> i32
    if current_theme == 1 then return 0xC8DCF0 end if
    return 0x094771
end function

pub function menu_text() -> i32
    if current_theme == 1 then return 0x1E1E1E end if
    return 0xCCCCCC
end function

pub function text_selection_bg() -> i32
    if current_theme == 1 then return 0xADD6FF end if
    return 0x264F78
end function
