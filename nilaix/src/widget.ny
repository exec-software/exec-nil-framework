module widget
# Basic UI widgets for nilaix

import theme
import winplat
import char_utils // Import our new char utilities
import string_utils // Import our new string utilities
import syntax_highlighter // Import syntax highlighting system
import code_completion // Import code completion system
import language_server // Import language server for symbol parsing

// Global focus state
let mut focused_widget_id: i32 = -1

// Global tooltip system
let mut active_tooltip: widget_data = create_tooltip(-1, "")
let mut tooltip_hover_widget_id: i32 = -1
let mut tooltip_hover_time: i32 = 0
let mut last_mouse_x: i32 = 0
let mut last_mouse_y: i32 = 0

// Global context menu system
let mut context_menu_visible: bool = false
let mut context_menu_x: i32 = 0
let mut context_menu_y: i32 = 0
let mut context_menu_items: array<text> = [""]
let mut context_menu_actions: array<i32> = [0]
let mut context_menu_selected: i32 = -1

// Global error line highlighting system
let mut error_lines: array<i32> = []
let mut error_line_count: i32 = 0

// ========================================================================
// DEBUG WIDGET SYSTEM - Debug-specific UI components
// ========================================================================

// Global debug state
let mut debug_state: text = "detached"  // detached, running, paused, stopped
let mut debug_active: bool = false
let mut current_debug_line: i32 = 0
let mut current_debug_file: text = ""

// Breakpoint management
let mut breakpoints: array<breakpoint_info> = []
let mut breakpoint_count: i32 = 0

// Call stack and variable data
let mut call_stack: array<call_frame_info> = []
let mut call_stack_count: i32 = 0
let mut variables: array<variable_info> = []
let mut variable_count: i32 = 0

// Debug widget constants
let ID_DEBUG_START: i32 = 1000
let ID_DEBUG_STOP: i32 = 1001
let ID_DEBUG_STEP_INTO: i32 = 1002
let ID_DEBUG_STEP_OVER: i32 = 1003
let ID_DEBUG_STEP_OUT: i32 = 1004
let ID_DEBUG_CONTINUE: i32 = 1005
let ID_DEBUG_CALL_STACK: i32 = 1006
let ID_DEBUG_VARIABLES: i32 = 1007
let ID_DEBUG_BREAKPOINTS: i32 = 1008

// Debug data structures
pub struct breakpoint_info
    id: i32
    file: text
    line: i32
    enabled: bool
    condition: text
    hit_count: i32
end struct

pub struct call_frame_info
    function_name: text
    file: text
    line: i32
    level: i32
end struct

pub struct variable_info
    name: text
    type_name: text
    value: text
    scope: text  // local, parameter, global
    expanded: bool
end struct

pub function set_error_lines(lines: array<i32>, count: i32)
    error_lines = lines
    error_line_count = count
end function

pub function clear_error_lines()
    error_lines = []
    error_line_count = 0
end function

pub function has_error_on_line(line_num: i32) -> bool
    let mut i = 0
    while i < error_line_count do
        if error_lines[i] == line_num then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function error_line_bg() -> i32
    // Semi-transparent red for error line background
    return 0x442222
end function

// Keyboard shortcuts constants
pub function KEY_CTRL_A() -> i32 return 1 end function     // Select All
pub function KEY_CTRL_S() -> i32 return 2 end function     // Save
pub function KEY_CTRL_C() -> i32 return 3 end function     // Copy
pub function KEY_CTRL_V() -> i32 return 4 end function     // Paste
pub function KEY_CTRL_X() -> i32 return 5 end function     // Cut
pub function KEY_CTRL_Z() -> i32 return 6 end function     // Undo
pub function KEY_CTRL_Y() -> i32 return 7 end function     // Redo
pub function KEY_CTRL_F() -> i32 return 8 end function     // Find
pub function KEY_CTRL_G() -> i32 return 9 end function     // Find Next
pub function KEY_CTRL_H() -> i32 return 10 end function    // Replace
pub function KEY_CTRL_W() -> i32 return 11 end function    // Close Tab
pub function KEY_CTRL_T() -> i32 return 12 end function    // New Tab
pub function KEY_F3() -> i32 return 13 end function        // Find Next
pub function KEY_ESC() -> i32 return 14 end function       // Escape

// =========================================================================
// Drag and Drop System
// =========================================================================

// Drag and drop constants
pub function DRAG_TYPE_NONE() -> i32 return 0 end function
pub function DRAG_TYPE_TEXT() -> i32 return 1 end function
pub function DRAG_TYPE_WIDGET() -> i32 return 2 end function
pub function DRAG_TYPE_FILE() -> i32 return 3 end function
pub function DRAG_TYPE_DATA() -> i32 return 4 end function

// Drag and drop state
let mut drag_active: bool = false
let mut drag_widget_id: i32 = -1
let mut drag_type: i32 = DRAG_TYPE_NONE()
let mut drag_text_data: text = ""
let mut drag_start_x: i32 = 0
let mut drag_start_y: i32 = 0
let mut drag_current_x: i32 = 0
let mut drag_current_y: i32 = 0
let mut drag_offset_x: i32 = 0
let mut drag_offset_y: i32 = 0

// Drop zones for widget-to-widget dragging
let mut drop_zones: array<drop_zone> = [create_drop_zone(-1, 0, 0, 0, 0, DRAG_TYPE_NONE(), 0)]
let mut drop_zone_count: i32 = 0

// Drop zone structure
pub struct drop_zone
    widget_id: i32
    x: i32
    y: i32
    width: i32
    height: i32
    accepts_types: i32  // Bitmask of accepted drag types
    active: bool
end struct

// Drag data structure
pub struct drag_data
    drag_type: i32
    data: text
    source_widget_id: i32
    preview_text: text
end struct

pub function get_focused_widget_id() -> i32
    return focused_widget_id
end function

pub function set_focused_widget_id(id: i32)
    focused_widget_id = id
end function

pub function WIDGET_BUTTON() -> i32
    return 0
end function

pub function WIDGET_TEXT_FIELD() -> i32
    return 1
end function

pub function WIDGET_LABEL() -> i32
    return 2
end function

pub function WIDGET_DROPDOWN() -> i32
    return 3
end function

pub function WIDGET_MENU() -> i32
    return 4
end function

pub function WIDGET_CHECKBOX() -> i32
    return 5
end function

pub function WIDGET_RADIOBUTTON() -> i32
    return 6
end function

pub function WIDGET_STATUSBAR() -> i32
    return 7
end function

pub function WIDGET_SLIDER() -> i32
    return 8
end function

pub function WIDGET_PROGRESSBAR() -> i32
    return 9
end function

pub function WIDGET_LISTBOX() -> i32
    return 10
end function

pub function WIDGET_TOOLBAR() -> i32
    return 11
end function

pub function WIDGET_PANEL() -> i32
    return 12
end function

pub function WIDGET_TEXT_EDITOR() -> i32
    return 13
end function

pub function WIDGET_TABBAR() -> i32
    return 14
end function

pub function WIDGET_TREEVIEW() -> i32
    return 15
end function

pub function WIDGET_SCROLL_CONTAINER() -> i32
    return 16
end function

pub function WIDGET_DIALOG() -> i32
    return 17
end function

pub function WIDGET_TOOLTIP() -> i32
    return 18
end function

// Layout constants
pub function LAYOUT_HBOX() -> i32
    return 0
end function

pub function LAYOUT_VBOX() -> i32
    return 1
end function

pub function LAYOUT_GRID() -> i32
    return 2
end function

pub struct layout_data
    layout_type: i32
    x: i32
    y: i32
    width: i32
    height: i32
    rows: i32
    cols: i32
    widgets: text  # comma separated widget ids for simplicity
    padding: i32   # inner padding from edges
    spacing: i32   # spacing between child widgets
end struct

pub struct undo_state
    text: text
    cursor_pos: i32
    selection_start: i32
    selection_end: i32
    selection_active: bool
end struct

pub struct undo_stack
    states: array<undo_state>
    count: i32
    max_size: i32
    current_index: i32
end struct

pub struct widget_data
    widget_type: i32
    x: i32
    y: i32
    width: i32
    height: i32
    text: text
    enabled: bool
    visible: bool
    id: i32
    options: text  # comma separated for dropdown/menu
    selected_index: i32
    checked: bool  # for checkbox/radio
    cursor_pos: i32 # For text input fields
    caret_blink_state: bool # For text input fields
    # Text selection fields for text editor
    selection_start: i32  # Start position of selection
    selection_end: i32    # End position of selection
    selection_active: bool # Whether selection is active
    # Undo/redo system for text editor
    undo_stack: undo_stack
    redo_stack: undo_stack
    # Tooltip fields
    tooltip_text: text
    tooltip_visible: bool
    tooltip_x: i32
    tooltip_y: i32
    tooltip_target_id: i32  # Widget that owns this tooltip
    tooltip_show_time: i32  # For auto-hide timeout
end struct

pub struct updated_widgets
    w1: widget_data
    w2: widget_data
end struct

pub struct updated_four_widgets
    w1: widget_data
    w2: widget_data
    w3: widget_data
    w4: widget_data
end struct

pub function create_hbox(x: i32, y: i32, width: i32, height: i32) -> layout_data
    let l = layout_data {
        layout_type: LAYOUT_HBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        rows: 1,
        cols: 2,
        widgets: "",
        padding: 4,
        spacing: 4
    }
    return l
end function

pub function create_vbox(x: i32, y: i32, width: i32, height: i32) -> layout_data
    let l = layout_data {
        layout_type: LAYOUT_VBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        rows: 2,
        cols: 1,
        widgets: "",
        padding: 4,
        spacing: 4
    }
    return l
end function

pub function create_grid(x: i32, y: i32, width: i32, height: i32, rows: i32, cols: i32) -> layout_data
    let l = layout_data {
        layout_type: LAYOUT_GRID(),
        x: x,
        y: y,
        width: width,
        height: height,
        rows: rows,
        cols: cols,
        widgets: "",
        padding: 4,
        spacing: 4
    }
    return l
end function

pub function create_hbox_with_spacing(x: i32, y: i32, width: i32, height: i32, padding: i32, spacing: i32) -> layout_data
    let l = layout_data {
        layout_type: LAYOUT_HBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        rows: 1,
        cols: 2,
        widgets: "",
        padding: padding,
        spacing: spacing
    }
    return l
end function

pub function create_vbox_with_spacing(x: i32, y: i32, width: i32, height: i32, padding: i32, spacing: i32) -> layout_data
    let l = layout_data {
        layout_type: LAYOUT_VBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        rows: 2,
        cols: 1,
        widgets: "",
        padding: padding,
        spacing: spacing
    }
    return l
end function

pub function add_widget_to_layout(mut l: layout_data, widget_id: i32)
    // Simplified: assume widgets are managed externally
end function

// N-widget layout functions using widget arrays
pub struct widget_array
    items: array<widget_data>
    count: i32
end struct

pub function layout_hbox_n(layout: layout_data, widgets: widget_array) -> widget_array
    // Distribute N widgets horizontally with padding and spacing
    let count = widgets.count
    if count == 0 then return widgets end if
    let inner_width = layout.width - (layout.padding * 2)
    let total_spacing = layout.spacing * (count - 1)
    let widget_width = (inner_width - total_spacing) / count
    let mut result = widget_array { items: [], count: count }
    let mut i = 0
    while i < count do
        let w = widgets.items[i]
        let new_x = layout.x + layout.padding + (i * (widget_width + layout.spacing))
        let new_y = layout.y + layout.padding
        let new_h = layout.height - (layout.padding * 2)
        let updated = widget_data {
            widget_type: w.widget_type,
            x: new_x, y: new_y,
            width: widget_width, height: new_h,
            text: w.text, enabled: w.enabled, visible: w.visible,
            id: w.id, options: w.options,
            selected_index: w.selected_index, checked: w.checked,
            cursor_pos: w.cursor_pos, caret_blink_state: w.caret_blink_state
        }
        result.items = result.items + [updated]
        i = i + 1
    end while
    return result
end function

pub function layout_vbox_n(layout: layout_data, widgets: widget_array) -> widget_array
    // Distribute N widgets vertically with padding and spacing
    let count = widgets.count
    if count == 0 then return widgets end if
    let inner_height = layout.height - (layout.padding * 2)
    let total_spacing = layout.spacing * (count - 1)
    let widget_height = (inner_height - total_spacing) / count
    let mut result = widget_array { items: [], count: count }
    let mut i = 0
    while i < count do
        let w = widgets.items[i]
        let new_x = layout.x + layout.padding
        let new_y = layout.y + layout.padding + (i * (widget_height + layout.spacing))
        let new_w = layout.width - (layout.padding * 2)
        let updated = widget_data {
            widget_type: w.widget_type,
            x: new_x, y: new_y,
            width: new_w, height: widget_height,
            text: w.text, enabled: w.enabled, visible: w.visible,
            id: w.id, options: w.options,
            selected_index: w.selected_index, checked: w.checked,
            cursor_pos: w.cursor_pos, caret_blink_state: w.caret_blink_state
        }
        result.items = result.items + [updated]
        i = i + 1
    end while
    return result
end function

pub function layout_grid_n(layout: layout_data, widgets: widget_array) -> widget_array
    // Arrange N widgets in a grid with rows and cols
    let count = widgets.count
    if count == 0 then return widgets end if
    let inner_width = layout.width - (layout.padding * 2)
    let inner_height = layout.height - (layout.padding * 2)
    let cols = layout.cols
    let rows = layout.rows
    let h_spacing = layout.spacing * (cols - 1)
    let v_spacing = layout.spacing * (rows - 1)
    let cell_width = (inner_width - h_spacing) / cols
    let cell_height = (inner_height - v_spacing) / rows
    let mut result = widget_array { items: [], count: count }
    let mut i = 0
    while i < count do
        let w = widgets.items[i]
        let col = i % cols
        let row = i / cols
        if row >= rows then break end if
        let new_x = layout.x + layout.padding + (col * (cell_width + layout.spacing))
        let new_y = layout.y + layout.padding + (row * (cell_height + layout.spacing))
        let updated = widget_data {
            widget_type: w.widget_type,
            x: new_x, y: new_y,
            width: cell_width, height: cell_height,
            text: w.text, enabled: w.enabled, visible: w.visible,
            id: w.id, options: w.options,
            selected_index: w.selected_index, checked: w.checked,
            cursor_pos: w.cursor_pos, caret_blink_state: w.caret_blink_state
        }
        result.items = result.items + [updated]
        i = i + 1
    end while
    return result
end function

pub function apply_hbox_layout(w1: widget_data, w2: widget_data, x: i32, y: i32, total_width: i32, height: i32) -> updated_widgets
    let half_width = total_width / 2
    let updated_w1 = widget_data {
        widget_type: w1.widget_type,
        x: x,
        y: y,
        width: half_width,
        height: height,
        text: w1.text,
        enabled: w1.enabled,
        visible: w1.visible,
        id: w1.id,
        options: w1.options,
        selected_index: w1.selected_index,
        checked: w1.checked,
        cursor_pos: w1.cursor_pos,
        caret_blink_state: w1.caret_blink_state
    }
    let updated_w2 = widget_data {
        widget_type: w2.widget_type,
        x: x + half_width,
        y: y,
        width: half_width,
        height: height,
        text: w2.text,
        enabled: w2.enabled,
        visible: w2.visible,
        id: w2.id,
        options: w2.options,
        selected_index: w2.selected_index,
        checked: w2.checked,
        cursor_pos: w2.cursor_pos,
        caret_blink_state: w2.caret_blink_state
    }
    return updated_widgets { w1: updated_w1, w2: updated_w2 }
end function

pub function apply_vbox_layout(w1: widget_data, w2: widget_data, x: i32, y: i32, width: i32, total_height: i32) -> updated_widgets
    let half_height = total_height / 2
    let updated_w1 = widget_data {
        widget_type: w1.widget_type,
        x: x,
        y: y,
        width: width,
        height: half_height,
        text: w1.text,
        enabled: w1.enabled,
        visible: w1.visible,
        id: w1.id,
        options: w1.options,
        selected_index: w1.selected_index,
        checked: w1.checked,
        cursor_pos: w1.cursor_pos,
        caret_blink_state: w1.caret_blink_state
    }
    let updated_w2 = widget_data {
        widget_type: w2.widget_type,
        x: x,
        y: y + half_height,
        width: width,
        height: half_height,
        text: w2.text,
        enabled: w2.enabled,
        visible: w2.visible,
        id: w2.id,
        options: w2.options,
        selected_index: w2.selected_index,
        checked: w2.checked,
        cursor_pos: w2.cursor_pos,
        caret_blink_state: w2.caret_blink_state
    }
    return updated_widgets { w1: updated_w1, w2: updated_w2 }
end function

pub function apply_grid_layout(w1: widget_data, w2: widget_data, w3: widget_data, w4: widget_data, x: i32, y: i32, total_width: i32, total_height: i32) -> updated_four_widgets
    let half_width = total_width / 2
    let half_height = total_height / 2
    let updated_w1 = widget_data {
        widget_type: w1.widget_type,
        x: x,
        y: y,
        width: half_width,
        height: half_height,
        text: w1.text,
        enabled: w1.enabled,
        visible: w1.visible,
        id: w1.id,
        options: w1.options,
        selected_index: w1.selected_index,
        checked: w1.checked,
        cursor_pos: w1.cursor_pos,
        caret_blink_state: w1.caret_blink_state
    }
    let updated_w2 = widget_data {
        widget_type: w2.widget_type,
        x: x + half_width,
        y: y,
        width: half_width,
        height: half_height,
        text: w2.text,
        enabled: w2.enabled,
        visible: w2.visible,
        id: w2.id,
        options: w2.options,
        selected_index: w2.selected_index,
        checked: w2.checked,
        cursor_pos: w2.cursor_pos,
        caret_blink_state: w2.caret_blink_state
    }
    let updated_w3 = widget_data {
        widget_type: w3.widget_type,
        x: x,
        y: y + half_height,
        width: half_width,
        height: half_height,
        text: w3.text,
        enabled: w3.enabled,
        visible: w3.visible,
        id: w3.id,
        options: w3.options,
        selected_index: w3.selected_index,
        checked: w3.checked,
        cursor_pos: w3.cursor_pos,
        caret_blink_state: w3.caret_blink_state
    }
    let updated_w4 = widget_data {
        widget_type: w4.widget_type,
        x: x + half_width,
        y: y + half_height,
        width: half_width,
        height: half_height,
        text: w4.text,
        enabled: w4.enabled,
        visible: w4.visible,
        id: w4.id,
        options: w4.options,
        selected_index: w4.selected_index,
        checked: w4.checked,
        cursor_pos: w4.cursor_pos,
        caret_blink_state: w4.caret_blink_state
    }
    return updated_four_widgets { w1: updated_w1, w2: updated_w2, w3: updated_w3, w4: updated_w4 }
end function

// Signal constants
pub function SIGNAL_CLICKED() -> i32
    return 0
end function

pub function SIGNAL_TEXT_CHANGED() -> i32
    return 1
end function

pub function SIGNAL_CHECKED_CHANGED() -> i32
    return 2
end function

pub function SIGNAL_SELECTED_CHANGED() -> i32
    return 3
end function

pub function SIGNAL_KEY_DOWN() -> i32
    return 4
end function

pub function SIGNAL_TAB_CHANGED() -> i32
    return 5
end function

pub function SIGNAL_TAB_CLOSED() -> i32
    return 6
end function

pub function SIGNAL_NODE_TOGGLED() -> i32
    return 7
end function

// Drag and drop signals
pub function SIGNAL_DRAG_STARTED() -> i32
    return 8
end function

pub function SIGNAL_DRAG_ENDED() -> i32
    return 9
end function

pub function SIGNAL_DRAG_ENTER() -> i32
    return 10
end function

pub function SIGNAL_DRAG_EXIT() -> i32
    return 11
end function

pub function SIGNAL_DROPPED() -> i32
    return 12
end function

pub struct signal_data
    widget_id: i32
    signal_type: i32
    data: text  // optional data like new text
end struct

// Signal queue - supports multiple pending signals
let MAX_SIGNALS: i32 = 32
let mut signal_queue: array<signal_data> = []
let mut signal_count: i32 = 0

pub function emit_signal(widget_id: i32, signal_type: i32, data: text)
    if signal_count >= MAX_SIGNALS then
        // Drop oldest signal to make room
        let mut new_queue: array<signal_data> = []
        let mut i = 1
        while i < signal_count do
            new_queue = new_queue + [signal_queue[i]]
            i = i + 1
        end while
        signal_queue = new_queue
        signal_count = signal_count - 1
    end if
    let sig = signal_data { widget_id: widget_id, signal_type: signal_type, data: data }
    signal_queue = signal_queue + [sig]
    signal_count = signal_count + 1
end function

pub function get_pending_signal() -> signal_data
    if signal_count == 0 then
        return signal_data { widget_id: -1, signal_type: -1, data: "" }
    end if
    return signal_queue[0]
end function

pub function pop_signal() -> signal_data
    if signal_count == 0 then
        return signal_data { widget_id: -1, signal_type: -1, data: "" }
    end if
    let sig = signal_queue[0]
    let mut new_queue: array<signal_data> = []
    let mut i = 1
    while i < signal_count do
        new_queue = new_queue + [signal_queue[i]]
        i = i + 1
    end while
    signal_queue = new_queue
    signal_count = signal_count - 1
    return sig
end function

pub function has_pending_signals() -> bool
    return signal_count > 0
end function

pub function get_signal_count() -> i32
    return signal_count
end function

pub function clear_signal()
    signal_queue = []
    signal_count = 0
end function

pub function clear_signals_for_widget(widget_id: i32)
    let mut new_queue: array<signal_data> = []
    let mut new_count: i32 = 0
    let mut i = 0
    while i < signal_count do
        if signal_queue[i].widget_id != widget_id then
            new_queue = new_queue + [signal_queue[i]]
            new_count = new_count + 1
        end if
        i = i + 1
    end while
    signal_queue = new_queue
    signal_count = new_count
end function

pub function insert_char_at_cursor(original_text: text, char_to_add: text, cursor_pos: i32) -> text
    let before_cursor = string_utils.substring(original_text, 0, cursor_pos)
    let after_cursor = string_utils.substring(original_text, cursor_pos, string_utils.length(original_text))
    return before_cursor + char_to_add + after_cursor
end function

// Helper function to convert keycode to text.
// This assumes basic ASCII-like character conversion and string operations are available.
pub function keycode_to_text(keycode: i32) -> text
    return char_utils.to_char(keycode)
end function

pub function create_button(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_BUTTON(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false,
        selection_start: 0,
        selection_end: 0,
        selection_active: false,
        undo_stack: create_undo_stack(1),
        redo_stack: create_undo_stack(1),
        tooltip_text: "",
        tooltip_visible: false,
        tooltip_x: 0,
        tooltip_y: 0,
        tooltip_target_id: id,
        tooltip_show_time: 0
    }
    return w
end function

pub function create_text_field(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_TEXT_FIELD(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: string_utils.length(text),
        caret_blink_state: true
    }
    return w
end function

pub function create_label(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_LABEL(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: false,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_dropdown(x: i32, y: i32, width: i32, height: i32, options: text, selected_index: i32, id: i32) -> widget_data
    # options is comma separated list
    let selected_text = ""  # TODO: extract from options
    let w = widget_data {
        widget_type: WIDGET_DROPDOWN(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: selected_text,
        enabled: true,
        visible: true,
        id: id,
        options: options,
        selected_index: selected_index,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_menu(x: i32, y: i32, width: i32, height: i32, items: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_MENU(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: items,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_checkbox(x: i32, y: i32, width: i32, height: i32, text: text, checked: bool, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_CHECKBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: checked,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_radiobutton(x: i32, y: i32, width: i32, height: i32, text: text, checked: bool, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_RADIOBUTTON(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: checked,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_statusbar(x: i32, y: i32, width: i32, height: i32, text: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_STATUSBAR(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: text,
        enabled: false,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_slider(x: i32, y: i32, width: i32, height: i32, value: i32, id: i32) -> widget_data
    // value: 0-100 percentage, stored in selected_index
    let mut val = value
    if val < 0 then val = 0 end if
    if val > 100 then val = 100 end if
    let w = widget_data {
        widget_type: WIDGET_SLIDER(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: val,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_progressbar(x: i32, y: i32, width: i32, height: i32, value: i32, id: i32) -> widget_data
    // value: 0-100 percentage, stored in selected_index
    let mut val = value
    if val < 0 then val = 0 end if
    if val > 100 then val = 100 end if
    let w = widget_data {
        widget_type: WIDGET_PROGRESSBAR(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: false,
        visible: true,
        id: id,
        options: "",
        selected_index: val,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_listbox(x: i32, y: i32, width: i32, height: i32, items: text, id: i32) -> widget_data
    // items: comma-separated list of items
    let w = widget_data {
        widget_type: WIDGET_LISTBOX(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: items,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_toolbar(x: i32, y: i32, width: i32, height: i32, items: text, id: i32) -> widget_data
    // items: comma-separated button labels
    let w = widget_data {
        widget_type: WIDGET_TOOLBAR(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: items,
        selected_index: -1,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_panel(x: i32, y: i32, width: i32, height: i32, title: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_PANEL(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: title,
        enabled: true,
        visible: true,
        id: id,
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_text_editor(x: i32, y: i32, width: i32, height: i32, content: text, id: i32) -> widget_data
    // Multi-line text editor widget
    // text: multi-line content with \n separators
    // cursor_pos: linear cursor position in text
    // selected_index: scroll offset (top visible line number)
    // options: config flags (e.g., "line_numbers,syntax,breakpoints")
    let w = widget_data {
        widget_type: WIDGET_TEXT_EDITOR(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: content,
        enabled: true,
        visible: true,
        id: id,
        options: "line_numbers,syntax,breakpoints",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_tabbar_closeable(x: i32, y: i32, width: i32, height: i32, tabs: text, id: i32) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_TABBAR(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: tabs,
        selected_index: 0,
        checked: true,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_treeview(x: i32, y: i32, width: i32, height: i32, items: text, id: i32) -> widget_data
    // Tree view widget
    // options: pipe-separated tree items, each formatted as "depth:label"
    //   e.g., "0:Root|1:Child1|1:Child2|2:Grandchild|0:Root2"
    // selected_index: selected item index in flat visible list
    // text: comma-separated indices of expanded nodes
    let w = widget_data {
        widget_type: WIDGET_TREEVIEW(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "0",
        enabled: true,
        visible: true,
        id: id,
        options: items,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_scroll_container(x: i32, y: i32, width: i32, height: i32, content_width: i32, content_height: i32, id: i32) -> widget_data
    // Scroll container widget
    // selected_index: vertical scroll offset (pixels)
    // cursor_pos: horizontal scroll offset (pixels)
    // options: "content_w:X,content_h:Y" for content dimensions
    // text: unused (child widgets managed externally)
    let opts = "content_w:" + string_utils.int_to_text(content_width) + ",content_h:" + string_utils.int_to_text(content_height)
    let w = widget_data {
        widget_type: WIDGET_SCROLL_CONTAINER(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: "",
        enabled: true,
        visible: true,
        id: id,
        options: opts,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_dialog(x: i32, y: i32, width: i32, height: i32, title: text, message: text, id: i32) -> widget_data
    // Dialog widget - modal overlay
    // text: title
    // options: "type:confirm" or "type:info" or "type:input"
    // checked: true = visible/active
    // selected_index: result (0=none, 1=ok, 2=cancel)
    let w = widget_data {
        widget_type: WIDGET_DIALOG(),
        x: x,
        y: y,
        width: width,
        height: height,
        text: title,
        enabled: true,
        visible: false,
        id: id,
        options: "type:confirm,message:" + message,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_info_dialog(title: text, message: text, id: i32) -> widget_data
    // Centered info dialog (OK only)
    let w = widget_data {
        widget_type: WIDGET_DIALOG(),
        x: 300,
        y: 200,
        width: 400,
        height: 200,
        text: title,
        enabled: true,
        visible: false,
        id: id,
        options: "type:info,message:" + message,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function create_confirm_dialog(title: text, message: text, id: i32) -> widget_data
    // Centered confirm dialog (OK + Cancel)
    let w = widget_data {
        widget_type: WIDGET_DIALOG(),
        x: 300,
        y: 200,
        width: 400,
        height: 200,
        text: title,
        enabled: true,
        visible: false,
        id: id,
        options: "type:confirm,message:" + message,
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false
    }
    return w
end function

pub function show_dialog(mut w: widget_data)
    w.visible = true
    w.selected_index = 0
end function

pub function hide_dialog(mut w: widget_data)
    w.visible = false
end function

pub function dialog_get_result(w: widget_data) -> i32
    return w.selected_index
end function

// ScrollContainer helper functions
pub function scroll_container_get_content_width(w: widget_data) -> i32
    // Parse content_w from options
    let idx = string_utils.index_of(w.options, "content_w:")
    if idx < 0 then return w.width end if
    let after = string_utils.substring(w.options, idx + 10, string_utils.length(w.options))
    let comma_idx = string_utils.index_of(after, ",")
    let mut val_str = after
    if comma_idx >= 0 then
        val_str = string_utils.substring(after, 0, comma_idx)
    end if
    return string_utils.text_to_int(val_str)
end function

pub function scroll_container_get_content_height(w: widget_data) -> i32
    // Parse content_h from options
    let idx = string_utils.index_of(w.options, "content_h:")
    if idx < 0 then return w.height end if
    let after = string_utils.substring(w.options, idx + 10, string_utils.length(w.options))
    let comma_idx = string_utils.index_of(after, ",")
    let mut val_str = after
    if comma_idx >= 0 then
        val_str = string_utils.substring(after, 0, comma_idx)
    end if
    return string_utils.text_to_int(val_str)
end function

pub function scroll_container_max_scroll_y(w: widget_data) -> i32
    let content_h = scroll_container_get_content_height(w)
    let max = content_h - w.height
    if max < 0 then return 0 end if
    return max
end function

pub function scroll_container_max_scroll_x(w: widget_data) -> i32
    let content_w = scroll_container_get_content_width(w)
    let max = content_w - w.width
    if max < 0 then return 0 end if
    return max
end function

pub function scroll_container_needs_vscroll(w: widget_data) -> bool
    return scroll_container_get_content_height(w) > w.height
end function

pub function scroll_container_needs_hscroll(w: widget_data) -> bool
    return scroll_container_get_content_width(w) > w.width
end function

// Dialog helper to parse message from options
pub function dialog_get_message(w: widget_data) -> text
    let idx = string_utils.index_of(w.options, "message:")
    if idx < 0 then return "" end if
    return string_utils.substring(w.options, idx + 8, string_utils.length(w.options))
end function

pub function dialog_get_type(w: widget_data) -> text
    let idx = string_utils.index_of(w.options, "type:")
    if idx < 0 then return "info" end if
    let after = string_utils.substring(w.options, idx + 5, string_utils.length(w.options))
    let comma_idx = string_utils.index_of(after, ",")
    if comma_idx >= 0 then
        return string_utils.substring(after, 0, comma_idx)
    end if
    return after
end function

// =========================================================================
// Text Editor Helper Functions
// =========================================================================

pub function editor_get_line_count(content: text) -> i32
    let len = string_utils.length(content)
    if len == 0 then return 1 end if
    let mut count = 1
    let mut i = 0
    while i < len do
        if string_utils.char_at(content, i) == "\n" then
            count = count + 1
        end if
        i = i + 1
    end while
    return count
end function

pub function editor_cursor_to_line(content: text, cursor_pos: i32) -> i32
    let mut line = 0
    let mut i = 0
    while i < cursor_pos do
        if string_utils.char_at(content, i) == "\n" then
            line = line + 1
        end if
        i = i + 1
    end while
    return line
end function

pub function editor_cursor_to_col(content: text, cursor_pos: i32) -> i32
    let mut col = 0
    let mut i = 0
    while i < cursor_pos do
        if string_utils.char_at(content, i) == "\n" then
            col = 0
        else
            col = col + 1
        end if
        i = i + 1
    end while
    return col
end function

pub function editor_line_start(content: text, line: i32) -> i32
    let len = string_utils.length(content)
    if line == 0 then return 0 end if
    let mut current_line = 0
    let mut i = 0
    while i < len do
        if string_utils.char_at(content, i) == "\n" then
            current_line = current_line + 1
            if current_line == line then
                return i + 1
            end if
        end if
        i = i + 1
    end while
    return len
end function

pub function draw_active_tooltip(handle: i32)
    // Draw the active tooltip on top of everything
    draw_widget(handle, active_tooltip)
end function

pub function editor_line_end(content: text, line: i32) -> i32
    let len = string_utils.length(content)
    let start = editor_line_start(content, line)
    let mut i = start
    while i < len do
        if string_utils.char_at(content, i) == "\n" then
            return i
        end if
        i = i + 1
    end while
    return len
end function

pub function editor_get_line(content: text, line: i32) -> text
    let start = editor_line_start(content, line)
    let end_pos = editor_line_end(content, line)
    return string_utils.substring(content, start, end_pos)
end function

pub function editor_visible_lines(height: i32) -> i32
    // Each line is 16px, with 2px padding top
    let line_height = 16
    return (height - 4) / line_height
end function

// =========================================================================
// TreeView Helper Functions
// =========================================================================

pub struct tree_node
    depth: i32
    label: text
    index: i32
end struct

pub struct tree_node_list
    items: array<tree_node>
    count: i32
end struct

pub function treeview_parse_nodes(items_str: text) -> tree_node_list
    // Parse "depth:label" pipe-separated items
    let parts = string_utils.split(items_str, "|")
    let mut result = tree_node_list { items: [], count: 0 }
    let mut i = 0
    while i < parts.count do
        let item = string_utils.trim(parts.items[i])
        let colon_pos = string_utils.index_of(item, ":")
        if colon_pos > 0 then
            let depth_str = string_utils.substring(item, 0, colon_pos)
            let label = string_utils.substring(item, colon_pos + 1, string_utils.length(item))
            // Simple single-digit depth parse
            let mut depth = 0
            if depth_str == "1" then depth = 1
            elseif depth_str == "2" then depth = 2
            elseif depth_str == "3" then depth = 3
            elseif depth_str == "4" then depth = 4
            end if
            let node = tree_node { depth: depth, label: label, index: i }
            result.items = result.items + [node]
            result.count = result.count + 1
        end if
        i = i + 1
    end while
    return result
end function

pub function treeview_is_expanded(expanded_str: text, index: i32) -> bool
    let idx_str = string_utils.int_to_text(index)
    let parts = string_utils.split(expanded_str, ",")
    let mut i = 0
    while i < parts.count do
        if string_utils.trim(parts.items[i]) == idx_str then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function treeview_has_children(nodes: tree_node_list, index: i32) -> bool
    if index + 1 >= nodes.count then return false end if
    return nodes.items[index + 1].depth > nodes.items[index].depth
end function

pub function treeview_toggle_expanded(expanded_str: text, index: i32) -> text
    let idx_str = string_utils.int_to_text(index)
    if treeview_is_expanded(expanded_str, index) then
        // Remove index from expanded list
        let parts = string_utils.split(expanded_str, ",")
        let mut result = ""
        let mut first = true
        let mut i = 0
        while i < parts.count do
            if string_utils.trim(parts.items[i]) != idx_str then
                if not first then result = result + "," end if
                result = result + string_utils.trim(parts.items[i])
                first = false
            end if
            i = i + 1
        end while
        if string_utils.length(result) == 0 then result = "" end if
        return result
    else
        // Add index to expanded list
        if string_utils.length(expanded_str) == 0 then
            return idx_str
        end if
        return expanded_str + "," + idx_str
    end if
end function

pub function treeview_is_visible(nodes: tree_node_list, expanded_str: text, index: i32) -> bool
    // A node is visible if all its ancestors are expanded
    if index == 0 then return true end if
    let node_depth = nodes.items[index].depth
    if node_depth == 0 then return true end if
    // Walk backwards to find parent (first node with depth = node_depth - 1)
    let mut j = index - 1
    while j >= 0 do
        if nodes.items[j].depth == node_depth - 1 then
            // This is the parent - check if it's expanded
            if not treeview_is_expanded(expanded_str, j) then
                return false
            end if
            // Recursively check parent visibility
            return treeview_is_visible(nodes, expanded_str, j)
        end if
        j = j - 1
    end while
    return true
end function

pub function is_point_in_widget(x: i32, y: i32, w: widget_data) -> bool
    return x >= w.x and x <= w.x + w.width and y >= w.y and y <= w.y + w.height
end function

// =========================================================================
// Undo/Redo Helper Functions
// =========================================================================

pub function create_undo_stack(max_size: i32) -> undo_stack
    return undo_stack {
        states: [],
        count: 0,
        max_size: max_size,
        current_index: -1
    }
end function

pub function undo_push_state(mut stack: undo_stack, text: text, cursor_pos: i32, sel_start: i32, sel_end: i32, sel_active: bool)
    // Create new state
    let new_state = undo_state {
        text: text,
        cursor_pos: cursor_pos,
        selection_start: sel_start,
        selection_end: sel_end,
        selection_active: sel_active
    }

    // If we're not at the top, truncate redo history
    if stack.current_index < stack.count - 1 then
        let mut new_states: array<undo_state> = []
        let mut i = 0
        while i <= stack.current_index do
            new_states = new_states + [stack.states[i]]
            i = i + 1
        end while
        stack.states = new_states
        stack.count = stack.current_index + 1
    end if

    // Add new state
    stack.states = stack.states + [new_state]
    stack.count = stack.count + 1
    stack.current_index = stack.count - 1

    // Limit stack size - cap at max_size by adjusting index
    if stack.count > stack.max_size then
        stack.count = stack.max_size
        stack.current_index = stack.count - 1
    end if
end function

pub function undo_can_undo(stack: undo_stack) -> bool
    return stack.current_index > 0
end function

pub function undo_can_redo(stack: undo_stack) -> bool
    return stack.current_index < stack.count - 1
end function

pub function undo_perform_undo(mut w: widget_data) -> bool
    if not undo_can_undo(w.undo_stack) then return false end if

    w.undo_stack.current_index = w.undo_stack.current_index - 1
    let state = w.undo_stack.states[w.undo_stack.current_index]

    w.text = state.text
    w.cursor_pos = state.cursor_pos
    w.selection_start = state.selection_start
    w.selection_end = state.selection_end
    w.selection_active = state.selection_active

    emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
    return true
end function

pub function undo_perform_redo(mut w: widget_data) -> bool
    if not undo_can_redo(w.undo_stack) then return false end if

    w.undo_stack.current_index = w.undo_stack.current_index + 1
    let state = w.undo_stack.states[w.undo_stack.current_index]

    w.text = state.text
    w.cursor_pos = state.cursor_pos
    w.selection_start = state.selection_start
    w.selection_end = state.selection_end
    w.selection_active = state.selection_active

    emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
    return true
end function

pub function undo_save_state(mut w: widget_data)
    undo_push_state(w.undo_stack, w.text, w.cursor_pos, w.selection_start, w.selection_end, w.selection_active)
end function

pub function create_tooltip(target_id: i32, text: text) -> widget_data
    let w = widget_data {
        widget_type: WIDGET_TOOLTIP(),
        x: 0,
        y: 0,
        width: 200,  // Auto-calculated based on text
        height: 30,  // Auto-calculated based on text
        text: text,
        enabled: true,
        visible: false,  // Initially hidden
        id: -1,  // Tooltips don't need unique IDs
        options: "",
        selected_index: 0,
        checked: false,
        cursor_pos: 0,
        caret_blink_state: false,
        selection_start: 0,
        selection_end: 0,
        selection_active: false,
        undo_stack: create_undo_stack(1),
        redo_stack: create_undo_stack(1),
        tooltip_text: text,
        tooltip_visible: false,
        tooltip_x: 0,
        tooltip_y: 0,
        tooltip_target_id: target_id,
        tooltip_show_time: 0
    }
    return w
end function

pub function tooltip_show(mut tooltip: widget_data, x: i32, y: i32)
    tooltip.tooltip_visible = true
    tooltip.tooltip_x = x
    tooltip.tooltip_y = y
    tooltip.tooltip_show_time = 0
    tooltip.visible = true

    // Calculate size based on text
    let text_width = string_utils.length(tooltip.text) * 8 + 16
    let text_height = 20
    tooltip.width = text_width
    tooltip.height = text_height
end function

pub function tooltip_hide(mut tooltip: widget_data)
    tooltip.tooltip_visible = false
    tooltip.visible = false
end function

pub function tooltip_update_position(mut tooltip: widget_data, mouse_x: i32, mouse_y: i32)
    // Position tooltip near mouse but ensure it stays on screen
    let tooltip_width = string_utils.length(tooltip.text) * 8 + 16
    let tooltip_height = 20

    // Default position: below and to the right of mouse
    let mut x = mouse_x + 10
    let mut y = mouse_y + 20

    // TODO: Get screen dimensions to prevent going off-screen
    // For now, just ensure positive coordinates
    if x < 0 then x = 0 end if
    if y < 0 then y = 0 end if

    tooltip.tooltip_x = x
    tooltip.tooltip_y = y
    tooltip.x = x
    tooltip.y = y
    tooltip.width = tooltip_width
    tooltip.height = tooltip_height
end function

pub function tooltip_should_show(tooltip: widget_data, hover_time: i32) -> bool
    // Show tooltip after 500ms of hovering
    if hover_time < 500 then return false end if
    if not tooltip.tooltip_visible then return true end if
    return false
end function

// Global tooltip management functions
pub function process_tooltip_hover(mouse_x: i32, mouse_y: i32, widgets: widget_array)
    // Update last mouse position
    last_mouse_x = mouse_x
    last_mouse_y = mouse_y

    // Check which widget is being hovered
    let mut hovered_widget_id: i32 = -1
    let mut i = 0
    while i < widgets.count do
        let w = widgets.items[i]
        if is_point_in_widget(mouse_x, mouse_y, w) and string_utils.length(w.tooltip_text) > 0 then
            hovered_widget_id = w.id
            break
        end if
        i = i + 1
    end while

    // If we're hovering over a different widget, reset hover time
    if hovered_widget_id != tooltip_hover_widget_id then
        tooltip_hover_widget_id = hovered_widget_id
        tooltip_hover_time = 0
        tooltip_hide(active_tooltip)
    end if

    // If we're hovering over a widget with a tooltip, update hover time
    if hovered_widget_id != -1 then
        tooltip_hover_time = tooltip_hover_time + 16  // Assuming 60 FPS (16ms per frame)

        // Find the widget and get its tooltip text
        i = 0
        while i < widgets.count do
            let w = widgets.items[i]
            if w.id == hovered_widget_id then
                // Check if we should show the tooltip
                if tooltip_should_show(active_tooltip, tooltip_hover_time) then
                    tooltip_update_position(active_tooltip, mouse_x, mouse_y)
                    tooltip_show(active_tooltip, mouse_x, mouse_y)
                    active_tooltip.tooltip_text = w.tooltip_text
                end if
                break
            end if
            i = i + 1
        end while
    else
        // Hide tooltip if not hovering over any widget
        tooltip_hide(active_tooltip)
        tooltip_hover_time = 0
    end if
end function

pub function get_active_tooltip() -> widget_data
    return active_tooltip
end function

pub function set_widget_tooltip(mut w: widget_data, tooltip_text: text)
    w.tooltip_text = tooltip_text
end function

// Convenience functions to create widgets with tooltips
pub function create_button_with_tooltip(x: i32, y: i32, width: i32, height: i32, text: text, tooltip: text, id: i32) -> widget_data
    let w = create_button(x, y, width, height, text, id)
    set_widget_tooltip(w, tooltip)
    return w
end function

pub function create_text_field_with_tooltip(x: i32, y: i32, width: i32, height: i32, text: text, tooltip: text, id: i32) -> widget_data
    let w = create_text_field(x, y, width, height, text, id)
    set_widget_tooltip(w, tooltip)
    return w
end function

pub function create_label_with_tooltip(x: i32, y: i32, width: i32, height: i32, text: text, tooltip: text, id: i32) -> widget_data
    let w = create_label(x, y, width, height, text, id)
    set_widget_tooltip(w, tooltip)
    return w
end function

// =========================================================================
// Statusbar Integration Functions
// =========================================================================

pub function update_statusbar_with_editor_state(mut statusbar: widget_data, editor: widget_data, file_path: text, modified: bool)
    // Build statusbar text with editor information
    let cursor_line = editor_cursor_to_line(editor.text, editor.cursor_pos) + 1  // 1-based
    let cursor_col = editor_cursor_to_col(editor.text, editor.cursor_pos) + 1   // 1-based
    let total_lines = editor_get_line_count(editor.text)

    // Create status components
    let position_info = "Line " + string_utils.int_to_text(cursor_line) +
                       ", Col " + string_utils.int_to_text(cursor_col) +
                       " (" + string_utils.int_to_text(total_lines) + " total)"

    let mut file_info = ""
    if string_utils.length(file_path) > 0 then
        let file_name = get_filename_from_path(file_path)
        file_info = file_name
        if modified then
            file_info = file_info + " •"
        end if
    else
        file_info = "Untitled"
        if modified then
            file_info = file_info + " •"
        end if
    end if

    // Get selected text info
    let mut selection_info = ""
    if editor.selection_active then
        let selected_text = editor_get_selected_text(editor)
        let selection_length = string_utils.length(selected_text)
        if selection_length > 0 then
            selection_info = string_utils.int_to_text(selection_length) + " chars selected"
        end if
    end if

    // Combine all status information
    let mut status_text = file_info
    if string_utils.length(position_info) > 0 then
        if string_utils.length(status_text) > 0 then
            status_text = status_text + " | "
        end if
        status_text = status_text + position_info
    end if

    if string_utils.length(selection_info) > 0 then
        if string_utils.length(status_text) > 0 then
            status_text = status_text + " | "
        end if
        status_text = status_text + selection_info
    end if

    statusbar.text = status_text
end function

pub function get_filename_from_path(file_path: text) -> text
    // Simple filename extraction - find last '/' or '\'
    let mut last_slash = -1
    let mut i = 0
    let path_len = string_utils.length(file_path)

    while i < path_len do
        let ch = string_utils.char_at(file_path, i)
        if ch == "/" or ch == "\\" then
            last_slash = i
        end if
        i = i + 1
    end while

    if last_slash >= 0 then
        return string_utils.substring(file_path, last_slash + 1, path_len)
    end if

    return file_path
end function

pub function update_statusbar_position(mut statusbar: widget_data, line: i32, col: i32, total_lines: i32)
    let position_info = "Line " + string_utils.int_to_text(line + 1) +
                       ", Col " + string_utils.int_to_text(col + 1) +
                       " (" + string_utils.int_to_text(total_lines) + " total)"

    // Append to existing status text
    if string_utils.length(statusbar.text) > 0 then
        statusbar.text = statusbar.text + " | " + position_info
    else
        statusbar.text = position_info
    end if
end function

pub function update_statusbar_file_info(mut statusbar: widget_data, file_path: text, modified: bool)
    let mut file_info = ""
    if string_utils.length(file_path) > 0 then
        let file_name = get_filename_from_path(file_path)
        file_info = file_name
        if modified then
            file_info = file_info + " •"
        end if
    else
        file_info = "Untitled"
        if modified then
            file_info = file_info + " •"
        end if
    end if

    // Prepend to existing status text
    if string_utils.length(statusbar.text) > 0 then
        statusbar.text = file_info + " | " + statusbar.text
    else
        statusbar.text = file_info
    end if
end function

pub function update_statusbar_selection(mut statusbar: widget_data, selection_length: i32)
    if selection_length > 0 then
        let selection_info = string_utils.int_to_text(selection_length) + " chars selected"

        // Append to existing status text
        if string_utils.length(statusbar.text) > 0 then
            statusbar.text = statusbar.text + " | " + selection_info
        else
            statusbar.text = selection_info
        end if
    end if
end function

pub function clear_statusbar(mut statusbar: widget_data)
    statusbar.text = ""
end function

// =========================================================================
// Text Selection Helper Functions
// =========================================================================

pub function editor_set_selection(mut w: widget_data, start_in: i32, end_in: i32)
    let mut start = start_in
    let mut end_pos = end_in
    if start < 0 then start = 0 end if
    if end_pos < 0 then end_pos = 0 end if

    let text_len = string_utils.length(w.text)
    if start > text_len then start = text_len end if
    if end_pos > text_len then end_pos = text_len end if

    if start == end_pos then
        w.selection_active = false
        w.selection_start = 0
        w.selection_end = 0
    else
        w.selection_active = true
        if start <= end_pos then
            w.selection_start = start
            w.selection_end = end_pos
        else
            w.selection_start = end_pos
            w.selection_end = start
        end if
    end if
end function

pub function editor_clear_selection(mut w: widget_data)
    w.selection_active = false
    w.selection_start = 0
    w.selection_end = 0
end function

pub function editor_get_selected_text(w: widget_data) -> text
    if not w.selection_active then
        return ""
    end if

    let mut start = w.selection_start
    let mut end_pos = w.selection_end
    if start > end_pos then
        let temp = start
        start = end_pos
        end_pos = temp
    end if

    return string_utils.substring(w.text, start, end_pos)
end function

pub function editor_select_all(mut w: widget_data)
    w.selection_active = true
    w.selection_start = 0
    w.selection_end = string_utils.length(w.text)
end function

pub function editor_select_line(mut w: widget_data, line: i32)
    let start = editor_line_start(w.text, line)
    let end_pos = editor_line_end(w.text, line)
    editor_set_selection(w, start, end_pos)
end function

pub function editor_select_word_at_cursor(mut w: widget_data)
    let cursor = w.cursor_pos
    let text_len = string_utils.length(w.text)

    // Find word boundaries
    let mut start = cursor
    let mut end_pos = cursor

    // Find start of word (alphanumeric and underscore)
    while start > 0 and (is_word_char(string_utils.char_at(w.text, start - 1))) do
        start = start - 1
    end while

    // Find end of word
    while end_pos < text_len and (is_word_char(string_utils.char_at(w.text, end_pos))) do
        end_pos = end_pos + 1
    end while

    editor_set_selection(w, start, end_pos)
end function

pub function is_word_char(ch: text) -> bool
    if string_utils.length(ch) != 1 then return false end if

    let c = string_utils.char_code(ch)
    // Check if alphanumeric or underscore
    return (c >= 48 and c <= 57) or  // 0-9
           (c >= 65 and c <= 90) or  // A-Z
           (c >= 97 and c <= 122) or // a-z
           (c == 95)                 // underscore
end function

pub function check_widget_click(mouse_x: i32, mouse_y: i32, modifiers: i32, mut w: widget_data) -> bool
    if not w.visible then return false end if
    if not is_point_in_widget(mouse_x, mouse_y, w) then return false end if

    if w.widget_type == WIDGET_BUTTON() then
        if not w.enabled then return false end if
        emit_signal(w.id, SIGNAL_CLICKED(), w.text)
        set_focused_widget_id(-1)
        return true
    elseif w.widget_type == WIDGET_TEXT_FIELD() then
        if not w.enabled then return false end if
        // Calculate cursor position from click x
        let char_offset = (mouse_x - w.x - 4) / 8
        let text_len = string_utils.length(w.text)
        if char_offset < 0 then
            w.cursor_pos = 0
        elseif char_offset > text_len then
            w.cursor_pos = text_len
        else
            w.cursor_pos = char_offset
        end if
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_CHECKBOX() then
        if not w.enabled then return false end if
        w.checked = not w.checked
        let mut state_str = "false"
        if w.checked then state_str = "true" end if
        emit_signal(w.id, SIGNAL_CHECKED_CHANGED(), state_str)
        return true
    elseif w.widget_type == WIDGET_RADIOBUTTON() then
        if not w.enabled then return false end if
        w.checked = true
        emit_signal(w.id, SIGNAL_CHECKED_CHANGED(), "true")
        return true
    elseif w.widget_type == WIDGET_DROPDOWN() then
        if not w.enabled then return false end if
        // Cycle to next option on click
        let parts = string_utils.split(w.options, ",")
        if parts.count > 0 then
            w.selected_index = (w.selected_index + 1) % parts.count
            w.text = string_utils.trim(parts.items[w.selected_index])
            emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), w.text)
        end if
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_MENU() then
        if not w.enabled then return false end if
        // Determine which menu item was clicked based on y position
        let item_height = 20
        let item_index = (mouse_y - w.y) / item_height
        let parts = string_utils.split(w.options, ",")
        if item_index >= 0 and item_index < parts.count then
            w.selected_index = item_index
            let selected_item = string_utils.trim(parts.items[item_index])
            emit_signal(w.id, SIGNAL_CLICKED(), selected_item)
        end if
        return true
    elseif w.widget_type == WIDGET_SLIDER() then
        if not w.enabled then return false end if
        // Map click x to slider value
        let range_px = w.width - 16
        let offset = mouse_x - w.x - 8
        let mut pct = 0
        if range_px > 0 then
            pct = (offset * 100) / range_px
        end if
        if pct < 0 then pct = 0 end if
        if pct > 100 then pct = 100 end if
        w.selected_index = pct  // reuse selected_index for slider value (0-100)
        emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), string_utils.int_to_text(pct))
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_LISTBOX() then
        if not w.enabled then return false end if
        let item_height = 18
        let item_index = (mouse_y - w.y - 2) / item_height
        let parts = string_utils.split(w.options, ",")
        if item_index >= 0 and item_index < parts.count then
            w.selected_index = item_index
            let selected_item = string_utils.trim(parts.items[item_index])
            emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), selected_item)
        end if
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_TEXT_EDITOR() then
        if not w.enabled then return false end if
        // Calculate click position in editor
        let gutter_width = 48  // line number gutter
        let show_gutter = string_utils.contains(w.options, "line_numbers")
        let mut content_x = w.x + 4
        if show_gutter then content_x = w.x + gutter_width end if
        let line_height = 16
        let click_line = (mouse_y - w.y - 2) / line_height + w.selected_index
        let click_col = (mouse_x - content_x) / 8
        // Convert line/col to linear cursor position
        let total_lines = editor_get_line_count(w.text)
        let mut target_line = click_line
        if target_line < 0 then target_line = 0 end if
        if target_line >= total_lines then target_line = total_lines - 1 end if
        let line_start = editor_line_start(w.text, target_line)
        let line_end_pos = editor_line_end(w.text, target_line)
        let line_len = line_end_pos - line_start
        let mut target_col = click_col
        if target_col < 0 then target_col = 0 end if
        if target_col > line_len then target_col = line_len end if
        let new_cursor_pos = line_start + target_col

        // Handle text selection with Shift+click
        if (modifiers and winplat::MODIFIER_SHIFT()) != 0 then
            if not w.selection_active then
                // Start selection from current cursor position
                w.selection_start = w.cursor_pos
                w.selection_active = true
            end if
            editor_set_selection(w, w.selection_start, new_cursor_pos)
        else
            // Clear selection and set cursor
            editor_clear_selection(w)
            w.cursor_pos = new_cursor_pos
        end if

        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_TABBAR() then
        if not w.enabled then return false end if
        let parts = string_utils.split(w.options, ",")
        // Calculate which tab was clicked
        let tab_padding = 4
        let mut tab_x = w.x + 2
        let mut i = 0
        while i < parts.count do
            let tab_label = string_utils.trim(parts.items[i])
            let tab_width = string_utils.length(tab_label) * 8 + 20
            let mut close_btn_width = 0
            if w.checked then close_btn_width = 16 end if
            let full_tab_width = tab_width + close_btn_width
            if mouse_x >= tab_x and mouse_x < tab_x + full_tab_width then
                // Check if close button was clicked
                if w.checked and mouse_x >= tab_x + tab_width then
                    // Close button clicked - remove tab from options
                    let mut new_options = ""
                    let mut first = true
                    let mut j = 0
                    while j < parts.count do
                        if j != i then
                            if not first then new_options = new_options + "," end if
                            new_options = new_options + string_utils.trim(parts.items[j])
                            first = false
                        end if
                        j = j + 1
                    end while
                    w.options = new_options
                    if w.selected_index >= i and w.selected_index > 0 then
                        w.selected_index = w.selected_index - 1
                    end if
                    emit_signal(w.id, SIGNAL_TAB_CLOSED(), tab_label)
                    return true
                end if
                // Tab selection click
                if i != w.selected_index then
                    w.selected_index = i
                    emit_signal(w.id, SIGNAL_TAB_CHANGED(), tab_label)
                end if
                return true
            end if
            tab_x = tab_x + full_tab_width + tab_padding
            i = i + 1
        end while
        return true
    elseif w.widget_type == WIDGET_TREEVIEW() then
        if not w.enabled then return false end if

        // Handle normal click
        let nodes = treeview_parse_nodes(w.options)
        let item_height = 20
        let mut draw_y = w.y + 2
        let mut visible_index = 0
        let click_rel_y = mouse_y - w.y - 2
        let clicked_visible = click_rel_y / item_height
        // Find which visible node was clicked
        let mut target_node_index = -1
        let mut vi = 0
        let mut i = 0
        while i < nodes.count do
            if treeview_is_visible(nodes, w.text, i) then
                if vi == clicked_visible then
                    target_node_index = i
                    break
                end if
                vi = vi + 1
            end if
            i = i + 1
        end while
        if target_node_index >= 0 then
            let node = nodes.items[target_node_index]
            let indent = node.depth * 20 + w.x + 4
            // Check if expand/collapse arrow was clicked
            if treeview_has_children(nodes, target_node_index) and mouse_x >= indent and mouse_x < indent + 16 then
                w.text = treeview_toggle_expanded(w.text, target_node_index)
                emit_signal(w.id, SIGNAL_NODE_TOGGLED(), node.label)
            else
                w.selected_index = target_node_index
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), node.label)
            end if
        end if
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_SCROLL_CONTAINER() then
        if not w.enabled then return false end if
        // Check scrollbar click
        let needs_vscroll = scroll_container_needs_vscroll(w)
        let scrollbar_width = 12
        if needs_vscroll and mouse_x >= w.x + w.width - scrollbar_width then
            // Clicked on vertical scrollbar - map y to scroll position
            let content_h = scroll_container_get_content_height(w)
            let max_scroll = scroll_container_max_scroll_y(w)
            let rel_y = mouse_y - w.y
            let new_scroll = (rel_y * max_scroll) / w.height
            w.selected_index = new_scroll
            if w.selected_index < 0 then w.selected_index = 0 end if
            if w.selected_index > max_scroll then w.selected_index = max_scroll end if
            return true
        end if
        set_focused_widget_id(w.id)
        return true
    elseif w.widget_type == WIDGET_DIALOG() then
        if not w.visible then return false end if
        let dlg_type = dialog_get_type(w)
        let btn_y = w.y + w.height - 50
        let btn_height = 30
        if dlg_type == "confirm" then
            // OK button
            let ok_x = w.x + w.width / 2 - 110
            if mouse_x >= ok_x and mouse_x <= ok_x + 100 and mouse_y >= btn_y and mouse_y <= btn_y + btn_height then
                w.selected_index = 1  // OK
                w.visible = false
                emit_signal(w.id, SIGNAL_CLICKED(), "ok")
                return true
            end if
            // Cancel button
            let cancel_x = w.x + w.width / 2 + 10
            if mouse_x >= cancel_x and mouse_x <= cancel_x + 100 and mouse_y >= btn_y and mouse_y <= btn_y + btn_height then
                w.selected_index = 2  // Cancel
                w.visible = false
                emit_signal(w.id, SIGNAL_CLICKED(), "cancel")
                return true
            end if
        else
            // Info dialog - just OK button
            let ok_x = w.x + w.width / 2 - 50
            if mouse_x >= ok_x and mouse_x <= ok_x + 100 and mouse_y >= btn_y and mouse_y <= btn_y + btn_height then
                w.selected_index = 1  // OK
                w.visible = false
                emit_signal(w.id, SIGNAL_CLICKED(), "ok")
                return true
            end if
        end if
        // Close button in title bar
        let close_x = w.x + w.width - 25
        let close_y = w.y + 2
        if mouse_x >= close_x and mouse_x <= close_x + 20 and mouse_y >= close_y and mouse_y <= close_y + 20 then
            w.selected_index = 2  // Cancel/Close
            w.visible = false
            emit_signal(w.id, SIGNAL_CLICKED(), "cancel")
            return true
        end if
        return true  // Consume click (modal)
    end if

    return false
end function

pub function process_key_event(event: winplat::event, mut w: widget_data) -> bool
    if w.id != focused_widget_id then return false end if
    if event.event_type != winplat::EVENT_KEY_PRESS() then return false end if

    let key = event.key_code

    if w.widget_type == WIDGET_TEXT_FIELD() then
        if key == 0x10008 then // Backspace (flagged by the native platform layer)
            if w.cursor_pos > 0 then
                undo_save_state(w)  // Save state before modification
                let before_cursor = string_utils.substring(w.text, 0, w.cursor_pos - 1)
                let after_cursor = string_utils.substring(w.text, w.cursor_pos, string_utils.length(w.text))
                w.text = before_cursor + after_cursor
                w.cursor_pos = w.cursor_pos - 1
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                return true
            end if
        elseif key == 0x10009 then // Tab
            // For now, just consume tab. Later, implement focus cycling.
            emit_signal(w.id, SIGNAL_KEY_DOWN(), "tab")
            return true
        elseif key == 0x1000D then // Enter
            // For now, just consume enter. Later, implement form submission.
            emit_signal(w.id, SIGNAL_KEY_DOWN(), "enter")
            return true
        elseif key < 0x10000 then // Normal character
            let char_to_add = keycode_to_text(key)
            if string_utils.length(char_to_add) > 0 then
                undo_save_state(w)  // Save state before modification
                w.text = insert_char_at_cursor(w.text, char_to_add, w.cursor_pos)
                w.cursor_pos = w.cursor_pos + 1
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                // Trigger completion on word characters
                let code = string_utils.char_code(char_to_add)
                if (code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95 then
                    // Word character - trigger completion
                    let cursor_line = editor_cursor_to_line(w.text, w.cursor_pos)
                    let cursor_col = editor_cursor_to_col(w.text, w.cursor_pos)
                    let cursor_draw_line = cursor_line - w.selected_index
                    let visible = editor_visible_lines(w.height)
                    if cursor_draw_line >= 0 and cursor_draw_line < visible then
                        let show_gutter = string_utils.contains(w.options, "line_numbers")
                        let gutter_width = 48
                        let mut content_x = w.x + 4
                        if show_gutter then content_x = w.x + gutter_width + 4 end if
                        let cursor_x = content_x + (cursor_col * 8)
                        let cursor_y = w.y + 2 + (cursor_draw_line * 16)
                        code_completion.trigger_completion(w.text, w.cursor_pos, cursor_x, cursor_y + 20)
                    end if
                elseif code == 46 then
                    // Dot character - trigger module-qualified completion
                    let cursor_line = editor_cursor_to_line(w.text, w.cursor_pos)
                    let cursor_col = editor_cursor_to_col(w.text, w.cursor_pos)
                    let cursor_draw_line = cursor_line - w.selected_index
                    let visible = editor_visible_lines(w.height)
                    if cursor_draw_line >= 0 and cursor_draw_line < visible then
                        let show_gutter = string_utils.contains(w.options, "line_numbers")
                        let gutter_width = 48
                        let mut content_x = w.x + 4
                        if show_gutter then content_x = w.x + gutter_width + 4 end if
                        let cursor_x = content_x + (cursor_col * 8)
                        let cursor_y = w.y + 2 + (cursor_draw_line * 16)
                        code_completion.trigger_completion(w.text, w.cursor_pos, cursor_x, cursor_y + 20)
                    end if
                else
                    // Non-word character - hide completion
                    code_completion.hide_completion()
                end if
            end if
        end if
    elseif w.widget_type == WIDGET_SLIDER() then
        // Left/Right arrow keys adjust slider value
        if key == 0x10025 then // Left arrow
            if w.selected_index > 0 then
                w.selected_index = w.selected_index - 1
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), string_utils.int_to_text(w.selected_index))
                return true
            end if
        elseif key == 0x10027 then // Right arrow
            if w.selected_index < 100 then
                w.selected_index = w.selected_index + 1
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), string_utils.int_to_text(w.selected_index))
                return true
            end if
        end if
    elseif w.widget_type == WIDGET_LISTBOX() then
        // Up/Down arrow keys navigate list items
        let parts = string_utils.split(w.options, ",")
        if key == 0x10026 then // Up arrow
            if w.selected_index > 0 then
                w.selected_index = w.selected_index - 1
                let item_text = string_utils.trim(parts.items[w.selected_index])
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), item_text)
                return true
            end if
        elseif key == 0x10028 then // Down arrow
            if w.selected_index < parts.count - 1 then
                w.selected_index = w.selected_index + 1
                let item_text = string_utils.trim(parts.items[w.selected_index])
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), item_text)
                return true
            end if
        elseif key == 0x1000D then // Enter to select
            if w.selected_index >= 0 and w.selected_index < parts.count then
                let item_text = string_utils.trim(parts.items[w.selected_index])
                emit_signal(w.id, SIGNAL_CLICKED(), item_text)
                return true
            end if
        end if
    elseif w.widget_type == WIDGET_DROPDOWN() then
        // Up/Down arrow keys cycle dropdown options
        let parts = string_utils.split(w.options, ",")
        if parts.count > 0 then
            if key == 0x10026 then // Up arrow
                if w.selected_index > 0 then
                    w.selected_index = w.selected_index - 1
                else
                    w.selected_index = parts.count - 1
                end if
                w.text = string_utils.trim(parts.items[w.selected_index])
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), w.text)
                return true
            elseif key == 0x10028 then // Down arrow
                w.selected_index = (w.selected_index + 1) % parts.count
                w.text = string_utils.trim(parts.items[w.selected_index])
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), w.text)
                return true
            end if
        end if
    elseif w.widget_type == WIDGET_TEXT_EDITOR() then
        let text_len = string_utils.length(w.text)

        // Check for keyboard shortcuts first (Ctrl+Key combinations)
        if handle_keyboard_shortcut(event, w) then
            return true
        end if

        if key == 0x10008 then // Backspace
            // Delete selection if active
            if w.selection_active then
                undo_save_state(w)
                text_editor_delete_selection(w)
                code_completion.hide_completion()
                let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
                if cur_line < w.selected_index then
                    w.selected_index = cur_line
                end if
                return true
            end if
            if w.cursor_pos > 0 then
                undo_save_state(w)  // Save state before modification
                let before = string_utils.substring(w.text, 0, w.cursor_pos - 1)
                let after = string_utils.substring(w.text, w.cursor_pos, text_len)
                w.text = before + after
                w.cursor_pos = w.cursor_pos - 1
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                // Hide completion on backspace
                code_completion.hide_completion()
                // Auto-scroll up if cursor moved above visible area
                let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
                if cur_line < w.selected_index then
                    w.selected_index = cur_line
                end if
                return true
            end if
        elseif key == 0x1000D then // Enter - insert newline
            undo_save_state(w)  // Save state before modification
            let before = string_utils.substring(w.text, 0, w.cursor_pos)
            let after = string_utils.substring(w.text, w.cursor_pos, text_len)
            w.text = before + "\n" + after
            w.cursor_pos = w.cursor_pos + 1
            emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
            // Auto-scroll down if cursor below visible area
            let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
            let vis = editor_visible_lines(w.height)
            if cur_line >= w.selected_index + vis then
                w.selected_index = cur_line - vis + 1
            end if
            return true
        elseif key == 0x10025 then // Left arrow
            if w.cursor_pos > 0 then
                w.cursor_pos = w.cursor_pos - 1
                let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
                if cur_line < w.selected_index then
                    w.selected_index = cur_line
                end if
                return true
            end if
        elseif key == 0x10027 then // Right arrow
            if w.cursor_pos < text_len then
                w.cursor_pos = w.cursor_pos + 1
                let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
                let vis = editor_visible_lines(w.height)
                if cur_line >= w.selected_index + vis then
                    w.selected_index = cur_line - vis + 1
                end if
                return true
            end if
        elseif key == 0x10026 then // Up arrow
            let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
            let cur_col = editor_cursor_to_col(w.text, w.cursor_pos)
            if cur_line > 0 then
                let target_line = cur_line - 1
                let target_start = editor_line_start(w.text, target_line)
                let target_end = editor_line_end(w.text, target_line)
                let target_len = target_end - target_start
                let mut new_col = cur_col
                if new_col > target_len then new_col = target_len end if
                let new_cursor_pos = target_start + new_col

                // Handle Shift+Up arrow selection
                if (event.modifiers and winplat::MODIFIER_SHIFT()) != 0 then
                    if not w.selection_active then
                        w.selection_start = w.cursor_pos
                        w.selection_active = true
                    end if
                    editor_set_selection(w, w.selection_start, new_cursor_pos)
                else
                    editor_clear_selection(w)
                end if

                w.cursor_pos = new_cursor_pos
                if target_line < w.selected_index then
                    w.selected_index = target_line
                end if
                return true
            end if
        elseif key == 0x10028 then // Down arrow
            let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
            let cur_col = editor_cursor_to_col(w.text, w.cursor_pos)
            let total_lines = editor_get_line_count(w.text)
            if cur_line < total_lines - 1 then
                let target_line = cur_line + 1
                let target_start = editor_line_start(w.text, target_line)
                let target_end = editor_line_end(w.text, target_line)
                let target_len = target_end - target_start
                let mut new_col = cur_col
                if new_col > target_len then new_col = target_len end if
                let new_cursor_pos = target_start + new_col

                // Handle Shift+Down arrow selection
                if (event.modifiers and winplat::MODIFIER_SHIFT()) != 0 then
                    if not w.selection_active then
                        w.selection_start = w.cursor_pos
                        w.selection_active = true
                    end if
                    editor_set_selection(w, w.selection_start, new_cursor_pos)
                else
                    editor_clear_selection(w)
                end if

                w.cursor_pos = new_cursor_pos
                let vis = editor_visible_lines(w.height)
                if target_line >= w.selected_index + vis then
                    w.selected_index = target_line - vis + 1
                end if
                return true
            end if
        elseif key == 0x10024 then // Home - go to start of line
            let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
            w.cursor_pos = editor_line_start(w.text, cur_line)
            return true
        elseif key == 0x10023 then // End - go to end of line
            let cur_line = editor_cursor_to_line(w.text, w.cursor_pos)
            w.cursor_pos = editor_line_end(w.text, cur_line)
            return true
        elseif key == 0x1002E then // Delete
            // Delete selection if active
            if w.selection_active then
                undo_save_state(w)
                text_editor_delete_selection(w)
                code_completion.hide_completion()
                return true
            end if
            if w.cursor_pos < text_len then
                undo_save_state(w)
                let before = string_utils.substring(w.text, 0, w.cursor_pos)
                let after = string_utils.substring(w.text, w.cursor_pos + 1, text_len)
                w.text = before + after
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                code_completion.hide_completion()
                return true
            end if
        elseif key == 0x10009 then // Tab
            if code_completion.is_completion_visible() then
                // Apply completion
                w.text = code_completion.apply_completion(w.text, w.cursor_pos)
                w.cursor_pos = w.cursor_pos + string_utils.length(code_completion.get_selected_completion().insert_text) - string_utils.length(code_completion.get_word_at_cursor(w.text, w.cursor_pos))
                code_completion.hide_completion()
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
            else
                // Insert 4 spaces
                let before = string_utils.substring(w.text, 0, w.cursor_pos)
                let after = string_utils.substring(w.text, w.cursor_pos, text_len)
                w.text = before + "    " + after
                w.cursor_pos = w.cursor_pos + 4
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
            end if
            return true
        elseif key < 0x10000 then // Normal character
            let char_to_add = keycode_to_text(key)
            if string_utils.length(char_to_add) > 0 then
                w.text = insert_char_at_cursor(w.text, char_to_add, w.cursor_pos)
                w.cursor_pos = w.cursor_pos + 1
                emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                // Trigger completion on word characters
                let code = string_utils.char_code(char_to_add)
                if (code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or code == 95 then
                    // Word character - trigger completion
                    let cursor_line = editor_cursor_to_line(w.text, w.cursor_pos)
                    let cursor_col = editor_cursor_to_col(w.text, w.cursor_pos)
                    let scroll_offset = w.selected_index
                    let visible = editor_visible_lines(w.height)
                    let cursor_draw_line = cursor_line - scroll_offset
                    if cursor_draw_line >= 0 and cursor_draw_line < visible then
                        let show_gutter = string_utils.contains(w.options, "line_numbers")
                        let gutter_width = 48
                        let mut content_x = w.x + 4
                        if show_gutter then content_x = w.x + gutter_width + 4 end if
                        let cursor_x = content_x + (cursor_col * 8)
                        let cursor_y = w.y + 2 + (cursor_draw_line * 16)
                        code_completion.trigger_completion(w.text, w.cursor_pos, cursor_x, cursor_y + 20)
                    end if
                else
                    // Non-word character - hide completion
                    code_completion.hide_completion()
                end if
                return true
            end if
        elseif key == 0x10001 then // Ctrl+A (Select All)
            editor_select_all(w)
            return true
        elseif key == 0x03 then // Ctrl+C (Copy)
            if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
                let selected_text = editor_get_selected_text(w)
                if string_utils.length(selected_text) > 0 then
                    winplat::set_clipboard_text(selected_text)
                end if
                return true
            end if
        elseif key == 0x18 then // Ctrl+X (Cut)
            if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
                let selected_text = editor_get_selected_text(w)
                if string_utils.length(selected_text) > 0 then
                    winplat::set_clipboard_text(selected_text)
                    undo_save_state(w)  // Save state before modification
                    // Delete selected text
                    let before = string_utils.substring(w.text, 0, w.selection_start)
                    let after = string_utils.substring(w.text, w.selection_end, string_utils.length(w.text))
                    w.text = before + after
                    w.cursor_pos = w.selection_start
                    editor_clear_selection(w)
                    emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                end if
                return true
            end if
        elseif key == 0x16 then // Ctrl+V (Paste)
            if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
                let clipboard_text = winplat::get_clipboard_text()
                if string_utils.length(clipboard_text) > 0 then
                    undo_save_state(w)  // Save state before modification
                    // Delete selected text if any
                    let mut new_cursor_pos = w.cursor_pos
                    let mut before = string_utils.substring(w.text, 0, w.cursor_pos)
                    let mut after = string_utils.substring(w.text, w.cursor_pos, string_utils.length(w.text))

                    if w.selection_active then
                        before = string_utils.substring(w.text, 0, w.selection_start)
                        after = string_utils.substring(w.text, w.selection_end, string_utils.length(w.text))
                        new_cursor_pos = w.selection_start
                        editor_clear_selection(w)
                    end if

                    // Insert clipboard text
                    w.text = before + clipboard_text + after
                    w.cursor_pos = new_cursor_pos + string_utils.length(clipboard_text)
                    emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
                end if
                return true
            end if
        elseif key == 0x1A then // Ctrl+Z (Undo)
            if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
                if undo_perform_undo(w) then
                    return true
                end if
            end if
        elseif key == 0x19 then // Ctrl+Y (Redo)
            if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
                if undo_perform_redo(w) then
                    return true
                end if
            end if
        end if

        // Handle completion navigation keys
        if w.widget_type == WIDGET_TEXT_EDITOR() and code_completion.is_completion_visible() then
            if key == 0x10026 then // Up arrow
                code_completion.select_previous_completion()
                return true
            elseif key == 0x10028 then // Down arrow
                code_completion.select_next_completion()
                return true
            elseif key == 0x1001B then // Escape
                code_completion.hide_completion()
                return true
            end if
        end if
    elseif w.widget_type == WIDGET_TABBAR() then
        let parts = string_utils.split(w.options, ",")
        if parts.count > 0 then
            if key == 0x10025 then // Left arrow - previous tab
                if w.selected_index > 0 then
                    w.selected_index = w.selected_index - 1
                    let tab_label = string_utils.trim(parts.items[w.selected_index])
                    emit_signal(w.id, SIGNAL_TAB_CHANGED(), tab_label)
                    return true
                end if
            elseif key == 0x10027 then // Right arrow - next tab
                if w.selected_index < parts.count - 1 then
                    w.selected_index = w.selected_index + 1
                    let tab_label = string_utils.trim(parts.items[w.selected_index])
                    emit_signal(w.id, SIGNAL_TAB_CHANGED(), tab_label)
                    return true
                end if
            end if
        end if
    elseif w.widget_type == WIDGET_TREEVIEW() then
        let nodes = treeview_parse_nodes(w.options)
        if key == 0x10026 then // Up arrow
            // Move to previous visible node
            let mut prev_visible = -1
            let mut i = 0
            while i < nodes.count do
                if treeview_is_visible(nodes, w.text, i) then
                    if i == w.selected_index then
                        if prev_visible >= 0 then
                            w.selected_index = prev_visible
                            let node = nodes.items[prev_visible]
                            emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), node.label)
                        end if
                        return true
                    end if
                    prev_visible = i
                end if
                i = i + 1
            end while
        elseif key == 0x10028 then // Down arrow
            // Move to next visible node
            let mut found_current = false
            let mut i = 0
            while i < nodes.count do
                if treeview_is_visible(nodes, w.text, i) then
                    if found_current then
                        w.selected_index = i
                        let node = nodes.items[i]
                        emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), node.label)
                        return true
                    end if
                    if i == w.selected_index then
                        found_current = true
                    end if
                end if
                i = i + 1
            end while
        elseif key == 0x10027 then // Right arrow - expand node
            if w.selected_index >= 0 and w.selected_index < nodes.count then
                if treeview_has_children(nodes, w.selected_index) then
                    if not treeview_is_expanded(w.text, w.selected_index) then
                        w.text = treeview_toggle_expanded(w.text, w.selected_index)
                        let node = nodes.items[w.selected_index]
                        emit_signal(w.id, SIGNAL_NODE_TOGGLED(), node.label)
                        return true
                    end if
                end if
            end if
        elseif key == 0x10025 then // Left arrow - collapse node
            if w.selected_index >= 0 and w.selected_index < nodes.count then
                if treeview_has_children(nodes, w.selected_index) then
                    if treeview_is_expanded(w.text, w.selected_index) then
                        w.text = treeview_toggle_expanded(w.text, w.selected_index)
                        let node = nodes.items[w.selected_index]
                        emit_signal(w.id, SIGNAL_NODE_TOGGLED(), node.label)
                        return true
                    end if
                end if
            end if
        elseif key == 0x1000D then // Enter - toggle expand or select
            if w.selected_index >= 0 and w.selected_index < nodes.count then
                let node = nodes.items[w.selected_index]
                if treeview_has_children(nodes, w.selected_index) then
                    w.text = treeview_toggle_expanded(w.text, w.selected_index)
                    emit_signal(w.id, SIGNAL_NODE_TOGGLED(), node.label)
                else
                    emit_signal(w.id, SIGNAL_CLICKED(), node.label)
                end if
                return true
            end if
        end if
    elseif w.widget_type == WIDGET_SCROLL_CONTAINER() then
        // Arrow keys scroll the container
        let max_y = scroll_container_max_scroll_y(w)
        let max_x = scroll_container_max_scroll_x(w)
        if key == 0x10026 then // Up
            if w.selected_index > 0 then
                w.selected_index = w.selected_index - 20
                if w.selected_index < 0 then w.selected_index = 0 end if
                return true
            end if
        elseif key == 0x10028 then // Down
            if w.selected_index < max_y then
                w.selected_index = w.selected_index + 20
                if w.selected_index > max_y then w.selected_index = max_y end if
                return true
            end if
        elseif key == 0x10025 then // Left
            if w.cursor_pos > 0 then
                w.cursor_pos = w.cursor_pos - 20
                if w.cursor_pos < 0 then w.cursor_pos = 0 end if
                return true
            end if
        elseif key == 0x10027 then // Right
            if w.cursor_pos < max_x then
                w.cursor_pos = w.cursor_pos + 20
                if w.cursor_pos > max_x then w.cursor_pos = max_x end if
                return true
            end if
        elseif key == 0x10021 then // Page Up
            w.selected_index = w.selected_index - w.height
            if w.selected_index < 0 then w.selected_index = 0 end if
            return true
        elseif key == 0x10022 then // Page Down
            w.selected_index = w.selected_index + w.height
            if w.selected_index > max_y then w.selected_index = max_y end if
            return true
        elseif key == 0x10024 then // Home
            w.selected_index = 0
            return true
        elseif key == 0x10023 then // End
            w.selected_index = max_y
            return true
        end if
    elseif w.widget_type == WIDGET_DIALOG() then
        // Escape closes dialog
        if key == 0x1001B then // Escape
            w.selected_index = 2  // Cancel
            w.visible = false
            emit_signal(w.id, SIGNAL_CLICKED(), "cancel")
            return true
        elseif key == 0x1000D then // Enter confirms
            w.selected_index = 1  // OK
            w.visible = false
            emit_signal(w.id, SIGNAL_CLICKED(), "ok")
            return true
        end if
    end if

    return false
end function

// =========================================================================
// Hot Reload State Preservation System
// =========================================================================

// Widget state entry for hot reload preservation
pub struct widget_state_entry
    widget_id: i32
    widget_type: i32
    text: text
    cursor_pos: i32
    selection_start: i32
    selection_end: i32
    selection_active: bool
    scroll_offset: i32
    checked: bool
    selected_index: i32
    options: text
end struct

// Snapshot of all NyAIX state for hot reload
pub struct widget_state_snapshot
    // Global widget state
    focused_widget_id: i32

    // Tooltip state
    tooltip_hover_widget_id: i32
    tooltip_hover_time: i32
    last_mouse_x: i32
    last_mouse_y: i32

    // Context menu state
    context_menu_visible: bool
    context_menu_x: i32
    context_menu_y: i32
    context_menu_selected: i32

    // Error line state
    error_lines: array<i32>
    error_line_count: i32

    // Drag and drop state
    drag_active: bool
    drag_widget_id: i32
    drag_type: i32
    drag_text_data: text
    drag_start_x: i32
    drag_start_y: i32
    drag_current_x: i32
    drag_current_y: i32

    // Signal queue state
    signal_queue: array<signal_data>
    signal_count: i32

    // Per-widget state entries
    widget_states: array<widget_state_entry>
    widget_state_count: i32

    // Snapshot metadata
    version: i32
    timestamp: i32
end struct

// Global hot reload snapshot storage
let mut __hot_reload_snapshot: widget_state_snapshot = create_empty_snapshot()
let mut __hot_reload_version: i32 = 0

pub function create_empty_snapshot() -> widget_state_snapshot
    return widget_state_snapshot {
        focused_widget_id: -1,
        tooltip_hover_widget_id: -1,
        tooltip_hover_time: 0,
        last_mouse_x: 0,
        last_mouse_y: 0,
        context_menu_visible: false,
        context_menu_x: 0,
        context_menu_y: 0,
        context_menu_selected: -1,
        error_lines: [],
        error_line_count: 0,
        drag_active: false,
        drag_widget_id: -1,
        drag_type: DRAG_TYPE_NONE(),
        drag_text_data: "",
        drag_start_x: 0,
        drag_start_y: 0,
        drag_current_x: 0,
        drag_current_y: 0,
        signal_queue: [],
        signal_count: 0,
        widget_states: [],
        widget_state_count: 0,
        version: 0,
        timestamp: 0
    }
end function

// Snapshot a single widget's state
pub function snapshot_widget_state(w: widget_data) -> widget_state_entry
    return widget_state_entry {
        widget_id: w.id,
        widget_type: w.widget_type,
        text: w.text,
        cursor_pos: w.cursor_pos,
        selection_start: w.selection_start,
        selection_end: w.selection_end,
        selection_active: w.selection_active,
        scroll_offset: w.selected_index,
        checked: w.checked,
        selected_index: w.selected_index,
        options: w.options
    }
end function

// Restore a widget's state from entry
pub function restore_widget_state(mut w: widget_data, entry: widget_state_entry)
    // Only restore if widget types match
    if w.widget_type != entry.widget_type then return end if

    // Restore text content for text-based widgets
    if w.widget_type == WIDGET_TEXT_EDITOR() or w.widget_type == WIDGET_TEXT_FIELD() then
        w.text = entry.text
        w.cursor_pos = entry.cursor_pos
        w.selection_start = entry.selection_start
        w.selection_end = entry.selection_end
        w.selection_active = entry.selection_active
    end if

    // Restore scroll position for scrollable widgets
    if w.widget_type == WIDGET_TEXT_EDITOR() or w.widget_type == WIDGET_LISTBOX() or w.widget_type == WIDGET_TREEVIEW() then
        w.selected_index = entry.scroll_offset
    end if

    // Restore checked state for checkboxes and radio buttons
    if w.widget_type == WIDGET_CHECKBOX() or w.widget_type == WIDGET_RADIOBUTTON() then
        w.checked = entry.checked
    end if

    // Restore selected index for selection widgets
    if w.widget_type == WIDGET_DROPDOWN() or w.widget_type == WIDGET_LISTBOX() or w.widget_type == WIDGET_TABBAR() then
        w.selected_index = entry.selected_index
    end if
end function

// Snapshot all NyAIX global state
pub function snapshot_all_state(widgets: widget_array) -> widget_state_snapshot
    let mut snap = widget_state_snapshot {
        // Global widget state
        focused_widget_id: focused_widget_id,

        // Tooltip state
        tooltip_hover_widget_id: tooltip_hover_widget_id,
        tooltip_hover_time: tooltip_hover_time,
        last_mouse_x: last_mouse_x,
        last_mouse_y: last_mouse_y,

        // Context menu state
        context_menu_visible: context_menu_visible,
        context_menu_x: context_menu_x,
        context_menu_y: context_menu_y,
        context_menu_selected: context_menu_selected,

        // Error line state
        error_lines: error_lines,
        error_line_count: error_line_count,

        // Drag and drop state
        drag_active: drag_active,
        drag_widget_id: drag_widget_id,
        drag_type: drag_type,
        drag_text_data: drag_text_data,
        drag_start_x: drag_start_x,
        drag_start_y: drag_start_y,
        drag_current_x: drag_current_x,
        drag_current_y: drag_current_y,

        // Signal queue state (preserve pending signals)
        signal_queue: signal_queue,
        signal_count: signal_count,

        // Widget states
        widget_states: [],
        widget_state_count: 0,

        // Metadata
        version: __hot_reload_version,
        timestamp: 0
    }

    // Snapshot each widget's state
    let mut i = 0
    while i < widgets.count do
        let entry = snapshot_widget_state(widgets.items[i])
        snap.widget_states = snap.widget_states + [entry]
        snap.widget_state_count = snap.widget_state_count + 1
        i = i + 1
    end while

    return snap
end function

// Restore all NyAIX global state from snapshot
pub function restore_all_state(snap: widget_state_snapshot, mut widgets: widget_array)
    // Restore global widget state
    focused_widget_id = snap.focused_widget_id

    // Restore tooltip state
    tooltip_hover_widget_id = snap.tooltip_hover_widget_id
    tooltip_hover_time = snap.tooltip_hover_time
    last_mouse_x = snap.last_mouse_x
    last_mouse_y = snap.last_mouse_y

    // Restore context menu state
    context_menu_visible = snap.context_menu_visible
    context_menu_x = snap.context_menu_x
    context_menu_y = snap.context_menu_y
    context_menu_selected = snap.context_menu_selected

    // Restore error line state
    error_lines = snap.error_lines
    error_line_count = snap.error_line_count

    // Restore drag and drop state
    drag_active = snap.drag_active
    drag_widget_id = snap.drag_widget_id
    drag_type = snap.drag_type
    drag_text_data = snap.drag_text_data
    drag_start_x = snap.drag_start_x
    drag_start_y = snap.drag_start_y
    drag_current_x = snap.drag_current_x
    drag_current_y = snap.drag_current_y

    // Restore signal queue
    signal_queue = snap.signal_queue
    signal_count = snap.signal_count

    // Restore per-widget state by matching widget IDs
    let mut i = 0
    while i < widgets.count do
        let widget_id = widgets.items[i].id

        // Find matching state entry
        let mut j = 0
        while j < snap.widget_state_count do
            if snap.widget_states[j].widget_id == widget_id then
                restore_widget_state(widgets.items[i], snap.widget_states[j])
                break
            end if
            j = j + 1
        end while

        i = i + 1
    end while
end function

// Hot reload hooks - called by runtime before/after reload
// @hotreload.before
pub function on_before_reload(widgets: widget_array)
    __hot_reload_snapshot = snapshot_all_state(widgets)
    __hot_reload_version = __hot_reload_version + 1
end function

// @hotreload.after
pub function on_after_reload(mut widgets: widget_array)
    restore_all_state(__hot_reload_snapshot, widgets)
end function

// Get current hot reload version
pub function get_hot_reload_version() -> i32
    return __hot_reload_version
end function

// Get last snapshot for debugging/inspection
pub function get_last_snapshot() -> widget_state_snapshot
    return __hot_reload_snapshot
end function

// Clear snapshot (after successful reload completion)
pub function clear_hot_reload_snapshot()
    __hot_reload_snapshot = create_empty_snapshot()
end function

// Check if a snapshot exists
pub function has_hot_reload_snapshot() -> bool
    return __hot_reload_snapshot.widget_state_count > 0 or
           __hot_reload_snapshot.signal_count > 0 or
           __hot_reload_snapshot.focused_widget_id != -1
end function

pub function process_mouse_wheel_event(event: winplat::event, mut w: widget_data) -> bool
    if w.id != focused_widget_id then return false end if
    if event.event_type != winplat::EVENT_MOUSE_WHEEL() then return false end if

    let wheel_delta = event.mouse_y  // mouse_y contains wheel delta

    if w.widget_type == WIDGET_TEXT_EDITOR() then
        // Scroll text editor with mouse wheel
        let total_lines = editor_get_line_count(w.text)
        let visible_lines = editor_visible_lines(w.height)
        let mut scroll_amount = wheel_delta / 120  // Standard wheel delta

        // Invert for natural scrolling
        scroll_amount = -scroll_amount

        let mut new_scroll = w.selected_index + scroll_amount
        if new_scroll < 0 then new_scroll = 0 end if
        let mut max_scroll = total_lines - visible_lines
        if max_scroll < 0 then max_scroll = 0 end if
        if new_scroll > max_scroll then new_scroll = max_scroll end if

        w.selected_index = new_scroll
        emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "scrolled")
        return true

    elseif w.widget_type == WIDGET_LISTBOX() then
        // Scroll listbox with mouse wheel
        let parts = string_utils.split(w.options, ",")
        let mut scroll_amount = wheel_delta / 120
        scroll_amount = -scroll_amount  // Natural scrolling

        let mut new_index = w.selected_index + scroll_amount
        if new_index < 0 then new_index = 0 end if
        if new_index >= parts.count then new_index = parts.count - 1 end if

        w.selected_index = new_index
        let item_text = string_utils.trim(parts.items[new_index])
        emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), item_text)
        return true

    elseif w.widget_type == WIDGET_TREEVIEW() then
        // Scroll treeview with mouse wheel
        let nodes = treeview_parse_nodes(w.options)
        let visible_count = count_visible_nodes(nodes, w.text)
        let item_height = 20
        let visible_items = w.height / item_height

        let mut scroll_amount = wheel_delta / 120
        scroll_amount = -scroll_amount

        // Find first visible node index
        let mut first_visible_index = -1
        let mut i = 0
        while i < nodes.count do
            if treeview_is_visible(nodes, w.text, i) then
                first_visible_index = i
                break
            end if
            i = i + 1
        end while

        if first_visible_index >= 0 then
            let mut new_first = first_visible_index + scroll_amount
            if new_first < 0 then new_first = 0 end if
            if new_first >= nodes.count then new_first = nodes.count - 1 end if

            // Convert to scroll offset (stored in selected_index)
            w.selected_index = new_first
            emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "scrolled")
            return true
        end if

    elseif w.widget_type == WIDGET_SCROLL_CONTAINER() then
        // Enhanced scroll container with mouse wheel support
        let needs_vscroll = scroll_container_needs_vscroll(w)
        let needs_hscroll = scroll_container_needs_hscroll(w)

        if needs_vscroll or needs_hscroll then
            let mut scroll_amount = wheel_delta / 120
            scroll_amount = -scroll_amount  // Natural scrolling

            // Enhanced scrolling with momentum
            let scroll_speed = 3 // pixels per wheel tick (base speed)
            if (event.modifiers and winplat::MODIFIER_SHIFT()) != 0 then
                scroll_amount = scroll_amount * 2  // Accelerate with Shift
            end if

            // Determine scroll direction based on modifiers
            let mut scroll_vertical = true
            let mut scroll_horizontal = false
            if needs_hscroll and not needs_vscroll then
                scroll_vertical = false
                scroll_horizontal = true
            end if

            if scroll_vertical then
                // Vertical scrolling
                let max_scroll_y = scroll_container_max_scroll_y(w)
                let mut new_scroll_y = w.selected_index + (scroll_amount * scroll_speed)
                if new_scroll_y < 0 then new_scroll_y = 0 end if
                if new_scroll_y > max_scroll_y then new_scroll_y = max_scroll_y end if
                w.selected_index = new_scroll_y
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "scrolled")
                return true
            else
                // Horizontal scrolling
                let max_scroll_x = scroll_container_max_scroll_x(w)
                let mut new_scroll_x = w.cursor_pos + (scroll_amount * scroll_speed)
                if new_scroll_x < 0 then new_scroll_x = 0 end if
                if new_scroll_x > max_scroll_x then new_scroll_x = max_scroll_x end if
                w.cursor_pos = new_scroll_x
                emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "scrolled")
                return true
            end if
        end if

        // If no vertical scroll needed, try horizontal scrolling
        let max_scroll_x = scroll_container_max_scroll_x(w)
        if max_scroll_x > 0 then
            let mut h_scroll = 0 - (wheel_delta / 120) * 3
            let mut new_scroll_x = w.cursor_pos + h_scroll
            if new_scroll_x < 0 then new_scroll_x = 0 end if
            if new_scroll_x > max_scroll_x then new_scroll_x = max_scroll_x end if
            w.cursor_pos = new_scroll_x
            emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "scrolled")
            return true
        end if
    end if

    return false
end function

pub function process_mouse_click_event(event: winplat::event, mut w: widget_data) -> bool
    if event.event_type != winplat::EVENT_MOUSE_CLICK() then return false end if

    let x = event.mouse_x
    let y = event.mouse_y

    // Check if click is within widget bounds
    if x < w.x or x >= w.x + w.width or y < w.y or y >= w.y + w.height then
        return false
    end if

    // Handle context menu clicks first (right-click)
    if (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
        if w.widget_type == WIDGET_TEXT_EDITOR() then
            show_text_editor_context_menu(w, x, y)
            return true
        end if
    end if

    if w.widget_type == WIDGET_TEXT_EDITOR() then
        // Handle text editor clicks
        let local_x = x - w.x
        let local_y = y - w.y

        // Calculate line and column from click position
        let line_height = 16
        let char_width = 8
        let line = local_y / line_height
        let col = (local_x - 30) / char_width  // 30px offset for line numbers

        // Update cursor position
        w.cursor_pos = editor_get_position_from_line_col(w.text, line, col)
        emit_signal(w.id, SIGNAL_CLICKED(), "text_editor")
        return true

    elseif w.widget_type == WIDGET_BUTTON() then
        // Handle button clicks
        emit_signal(w.id, SIGNAL_CLICKED(), w.text)
        return true

    elseif w.widget_type == WIDGET_CHECKBOX() then
        // Handle checkbox clicks
        w.selected_index = 1 - w.selected_index  // Toggle
        emit_signal(w.id, SIGNAL_CLICKED(), if w.selected_index == 1 then "checked" else "unchecked" end if)
        return true

    elseif w.widget_type == WIDGET_DROPDOWN() then
        // Handle dropdown clicks
        w.selected_index = 1 - w.selected_index  // Simple toggle
        emit_signal(w.id, SIGNAL_CLICKED(), if w.selected_index == 1 then "open" else "closed" end if)
        return true

    end if

    return false
end function

pub function editor_get_position_from_line_col(content: text, line: i32, col: i32) -> i32
    let mut current_line = 0
    let mut current_col = 0
    let mut pos = 0
    let len = string_utils.length(content)

    while pos < len do
        if current_line == line and current_col >= col then
            return pos
        end if

        if string_utils.char_at(content, pos) == "\n" then
            current_line = current_line + 1
            current_col = 0
        else
            current_col = current_col + 1
        end if

        pos = pos + 1
    end while

    return len
end function

pub function count_visible_nodes(nodes: tree_node_list, expanded_str: text) -> i32
    let mut count = 0
    let mut i = 0
    while i < nodes.count do
        if treeview_is_visible(nodes, expanded_str, i) then
            count = count + 1
        end if
        i = i + 1
    end while
    return count
end function

pub function draw_widget(handle: i32, w: widget_data)
    if not w.visible then return end if
    if w.widget_type == WIDGET_BUTTON() then
        # Draw button background
        let mut btn_bg_color = theme.button_bg()
        if not w.enabled then btn_bg_color = theme.disabled_bg() end if
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, btn_bg_color)
        # Draw border
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.button_border())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.button_border())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.button_border())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.button_border())
        # Draw text centered
        let text_x = w.x + (w.width / 2) - (string_utils.length(w.text) * 4)  # rough centering
        let text_y = w.y + (w.height / 2) - 4
        winplat.draw_text(handle, text_x, text_y, w.text, theme.text_primary())
    elseif w.widget_type == WIDGET_TEXT_FIELD() then
        // Draw background
        let mut tf_bg = theme.textfield_bg()
        if not w.enabled then tf_bg = theme.disabled_bg() end if
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, tf_bg)
        // Draw border (highlighted if focused)
        let mut border_color = theme.textfield_border()
        if w.id == focused_widget_id then
            border_color = theme.focus_ring()
        end if
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, border_color)
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, border_color)
        // Draw text
        winplat.draw_text(handle, w.x + 4, w.y + (w.height / 2) - 4, w.text, theme.text_primary())

        // Draw cursor if focused and caret is blinking (visible)
        if w.id == focused_widget_id and w.caret_blink_state then
            let cursor_x = w.x + 4 + (w.cursor_pos * 8) // Assume monospaced font, 8 pixels per char
            winplat.fill_rect(handle, cursor_x, w.y + 4, 2, w.height - 8, theme.text_primary())
        end if
    elseif w.widget_type == WIDGET_LABEL() then
        # Draw text only
        winplat.draw_text(handle, w.x, w.y, w.text, theme.text_primary())
    elseif w.widget_type == WIDGET_DROPDOWN() then
        # Draw dropdown background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.button_bg())
        # Draw border
        let mut dd_border = theme.button_border()
        if w.id == focused_widget_id then dd_border = theme.focus_ring() end if
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, dd_border)
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, dd_border)
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, dd_border)
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, dd_border)
        # Draw selected text
        winplat.draw_text(handle, w.x + 4, w.y + (w.height / 2) - 4, w.text, theme.text_primary())
        # Draw dropdown arrow
        winplat.draw_text(handle, w.x + w.width - 14, w.y + (w.height / 2) - 4, "v", theme.dropdown_arrow())
    elseif w.widget_type == WIDGET_MENU() then
        # Draw menu background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.menu_bg())
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.border_primary())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.border_primary())
        # Draw menu items individually
        let parts = string_utils.split(w.options, ",")
        let item_height = 20
        let mut item_y = w.y + 2
        let mut i = 0
        while i < parts.count do
            let item_text = string_utils.trim(parts.items[i])
            if i == w.selected_index then
                winplat.fill_rect(handle, w.x + 2, item_y, w.width - 4, item_height, theme.menu_hover())
                winplat.draw_text(handle, w.x + 8, item_y + 3, item_text, theme.text_selected())
            else
                winplat.draw_text(handle, w.x + 8, item_y + 3, item_text, theme.text_primary())
            end if
            item_y = item_y + item_height
            i = i + 1
        end while
    elseif w.widget_type == WIDGET_CHECKBOX() then
        # Draw checkbox square
        let cb_y = w.y + (w.height / 2) - 8
        winplat.fill_rect(handle, w.x, cb_y, 16, 16, theme.textfield_bg())
        winplat.fill_rect(handle, w.x, cb_y, 16, 1, theme.textfield_border())
        winplat.fill_rect(handle, w.x, cb_y + 15, 16, 1, theme.textfield_border())
        winplat.fill_rect(handle, w.x, cb_y, 1, 16, theme.textfield_border())
        winplat.fill_rect(handle, w.x + 15, cb_y, 1, 16, theme.textfield_border())
        if w.checked then
            winplat.fill_rect(handle, w.x + 3, cb_y + 3, 10, 10, theme.checkbox_check_color())
        end if
        # Draw label text
        let mut cb_text_color = theme.text_primary()
        if not w.enabled then cb_text_color = theme.disabled_text() end if
        winplat.draw_text(handle, w.x + 22, cb_y + 2, w.text, cb_text_color)
    elseif w.widget_type == WIDGET_RADIOBUTTON() then
        # Draw radio circle (rendered as rounded square)
        let rb_y = w.y + (w.height / 2) - 8
        winplat.fill_rect(handle, w.x, rb_y, 16, 16, theme.textfield_bg())
        winplat.fill_rect(handle, w.x, rb_y, 16, 1, theme.textfield_border())
        winplat.fill_rect(handle, w.x, rb_y + 15, 16, 1, theme.textfield_border())
        winplat.fill_rect(handle, w.x, rb_y, 1, 16, theme.textfield_border())
        winplat.fill_rect(handle, w.x + 15, rb_y, 1, 16, theme.textfield_border())
        if w.checked then
            winplat.fill_rect(handle, w.x + 4, rb_y + 4, 8, 8, theme.radio_fill_color())
        end if
        # Draw label text
        let mut rb_text_color = theme.text_primary()
        if not w.enabled then rb_text_color = theme.disabled_text() end if
        winplat.draw_text(handle, w.x + 22, rb_y + 2, w.text, rb_text_color)
    elseif w.widget_type == WIDGET_STATUSBAR() then
        # Draw status bar background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.bg_status_bar())
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        # Draw text
        winplat.draw_text(handle, w.x + 2, w.y + 2, w.text, theme.text_primary())
    elseif w.widget_type == WIDGET_SLIDER() then
        // Draw slider track
        let track_y = w.y + (w.height / 2) - 2
        winplat.fill_rect(handle, w.x, track_y, w.width, 4, theme.slider_track())
        // Draw filled portion
        let fill_width = (w.width * w.selected_index) / 100
        winplat.fill_rect(handle, w.x, track_y, fill_width, 4, theme.slider_track_fill())
        // Draw thumb
        let thumb_x = w.x + fill_width - 6
        let thumb_y = w.y + (w.height / 2) - 8
        winplat.fill_rect(handle, thumb_x, thumb_y, 12, 16, theme.slider_thumb())
        // Draw border around thumb
        winplat.fill_rect(handle, thumb_x, thumb_y, 12, 1, theme.border_primary())
        winplat.fill_rect(handle, thumb_x, thumb_y + 15, 12, 1, theme.border_primary())
        winplat.fill_rect(handle, thumb_x, thumb_y, 1, 16, theme.border_primary())
        winplat.fill_rect(handle, thumb_x + 11, thumb_y, 1, 16, theme.border_primary())
        // Draw focus ring if focused
        if w.id == focused_widget_id then
            winplat.fill_rect(handle, w.x - 1, w.y - 1, w.width + 2, 1, theme.focus_ring())
            winplat.fill_rect(handle, w.x - 1, w.y + w.height, w.width + 2, 1, theme.focus_ring())
            winplat.fill_rect(handle, w.x - 1, w.y - 1, 1, w.height + 2, theme.focus_ring())
            winplat.fill_rect(handle, w.x + w.width, w.y - 1, 1, w.height + 2, theme.focus_ring())
        end if
    elseif w.widget_type == WIDGET_PROGRESSBAR() then
        // Draw progress bar background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.progress_bg())
        // Draw border
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.border_primary())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.border_primary())
        // Draw fill
        let fill_w = ((w.width - 4) * w.selected_index) / 100
        if fill_w > 0 then
            winplat.fill_rect(handle, w.x + 2, w.y + 2, fill_w, w.height - 4, theme.progress_fill())
        end if
        // Draw percentage text centered
        let pct_text = string_utils.int_to_text(w.selected_index) + "%"
        let text_x = w.x + (w.width / 2) - (string_utils.length(pct_text) * 4)
        let text_y = w.y + (w.height / 2) - 4
        winplat.draw_text(handle, text_x, text_y, pct_text, theme.text_primary())
    elseif w.widget_type == WIDGET_LISTBOX() then
        // Draw listbox background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.listbox_bg())
        // Draw border
        let mut border_color = theme.border_primary()
        if w.id == focused_widget_id then
            border_color = theme.focus_ring()
        end if
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, border_color)
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, border_color)
        // Draw items
        let parts = string_utils.split(w.options, ",")
        let item_height = 18
        let mut item_y = w.y + 2
        let mut i = 0
        while i < parts.count and item_y + item_height < w.y + w.height do
            let item_text = string_utils.trim(parts.items[i])
            if i == w.selected_index then
                // Draw selected background
                winplat.fill_rect(handle, w.x + 2, item_y, w.width - 4, item_height, theme.listbox_selected_bg())
                winplat.draw_text(handle, w.x + 6, item_y + 2, item_text, theme.text_selected())
            else
                winplat.draw_text(handle, w.x + 6, item_y + 2, item_text, theme.text_primary())
            end if
            item_y = item_y + item_height
            i = i + 1
        end while
    elseif w.widget_type == WIDGET_TOOLBAR() then
        // Draw toolbar background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.toolbar_bg())
        // Draw bottom border
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        // Draw toolbar buttons
        let parts = string_utils.split(w.options, ",")
        let btn_padding = 4
        let mut btn_x = w.x + btn_padding
        let btn_y = w.y + 2
        let btn_height = w.height - 6
        let mut i = 0
        while i < parts.count do
            let btn_text = string_utils.trim(parts.items[i])
            let btn_width = string_utils.length(btn_text) * 8 + 12
            // Draw button background
            let mut btn_bg = theme.button_bg()
            if i == w.selected_index then
                btn_bg = theme.button_bg_active()
            end if
            winplat.fill_rect(handle, btn_x, btn_y, btn_width, btn_height, btn_bg)
            // Draw button border
            winplat.fill_rect(handle, btn_x, btn_y, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, btn_x, btn_y + btn_height - 1, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, btn_x, btn_y, 1, btn_height, theme.button_border())
            winplat.fill_rect(handle, btn_x + btn_width - 1, btn_y, 1, btn_height, theme.button_border())
            // Draw button text
            winplat.draw_text(handle, btn_x + 6, btn_y + (btn_height / 2) - 4, btn_text, theme.text_primary())
            btn_x = btn_x + btn_width + btn_padding
            i = i + 1
        end while
    elseif w.widget_type == WIDGET_PANEL() then
        // Draw panel background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.panel_bg())
        // Draw border
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.border_primary())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.border_primary())
        // Draw header if title is set
        if string_utils.length(w.text) > 0 then
            let header_height = 22
            winplat.fill_rect(handle, w.x + 1, w.y + 1, w.width - 2, header_height, theme.panel_header_bg())
            winplat.fill_rect(handle, w.x, w.y + header_height, w.width, 1, theme.border_primary())
            winplat.draw_text(handle, w.x + 6, w.y + 4, w.text, theme.text_primary())
        end if
    elseif w.widget_type == WIDGET_TEXT_EDITOR() then
        // Draw editor background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.editor_bg())
        // Draw border
        let mut border_color = theme.border_primary()
        if w.id == focused_widget_id then
            border_color = theme.focus_ring()
        end if
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, border_color)
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, border_color)
        // Gutter and content area
        let show_gutter = string_utils.contains(w.options, "line_numbers")
        let gutter_width = 48
        let mut content_x = w.x + 4
        if show_gutter then
            // Draw gutter background
            winplat.fill_rect(handle, w.x + 1, w.y + 1, gutter_width - 1, w.height - 2, theme.editor_gutter_bg())
            // Draw gutter border
            winplat.fill_rect(handle, w.x + gutter_width, w.y, 1, w.height, theme.border_primary())
            content_x = w.x + gutter_width + 4
        end if
        // Draw visible lines
        let line_height = 16
        let visible = editor_visible_lines(w.height)
        let total_lines = editor_get_line_count(w.text)
        let scroll_offset = w.selected_index
        let cursor_line = editor_cursor_to_line(w.text, w.cursor_pos)
        let cursor_col = editor_cursor_to_col(w.text, w.cursor_pos)
        let mut draw_line = 0
        while draw_line < visible and (scroll_offset + draw_line) < total_lines do
            let line_num = scroll_offset + draw_line
            let line_y = w.y + 2 + (draw_line * line_height)
            // Draw line number in gutter
            if show_gutter then
                let num_str = string_utils.int_to_text(line_num + 1)
                let num_x = w.x + gutter_width - 8 - (string_utils.length(num_str) * 8)
                let mut num_color = theme.editor_line_number()
                if line_num == cursor_line then
                    num_color = theme.text_primary()
                end if
                winplat.draw_text(handle, num_x, line_y, num_str, num_color)
            end if
            // Highlight error lines (1-based line numbers in error_lines, line_num is 0-based)
            if has_error_on_line(line_num + 1) then
                winplat.fill_rect(handle, content_x - 2, line_y, w.width - (content_x - w.x) - 2, line_height, error_line_bg())
                // Also highlight gutter with red indicator
                if show_gutter then
                    winplat.fill_rect(handle, w.x + 1, line_y, 3, line_height, 0xFF4444)
                end if
            end if
            // Highlight current line
            if line_num == cursor_line then
                winplat.fill_rect(handle, content_x - 2, line_y, w.width - (content_x - w.x) - 2, line_height, theme.editor_current_line())
            end if
            // Draw selection highlight if active for this line
            if w.selection_active then
                let line_start = editor_line_start(w.text, line_num)
                let line_end_pos = editor_line_end(w.text, line_num)
                let mut sel_start = w.selection_start
                let mut sel_end = w.selection_end
                if sel_start > sel_end then
                    let temp = sel_start
                    sel_start = sel_end
                    sel_end = temp
                end if

                // Check if selection overlaps with this line
                if not (sel_end < line_start or sel_start > line_end_pos) then
                    let mut sel_start_col = 0
                    let mut sel_end_col = line_end_pos - line_start

                    if sel_start > line_start then
                        sel_start_col = sel_start - line_start
                    end if

                    if sel_end < line_end_pos then
                        sel_end_col = sel_end - line_start
                    end if

                    let sel_x = content_x + (sel_start_col * 8)
                    let sel_width = (sel_end_col - sel_start_col) * 8
                    winplat.fill_rect(handle, sel_x, line_y, sel_width, line_height, theme.text_selection_bg())
                end if
            end if

            // Draw line text with syntax highlighting
            let line_text = editor_get_line(w.text, line_num)
            let use_syntax = string_utils.contains(w.options, "syntax")
            if use_syntax then
                syntax_highlighter.highlight_line(line_text, handle, content_x, line_y)
            else
                winplat.draw_text(handle, content_x, line_y, line_text, theme.text_primary())
            end if
            draw_line = draw_line + 1
        end while
        // Draw cursor if focused
        if w.id == focused_widget_id and w.caret_blink_state then
            let cursor_draw_line = cursor_line - scroll_offset
            if cursor_draw_line >= 0 and cursor_draw_line < visible then
                let cursor_x = content_x + (cursor_col * 8)
                let cursor_y = w.y + 2 + (cursor_draw_line * line_height)
                winplat.fill_rect(handle, cursor_x, cursor_y, 2, line_height, theme.cursor_color())
            end if
        end if
        // Draw scroll indicator if content exceeds visible area
        if total_lines > visible then
            let scrollbar_x = w.x + w.width - 12
            let scrollbar_h = w.height - 4
            let thumb_h = (visible * scrollbar_h) / total_lines
            let mut th = thumb_h
            if th < 20 then th = 20 end if
            let thumb_y = w.y + 2 + (scroll_offset * (scrollbar_h - th)) / (total_lines - visible)
            // Scrollbar track
            winplat.fill_rect(handle, scrollbar_x, w.y + 2, 10, scrollbar_h, theme.editor_gutter_bg())
            // Scrollbar thumb
            winplat.fill_rect(handle, scrollbar_x + 2, thumb_y, 6, th, theme.slider_thumb())
        end if

        // Draw code completion popup if visible and this editor is focused
        if w.id == focused_widget_id and code_completion.is_completion_visible() then
            // Calculate cursor position for popup
            let cursor_line = editor_cursor_to_line(w.text, w.cursor_pos)
            let cursor_col = editor_cursor_to_col(w.text, w.cursor_pos)
            let cursor_draw_line = cursor_line - scroll_offset
            if cursor_draw_line >= 0 and cursor_draw_line < visible then
                let show_gutter = string_utils.contains(w.options, "line_numbers")
                let gutter_width = 48
                let mut content_x = w.x + 4
                if show_gutter then content_x = w.x + gutter_width + 4 end if
                let cursor_x = content_x + (cursor_col * 8)
                let cursor_y = w.y + 2 + (cursor_draw_line * 16)

                // Update completion popup position
                code_completion.trigger_completion(w.text, w.cursor_pos, cursor_x, cursor_y + 20)
                code_completion.draw_completion_popup(handle)
            end if
        end if
    elseif w.widget_type == WIDGET_TABBAR() then
        // Draw tab bar background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.tab_bar_bg())
        // Draw bottom border
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        // Draw individual tabs
        let parts = string_utils.split(w.options, ",")
        let tab_padding = 4
        let mut tab_x = w.x + 2
        let tab_height = w.height - 4
        let mut i = 0
        while i < parts.count do
            let tab_label = string_utils.trim(parts.items[i])
            let tab_text_width = string_utils.length(tab_label) * 8
            let tab_width = tab_text_width + 20
            let mut close_btn_width = 0
            if w.checked then close_btn_width = 16 end if
            let full_tab_width = tab_width + close_btn_width
            if i == w.selected_index then
                // Active tab - draw with active background and no bottom border
                winplat.fill_rect(handle, tab_x, w.y + 2, full_tab_width, tab_height, theme.tab_active_bg())
                // Top border highlight
                winplat.fill_rect(handle, tab_x, w.y + 2, full_tab_width, 2, theme.focus_ring())
                // Side borders
                winplat.fill_rect(handle, tab_x, w.y + 2, 1, tab_height, theme.border_primary())
                winplat.fill_rect(handle, tab_x + full_tab_width - 1, w.y + 2, 1, tab_height, theme.border_primary())
                // Draw text
                winplat.draw_text(handle, tab_x + 10, w.y + (w.height / 2) - 4, tab_label, theme.text_primary())
            else
                // Inactive tab
                winplat.fill_rect(handle, tab_x, w.y + 2, full_tab_width, tab_height, theme.tab_inactive_bg())
                // Bottom border
                winplat.fill_rect(handle, tab_x, w.y + w.height - 2, full_tab_width, 1, theme.border_primary())
                // Draw text
                winplat.draw_text(handle, tab_x + 10, w.y + (w.height / 2) - 4, tab_label, theme.text_secondary())
            end if
            // Draw close button if closeable
            if w.checked then
                let close_x = tab_x + tab_width
                let close_y = w.y + (w.height / 2) - 4
                winplat.draw_text(handle, close_x + 2, close_y, "x", theme.text_secondary())
            end if
            tab_x = tab_x + full_tab_width + tab_padding
            i = i + 1
        end while
    elseif w.widget_type == WIDGET_TREEVIEW() then
        // Draw treeview background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.treeview_bg())
        // Draw border
        let mut border_color = theme.border_primary()
        if w.id == focused_widget_id then
            border_color = theme.focus_ring()
        end if
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, border_color)
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, border_color)
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, border_color)
        // Draw visible tree nodes
        let nodes = treeview_parse_nodes(w.options)
        let item_height = 20
        let mut draw_y = w.y + 2
        let mut i = 0
        while i < nodes.count and draw_y + item_height < w.y + w.height do
            if treeview_is_visible(nodes, w.text, i) then
                let node = nodes.items[i]
                let indent = node.depth * 20 + 4
                let node_x = w.x + indent
                // Draw selection background
                if i == w.selected_index then
                    winplat.fill_rect(handle, w.x + 2, draw_y, w.width - 4, item_height, theme.treeview_selected_bg())
                end if
                // Draw expand/collapse indicator
                if treeview_has_children(nodes, i) then
                    if treeview_is_expanded(w.text, i) then
                        winplat.draw_text(handle, node_x, draw_y + 2, "v", theme.treeview_arrow())
                    else
                        winplat.draw_text(handle, node_x, draw_y + 2, ">", theme.treeview_arrow())
                    end if
                end if
                // Draw node label
                let label_x = node_x + 16
                let mut label_color = theme.text_primary()
                if i == w.selected_index then
                    label_color = theme.text_selected()
                end if
                winplat.draw_text(handle, label_x, draw_y + 2, node.label, label_color)
                // Draw indent guide lines
                let mut d = 0
                while d < node.depth do
                    let guide_x = w.x + (d * 20) + 12
                    winplat.fill_rect(handle, guide_x, draw_y, 1, item_height, theme.treeview_indent_guide())
                    d = d + 1
                end while
                draw_y = draw_y + item_height
            end if
            i = i + 1
        end while
    elseif w.widget_type == WIDGET_SCROLL_CONTAINER() then
        // Draw scroll container background
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.panel_bg())
        // Draw border
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.border_primary())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.border_primary())
        // Draw vertical scrollbar if needed
        let needs_vscroll = scroll_container_needs_vscroll(w)
        if needs_vscroll then
            let scrollbar_width = 12
            let scrollbar_x = w.x + w.width - scrollbar_width
            let content_h = scroll_container_get_content_height(w)
            let max_scroll = scroll_container_max_scroll_y(w)
            // Scrollbar track
            winplat.fill_rect(handle, scrollbar_x, w.y + 1, scrollbar_width - 1, w.height - 2, theme.editor_gutter_bg())
            // Scrollbar thumb
            let thumb_h = (w.height * w.height) / content_h
            let mut th = thumb_h
            if th < 20 then th = 20 end if
            let mut thumb_y = w.y + 1
            if max_scroll > 0 then
                thumb_y = w.y + 1 + (w.selected_index * (w.height - 2 - th)) / max_scroll
            end if
            winplat.fill_rect(handle, scrollbar_x + 2, thumb_y, scrollbar_width - 5, th, theme.slider_thumb())
            // Scrollbar border
            winplat.fill_rect(handle, scrollbar_x, w.y, 1, w.height, theme.border_primary())
        end if
        // Draw horizontal scrollbar if needed
        let needs_hscroll = scroll_container_needs_hscroll(w)
        if needs_hscroll then
            let scrollbar_height = 12
            let scrollbar_y = w.y + w.height - scrollbar_height
            let content_w = scroll_container_get_content_width(w)
            let max_scroll_x = scroll_container_max_scroll_x(w)
            let avail_width = w.width
            let mut aw = avail_width
            if needs_vscroll then aw = aw - 12 end if
            // Track
            winplat.fill_rect(handle, w.x + 1, scrollbar_y, aw - 1, scrollbar_height - 1, theme.editor_gutter_bg())
            // Thumb
            let thumb_w = (aw * aw) / content_w
            let mut tw = thumb_w
            if tw < 20 then tw = 20 end if
            let mut thumb_x = w.x + 1
            if max_scroll_x > 0 then
                thumb_x = w.x + 1 + (w.cursor_pos * (aw - 2 - tw)) / max_scroll_x
            end if
            winplat.fill_rect(handle, thumb_x, scrollbar_y + 2, tw, scrollbar_height - 5, theme.slider_thumb())
            // Border
            winplat.fill_rect(handle, w.x, scrollbar_y, w.width, 1, theme.border_primary())
        end if
    elseif w.widget_type == WIDGET_DIALOG() then
        // Draw semi-transparent overlay (darkened background)
        winplat.fill_rect(handle, 0, 0, 2000, 2000, theme.dialog_overlay())
        // Draw dialog panel
        winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.panel_bg())
        // Draw border
        winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.border_primary())
        winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.border_primary())
        winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.border_primary())
        // Draw title bar
        let title_height = 28
        winplat.fill_rect(handle, w.x + 1, w.y + 1, w.width - 2, title_height, theme.panel_header_bg())
        winplat.fill_rect(handle, w.x, w.y + title_height, w.width, 1, theme.border_primary())
        winplat.draw_text(handle, w.x + 10, w.y + 6, w.text, theme.text_primary())
        // Draw close button [X]
        let close_x = w.x + w.width - 25
        winplat.draw_text(handle, close_x, w.y + 6, "X", theme.text_secondary())
        // Draw message
        let message = dialog_get_message(w)
        let msg_y = w.y + title_height + 20
        winplat.draw_text(handle, w.x + 20, msg_y, message, theme.text_primary())
        // Draw buttons based on dialog type
        let dlg_type = dialog_get_type(w)
        let btn_y = w.y + w.height - 50
        let btn_height = 30
        let btn_width = 100
        if dlg_type == "confirm" then
            // OK button
            let ok_x = w.x + w.width / 2 - 110
            winplat.fill_rect(handle, ok_x, btn_y, btn_width, btn_height, theme.button_bg())
            winplat.fill_rect(handle, ok_x, btn_y, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, ok_x, btn_y + btn_height - 1, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, ok_x, btn_y, 1, btn_height, theme.button_border())
            winplat.fill_rect(handle, ok_x + btn_width - 1, btn_y, 1, btn_height, theme.button_border())
            winplat.draw_text(handle, ok_x + 40, btn_y + 8, "OK", theme.text_primary())
            // Cancel button
            let cancel_x = w.x + w.width / 2 + 10
            winplat.fill_rect(handle, cancel_x, btn_y, btn_width, btn_height, theme.button_bg())
            winplat.fill_rect(handle, cancel_x, btn_y, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, cancel_x, btn_y + btn_height - 1, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, cancel_x, btn_y, 1, btn_height, theme.button_border())
            winplat.fill_rect(handle, cancel_x + btn_width - 1, btn_y, 1, btn_height, theme.button_border())
            winplat.draw_text(handle, cancel_x + 24, btn_y + 8, "Cancel", theme.text_primary())
        else
            // Info dialog - just OK
            let ok_x = w.x + w.width / 2 - 50
            winplat.fill_rect(handle, ok_x, btn_y, btn_width, btn_height, theme.button_bg())
            winplat.fill_rect(handle, ok_x, btn_y, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, ok_x, btn_y + btn_height - 1, btn_width, 1, theme.button_border())
            winplat.fill_rect(handle, ok_x, btn_y, 1, btn_height, theme.button_border())
            winplat.fill_rect(handle, ok_x + btn_width - 1, btn_y, 1, btn_height, theme.button_border())
            winplat.draw_text(handle, ok_x + 40, btn_y + 8, "OK", theme.text_primary())
        end if
    elseif w.widget_type == WIDGET_TOOLTIP() then
        // Draw tooltip if visible
        if w.visible then
            // Draw tooltip background with rounded corners effect
            winplat.fill_rect(handle, w.x + 1, w.y + 1, w.width, w.height, theme.tooltip_shadow())
            winplat.fill_rect(handle, w.x, w.y, w.width, w.height, theme.tooltip_bg())
            // Draw tooltip border
            winplat.fill_rect(handle, w.x, w.y, w.width, 1, theme.tooltip_border())
            winplat.fill_rect(handle, w.x, w.y + w.height - 1, w.width, 1, theme.tooltip_border())
            winplat.fill_rect(handle, w.x, w.y, 1, w.height, theme.tooltip_border())
            winplat.fill_rect(handle, w.x + w.width - 1, w.y, 1, w.height, theme.tooltip_border())
            // Draw tooltip text
            winplat.draw_text(handle, w.x + 8, w.y + 4, w.text, theme.tooltip_text())
        end if
    end if
end function

// =========================================================================
// Simple Context Menu System
// =========================================================================

pub function show_context_menu(x: i32, y: i32, items: array<text>, actions: array<i32>)
    context_menu_visible = true
    context_menu_x = x
    context_menu_y = y
    context_menu_items = items
    context_menu_actions = actions
    context_menu_selected = -1
end function

pub function hide_context_menu()
    context_menu_visible = false
    context_menu_selected = -1
end function

pub function is_context_menu_visible() -> bool
    return context_menu_visible
end function

pub function draw_context_menu(handle: i32)
    if not context_menu_visible then return end if

    let item_height = 25
    let padding = 8
    let mut max_width = 120

    // Calculate maximum width needed
    let mut i = 0
    while i < context_menu_items.count do
        let text_width = string_utils.length(context_menu_items[i]) * 8 + padding * 2
        if text_width > max_width then max_width = text_width end if
        i = i + 1
    end while

    let menu_height = context_menu_items.count * item_height + 4

    // Ensure menu stays on screen
    let menu_x = context_menu_x
    let menu_y = context_menu_y

    // Draw background
    winplat.fill_rect(handle, menu_x, menu_y, max_width, menu_height, theme.menu_background())

    // Draw border
    winplat.fill_rect(handle, menu_x, menu_y, max_width, menu_height, theme.border_primary())

    // Draw items
    let mut j = 0
    while j < context_menu_items.count do
        let item_y = menu_y + 2 + j * item_height

        // Highlight selected item
        if j == context_menu_selected then
            winplat.fill_rect(handle, menu_x + 2, item_y, max_width - 4, item_height - 2, theme.menu_selected())
        end if

        // Draw text
        winplat.draw_text(handle, menu_x + padding, item_y + 5, context_menu_items[j], theme.menu_text())

        j = j + 1
    end while
end function

pub function handle_context_menu_click(x: i32, y: i32) -> i32
    if not context_menu_visible then return -1 end if

    let item_height = 25
    let menu_x = context_menu_x
    let menu_y = context_menu_y
    let max_width = 120

    // Check if click is within menu bounds
    if x >= menu_x and x < menu_x + max_width and y >= menu_y and y < menu_y + context_menu_items.count * item_height + 4 then
        let clicked_item = (y - menu_y - 2) / item_height
        if clicked_item >= 0 and clicked_item < context_menu_items.count then
            let action = context_menu_actions[clicked_item]
            hide_context_menu()
            return action
        end if
    else
        hide_context_menu()
    end if

    return -1
end function

pub function handle_context_menu_motion(x: i32, y: i32)
    if not context_menu_visible then return end if

    let item_height = 25
    let menu_x = context_menu_x
    let menu_y = context_menu_y
    let max_width = 120

    // Check if mouse is within menu bounds
    if x >= menu_x and x < menu_x + max_width and y >= menu_y and y < menu_y + context_menu_items.count * item_height + 4 then
        let hover_item = (y - menu_y - 2) / item_height
        if hover_item >= 0 and hover_item < context_menu_items.count then
            context_menu_selected = hover_item
        end if
    else
        context_menu_selected = -1
    end if
end function

// Text editor context menu actions
pub function CONTEXT_MENU_CUT() -> i32 return 1 end function
pub function CONTEXT_MENU_COPY() -> i32 return 2 end function
pub function CONTEXT_MENU_PASTE() -> i32 return 3 end function
pub function CONTEXT_MENU_SELECT_ALL() -> i32 return 4 end function
pub function CONTEXT_MENU_UNDO() -> i32 return 5 end function
pub function CONTEXT_MENU_REDO() -> i32 return 6 end function

pub function show_text_editor_context_menu(editor_widget: widget_data, x: i32, y: i32)
    let items = ["Cut", "Copy", "Paste", "", "Select All", "", "Undo", "Redo"]
    let actions = [CONTEXT_MENU_CUT(), CONTEXT_MENU_COPY(), CONTEXT_MENU_PASTE(), 0, CONTEXT_MENU_SELECT_ALL(), 0, CONTEXT_MENU_UNDO(), CONTEXT_MENU_REDO()]

    show_context_menu(x, y, items, actions)
end function

// =========================================================================
// Keyboard Shortcuts System
// =========================================================================

pub function handle_keyboard_shortcut(event: winplat::event, mut w: widget_data) -> bool
    let key = event.key_code
    let mut shortcut_id = 0

    // Handle special keys first (non-Ctrl shortcuts)
    if key == 0x1001B then  // Escape key
        shortcut_id = KEY_ESC()
    elseif key == 0x10051 then  // F3 key
        shortcut_id = KEY_F3()
    // Then check Ctrl+Key combinations
    elseif (event.modifiers and winplat::MODIFIER_CTRL()) != 0 then
        // Map Ctrl+Key combinations to shortcut IDs
        if key == 0x41 then shortcut_id = KEY_CTRL_A()  // Ctrl+A (Select All)
        elseif key == 0x43 then shortcut_id = KEY_CTRL_C()  // Ctrl+C (Copy)
        elseif key == 0x56 then shortcut_id = KEY_CTRL_V()  // Ctrl+V (Paste)
        elseif key == 0x58 then shortcut_id = KEY_CTRL_X()  // Ctrl+X (Cut)
        elseif key == 0x5A then shortcut_id = KEY_CTRL_Z()  // Ctrl+Z (Undo)
        elseif key == 0x59 then shortcut_id = KEY_CTRL_Y()  // Ctrl+Y (Redo)
        elseif key == 0x46 then shortcut_id = KEY_CTRL_F()  // Ctrl+F (Find)
        elseif key == 0x47 then shortcut_id = KEY_CTRL_G()  // Ctrl+G (Find Next)
        elseif key == 0x48 then shortcut_id = KEY_CTRL_H()  // Ctrl+H (Replace)
        elseif key == 0x57 then shortcut_id = KEY_CTRL_W()  // Ctrl+W (Close Tab)
        elseif key == 0x54 then shortcut_id = KEY_CTRL_T()  // Ctrl+T (New Tab)
        elseif key == 0x53 then shortcut_id = KEY_CTRL_S()  // Ctrl+S (Save)
        end if
    end if

    if shortcut_id > 0 then
        return process_keyboard_shortcut_action(shortcut_id, w)
    end if

    return false
end function

pub function process_keyboard_shortcut_action(shortcut_id: i32, mut w: widget_data) -> bool
    if w.widget_type == WIDGET_TEXT_EDITOR() then
        if shortcut_id == KEY_CTRL_A() then
            return text_editor_select_all(w)
        elseif shortcut_id == KEY_CTRL_C() then
            return text_editor_copy(w)
        elseif shortcut_id == KEY_CTRL_V() then
            return text_editor_paste(w)
        elseif shortcut_id == KEY_CTRL_X() then
            return text_editor_cut(w)
        elseif shortcut_id == KEY_CTRL_Z() then
            return text_editor_undo(w)
        elseif shortcut_id == KEY_CTRL_Y() then
            return text_editor_redo(w)
        elseif shortcut_id == KEY_CTRL_F() then
            return text_editor_find(w)
        elseif shortcut_id == KEY_CTRL_G() then
            return text_editor_find_next(w)
        elseif shortcut_id == KEY_CTRL_H() then
            return text_editor_replace(w)
        elseif shortcut_id == KEY_CTRL_S() then
            return text_editor_save(w)
        elseif shortcut_id == KEY_CTRL_W() then
            return text_editor_close_tab(w)
        elseif shortcut_id == KEY_CTRL_T() then
            return text_editor_new_tab(w)
        elseif shortcut_id == KEY_F3() then
            return text_editor_find_next(w)
        elseif shortcut_id == KEY_ESC() then
            hide_context_menu()
            emit_signal(w.id, SIGNAL_CLICKED(), "escape")
            return true
        end if
    end if

    return false
end function

// Text editor keyboard shortcut implementations
pub function text_editor_select_all(mut w: widget_data) -> bool
    w.selection_start = 0
    w.selection_end = string_utils.length(w.text)
    w.selection_active = true
    emit_signal(w.id, SIGNAL_SELECTED_CHANGED(), "select_all")
    return true
end function

pub function text_editor_copy(mut w: widget_data) -> bool
    if w.selection_active then
        let mut start = w.selection_start
        let mut end_pos = w.selection_end
        if start > end_pos then
            let temp = start
            start = end_pos
            end_pos = temp
        end if

        let selected_text = string_utils.substring(w.text, start, end_pos)
        winplat.set_clipboard_text(selected_text)
        emit_signal(w.id, SIGNAL_CLICKED(), "copy")
        return true
    end if
    return false
end function

pub function text_editor_cut(mut w: widget_data) -> bool
    if w.selection_active then
        text_editor_copy(w)
        text_editor_delete_selection(w)
        emit_signal(w.id, SIGNAL_CLICKED(), "cut")
        return true
    end if
    return false
end function

pub function text_editor_paste(mut w: widget_data) -> bool
    undo_save_state(w)
    let clipboard_text = winplat.get_clipboard_text()

    // Delete current selection if any
    if w.selection_active then
        text_editor_delete_selection(w)
    end if

    // Insert clipboard text at cursor position
    let before_cursor = string_utils.substring(w.text, 0, w.cursor_pos)
    let after_cursor = string_utils.substring(w.text, w.cursor_pos, string_utils.length(w.text))
    w.text = before_cursor + clipboard_text + after_cursor
    w.cursor_pos = w.cursor_pos + string_utils.length(clipboard_text)

    emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
    emit_signal(w.id, SIGNAL_CLICKED(), "paste")
    return true
end function

pub function text_editor_undo(mut w: widget_data) -> bool
    return undo_perform_undo(w)
end function

pub function text_editor_redo(mut w: widget_data) -> bool
    return undo_perform_redo(w)
end function

pub function text_editor_find(mut w: widget_data) -> bool
    // Emit find signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "find")
    return true
end function

pub function text_editor_find_next(mut w: widget_data) -> bool
    // Emit find_next signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "find_next")
    return true
end function

pub function text_editor_replace(mut w: widget_data) -> bool
    // Emit replace signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "replace")
    return true
end function

pub function text_editor_save(mut w: widget_data) -> bool
    // Emit save signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "save")
    return true
end function

pub function text_editor_close_tab(mut w: widget_data) -> bool
    // Emit close_tab signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "close_tab")
    return true
end function

pub function text_editor_new_tab(mut w: widget_data) -> bool
    // Emit new_tab signal for UI to handle
    emit_signal(w.id, SIGNAL_CLICKED(), "new_tab")
    return true
end function

pub function text_editor_delete_selection(mut w: widget_data) -> bool
    if w.selection_active then
        let mut start = w.selection_start
        let mut end_pos = w.selection_end
        if start > end_pos then
            let temp = start
            start = end_pos
            end_pos = temp
        end if

        let before_selection = string_utils.substring(w.text, 0, start)
        let after_selection = string_utils.substring(w.text, end_pos, string_utils.length(w.text))
        w.text = before_selection + after_selection
        w.cursor_pos = start
        w.selection_active = false

        emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
        return true
    end if
    return false
end function

// =========================================================================
// Drag and Drop Core Functions
// =========================================================================

pub function create_drop_zone(widget_id: i32, x: i32, y: i32, width: i32, height: i32, accepts_types: i32, active: bool) -> drop_zone
    let zone = drop_zone {
        widget_id: widget_id,
        x: x, y: y,
        width: width, height: height,
        accepts_types: accepts_types,
        active: active
    }
    return zone
end function

pub function create_drag_data(drag_type: i32, data: text, source_id: i32, preview: text) -> drag_data
    return drag_data {
        drag_type: drag_type,
        data: data,
        source_widget_id: source_id,
        preview_text: preview
    }
end function

pub function start_drag(widget_id: i32, dtype: i32, data: text, start_x: i32, start_y: i32)
    drag_active = true
    drag_widget_id = widget_id
    drag_type = dtype
    drag_text_data = data
    drag_start_x = start_x
    drag_start_y = start_y
    drag_current_x = start_x
    drag_current_y = start_y
    drag_offset_x = 0
    drag_offset_y = 0

    emit_signal(widget_id, SIGNAL_DRAG_STARTED(), data)
end function

pub function update_drag(current_x: i32, current_y: i32)
    if not drag_active then return end if

    drag_current_x = current_x
    drag_current_y = current_y

    // Check if over any drop zone
    let mut i = 0
    while i < drop_zone_count do
        if drop_zones[i].active then
            if is_point_in_rect(current_x, current_y, drop_zones[i].x, drop_zones[i].y, drop_zones[i].width, drop_zones[i].height) then
                if (drop_zones[i].accepts_types and drag_type) != 0 then
                    emit_signal(drop_zones[i].widget_id, SIGNAL_DRAG_ENTER(), drag_text_data)
                end if
            else
                emit_signal(drop_zones[i].widget_id, SIGNAL_DRAG_EXIT(), "")
            end if
        end if
        i = i + 1
    end while
end function

pub function end_drag(end_x: i32, end_y: i32)
    if not drag_active then return end if

    drag_current_x = end_x
    drag_current_y = end_y

    // Check for successful drop
    let mut dropped = false
    let mut i = 0
    while i < drop_zone_count do
        if drop_zones[i].active and is_point_in_rect(end_x, end_y, drop_zones[i].x, drop_zones[i].y, drop_zones[i].width, drop_zones[i].height) then
            if (drop_zones[i].accepts_types and drag_type) != 0 then
                emit_signal(drop_zones[i].widget_id, SIGNAL_DROPPED(), drag_text_data)
                dropped = true
                break
            end if
        end if
        i = i + 1
    end while

    // Emit drag ended signal
    emit_signal(drag_widget_id, SIGNAL_DRAG_ENDED(), if dropped then "success" else "cancelled" end if)

    // Reset drag state
    drag_active = false
    drag_widget_id = -1
    drag_type = DRAG_TYPE_NONE()
    drag_text_data = ""
end function

pub function cancel_drag()
    if drag_active then
        emit_signal(drag_widget_id, SIGNAL_DRAG_ENDED(), "cancelled")

        drag_active = false
        drag_widget_id = -1
        drag_type = DRAG_TYPE_NONE()
        drag_text_data = ""
    end if
end function

pub function is_point_in_rect(x: i32, y: i32, rect_x: i32, rect_y: i32, rect_width: i32, rect_height: i32) -> bool
    return x >= rect_x and x < rect_x + rect_width and y >= rect_y and y < rect_y + rect_height
end function

pub function add_drop_zone(zone: drop_zone)
    if drop_zone_count >= drop_zones.count then
        // Expand array if needed (simplified approach)
        let mut new_zones = drop_zones + [zone]
        drop_zones = new_zones
    else
        drop_zones[drop_zone_count] = zone
    end if
    drop_zone_count = drop_zone_count + 1
end function

pub function remove_drop_zone(widget_id: i32)
    let mut i = 0
    while i < drop_zone_count do
        if drop_zones[i].widget_id == widget_id then
            // Shift remaining zones left
            let mut j = i
            while j < drop_zone_count - 1 do
                drop_zones[j] = drop_zones[j + 1]
                j = j + 1
            end while
            drop_zone_count = drop_zone_count - 1
            break
        end if
        i = i + 1
    end while
end function

pub function get_drag_state() -> drag_data
    return create_drag_data(drag_type, drag_text_data, drag_widget_id, "")
end function

pub function is_drag_active() -> bool
    return drag_active
end function

pub function get_drag_position() -> i32
    return drag_current_x * 10000 + drag_current_y  // Pack x and y into single int (x*10000 + y)
end function

pub function extract_drag_x(packed_pos: i32) -> i32
    return packed_pos / 10000
end function

pub function extract_drag_y(packed_pos: i32) -> i32
    return packed_pos % 10000
end function

// =========================================================================
// Widget-Specific Drag and Drop Implementations
// =========================================================================

pub function handle_widget_drag_start(event: winplat::event, mut w: widget_data) -> bool
    let x = event.mouse_x
    let y = event.mouse_y

    // Check if click is within widget bounds
    if x < w.x or x >= w.x + w.width or y < w.y or y >= w.y + w.height then
        return false
    end if

    if w.widget_type == WIDGET_TEXT_EDITOR() then
        return handle_text_editor_drag_start(x, y, w)
    elseif w.widget_type == WIDGET_BUTTON() then
        return handle_button_drag_start(x, y, w)
    elseif w.widget_type == WIDGET_LABEL() then
        return handle_label_drag_start(x, y, w)
    end if

    return false
end function

pub function handle_text_editor_drag_start(x: i32, y: i32, mut w: widget_data) -> bool
    // Check if clicking on selected text
    if w.selection_active then
        let mut start = w.selection_start
        let mut end_pos = w.selection_end
        if start > end_pos then
            let temp = start
            start = end_pos
            end_pos = temp
        end if

        // Get selected text
        let selected_text = string_utils.substring(w.text, start, end_pos)

        // Start drag operation
        start_drag(w.id, DRAG_TYPE_TEXT(), selected_text, x, y)
        emit_signal(w.id, SIGNAL_CLICKED(), "drag_text_start")
        return true
    end if

    return false
end function

pub function handle_button_drag_start(x: i32, y: i32, mut w: widget_data) -> bool
    // Start drag with button text as data
    start_drag(w.id, DRAG_TYPE_WIDGET(), w.text, x, y)
    emit_signal(w.id, SIGNAL_CLICKED(), "drag_widget_start")
    return true
end function

pub function handle_label_drag_start(x: i32, y: i32, mut w: widget_data) -> bool
    // Start drag with label text as data
    start_drag(w.id, DRAG_TYPE_TEXT(), w.text, x, y)
    emit_signal(w.id, SIGNAL_CLICKED(), "drag_text_start")
    return true
end function

pub function handle_widget_drop(mut w: widget_data, drag_info: drag_data) -> bool
    if w.widget_type == WIDGET_TEXT_EDITOR() then
        return handle_text_editor_drop(w, drag_info)
    elseif w.widget_type == WIDGET_PANEL() then
        return handle_panel_drop(w, drag_info)
    elseif w.widget_type == WIDGET_LABEL() then
        return handle_label_drop(w, drag_info)
    end if

    return false
end function

pub function handle_text_editor_drop(mut w: widget_data, drag_info: drag_data) -> bool
    if drag_info.drag_type == DRAG_TYPE_TEXT() or drag_info.drag_type == DRAG_TYPE_FILE() then
        undo_save_state(w)

        // Delete current selection if any
        if w.selection_active then
            text_editor_delete_selection(w)
        end if

        // Insert dropped text at cursor position
        let before_cursor = string_utils.substring(w.text, 0, w.cursor_pos)
        let after_cursor = string_utils.substring(w.text, w.cursor_pos, string_utils.length(w.text))
        w.text = before_cursor + drag_info.data + after_cursor
        w.cursor_pos = w.cursor_pos + string_utils.length(drag_info.data)

        emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
        emit_signal(w.id, SIGNAL_CLICKED(), "text_dropped")
        return true
    end if

    return false
end function

pub function handle_panel_drop(mut w: widget_data, drag_info: drag_data) -> bool
    // Panel accepts any type - just emit signal with data
    emit_signal(w.id, SIGNAL_CLICKED(), "panel_dropped:" + drag_info.data)
    return true
end function

pub function handle_label_drop(mut w: widget_data, drag_info: drag_data) -> bool
    if drag_info.drag_type == DRAG_TYPE_TEXT() then
        // Update label text with dropped text
        w.text = drag_info.data
        emit_signal(w.id, SIGNAL_TEXT_CHANGED(), w.text)
        emit_signal(w.id, SIGNAL_CLICKED(), "label_dropped")
        return true
    end if

    return false
end function

pub function update_widget_drop_zones(mut w: widget_data)
    // Remove existing drop zone for this widget
    remove_drop_zone(w.id)

    // Add drop zone if widget accepts drops
    let mut accepts = 0
    if w.widget_type == WIDGET_TEXT_EDITOR() then
        accepts = DRAG_TYPE_TEXT() + DRAG_TYPE_FILE()
    elseif w.widget_type == WIDGET_PANEL() then
        accepts = DRAG_TYPE_TEXT() + DRAG_TYPE_WIDGET() + DRAG_TYPE_FILE() + DRAG_TYPE_DATA()
    elseif w.widget_type == WIDGET_LABEL() then
        accepts = DRAG_TYPE_TEXT()
    end if

    if accepts != 0 then
        let zone = create_drop_zone(w.id, w.x, w.y, w.width, w.height, accepts, w.enabled)
        add_drop_zone(zone)
    end if
end function

// =========================================================================
// Drag and Drop Visual Feedback
// =========================================================================

pub function draw_drag_feedback(dc: i32)
    if not drag_active then return end if

    // Draw drag cursor/indicator at current position
    let cursor_size = 16

    // Draw semi-transparent cursor
    winplat.fill_rect(dc, drag_current_x - cursor_size/2, drag_current_y - cursor_size/2, cursor_size, cursor_size, theme.border_primary())

    // Draw preview text if dragging text
    if drag_type == DRAG_TYPE_TEXT() and string_utils.length(drag_text_data) > 0 then
        let mut preview_text = drag_text_data
        if string_utils.length(preview_text) > 30 then
            preview_text = string_utils.substring(preview_text, 0, 30) + "..."
        end if

        // Draw background for text
        let text_width = string_utils.length(preview_text) * 8
        let text_height = 20
        winplat.fill_rect(dc, drag_current_x + 10, drag_current_y + 10, text_width + 4, text_height, theme.menu_background())
        winplat.fill_rect(dc, drag_current_x + 10, drag_current_y + 10, text_width + 4, text_height, theme.border_primary())

        // Draw preview text
        winplat.draw_text(dc, drag_current_x + 12, drag_current_y + 12, preview_text, theme.menu_text())
    end if
end function

pub function draw_drop_zone_indicators(dc: i32, widgets: array<widget_data>)
    let mut i = 0
    while i < drop_zone_count do
        if drop_zones[i].active then
            // Find widget for this drop zone
            let mut j = 0
            while j < widgets.count do
                if widgets[j].id == drop_zones[i].widget_id then
                    // Draw highlight around widget if it's a valid drop target
                    if is_drag_active and (drop_zones[i].accepts_types and drag_type != 0) then
                        winplat.fill_rect(dc, widgets[j].x - 2, widgets[j].y - 2, widgets[j].width + 4, widgets[j].height + 4, theme.border_primary())

                        // Draw drop indicator in center
                        let center_x = widgets[j].x + widgets[j].width / 2
                        let center_y = widgets[j].y + widgets[j].height / 2
                        winplat.fill_rect(dc, center_x - 2, center_y - 2, 4, 4, theme.menu_selected())
                    end if
                    break
                end if
                j = j + 1
            end while
        end if
        i = i + 1
    end while
end function

pub function process_drag_mouse_motion(event: winplat::event) -> bool
    if not drag_active then return false end if

    if event.event_type == winplat.EVENT_MOUSE_MOTION() then
        update_drag(event.mouse_x, event.mouse_y)
        return true
    elseif event.event_type == winplat.EVENT_MOUSE_CLICK() then
        // Check if it's the same mouse button that started the drag
        end_drag(event.mouse_x, event.mouse_y)
        return true
    end if

    return false
end function

// Text editor drag selection support
pub function handle_text_editor_selection_drag(event: winplat::event, mut w: widget_data) -> bool
    let x = event.mouse_x
    let y = event.mouse_y

    if event.event_type == winplat.EVENT_MOUSE_CLICK() and not drag_active then
        // Check if within text editor bounds
        if x >= w.x and x < w.x + w.width and y >= w.y and y < w.y + w.height then
            // Start selection drag if no current selection
            if not w.selection_active then
                let mut local_x = x - w.x
                let local_y = y - w.y

                let gutter_width = 48
                if string_utils.contains(w.options, "line_numbers") then
                    local_x = local_x - gutter_width
                end if

                let line_height = 16
                let click_line = (local_y - 2) / line_height
                let click_col = local_x / 8

                // Convert to cursor position
                w.cursor_pos = editor_get_position_from_line_col(w.text, click_line, click_col)
                w.selection_start = w.cursor_pos
                w.selection_end = w.cursor_pos
                w.selection_active = true

                // Could potentially start text drag if clicking on existing selection
                if w.selection_active and string_utils.length(string_utils.substring(w.text, w.selection_start, w.selection_end)) > 0 then
                    let mut start = w.selection_start
                    let mut end_pos = w.selection_end
                    if start > end_pos then
                        let temp = start
                        start = end_pos
                        end_pos = temp
                    end if

                    let selected_text = string_utils.substring(w.text, start, end_pos)
                    start_drag(w.id, DRAG_TYPE_TEXT(), selected_text, x, y)
                    return true
                end if
            end if
        end if
    end if

    return false
end function
# Create text editor with breakpoint support
pub function create_debug_text_editor(x: i32, y: i32, width: i32, height: i32, content: text, file_path: text, id: i32) -> widget_data
    let w = create_text_editor(x, y, width, height, content, id)
    w.options = "line_numbers,syntax,breakpoints:" + file_path
    return w
end function

# Check if editor has breakpoints enabled
pub function editor_has_breakpoints(editor: widget_data) -> bool
    let opts = editor.options
    return string_utils.contains(opts, "breakpoints:")
end function

# Get file path from debug editor options
pub function get_editor_file_path(editor: widget_data) -> text
    let opts = editor.options
    let colon_pos = string_utils.index_of(opts, ":")
    if colon_pos > 0 then
        return string_utils.substring(opts, colon_pos + 1, string_utils.length(opts))
    end if
    return ""
end function

# Toggle breakpoint in current editor line
pub function toggle_editor_breakpoint(editor: widget_data, line_number: i32) -> bool
    let file_path = get_editor_file_path(editor)
    if string_utils.length(file_path) == 0 then
        return false
    end if

    if has_breakpoint_at(file_path, line_number) then
        return remove_debug_breakpoint(file_path, line_number)
    else
        let bp_id = add_debug_breakpoint(file_path, line_number, "")
        return bp_id > 0
    end if
end function

// =========================================================================
// Debug infrastructure stubs
// =========================================================================

pub function has_breakpoint_at(file_path: text, line_number: i32) -> bool
    let mut i = 0
    while i < breakpoint_count do
        if breakpoints[i].file == file_path and breakpoints[i].line == line_number then
            return true
        end if
        i = i + 1
    end while
    return false
end function

pub function add_debug_breakpoint(file_path: text, line_number: i32, condition: text) -> i32
    let bp = breakpoint_info {
        id: breakpoint_count + 1,
        file: file_path,
        line: line_number,
        enabled: true,
        condition: condition,
        hit_count: 0
    }
    breakpoints = breakpoints + [bp]
    breakpoint_count = breakpoint_count + 1
    return bp.id
end function

pub function remove_debug_breakpoint(file_path: text, line_number: i32) -> bool
    let mut new_bps: array<breakpoint_info> = []
    let mut found = false
    let mut i = 0
    while i < breakpoint_count do
        if breakpoints[i].file == file_path and breakpoints[i].line == line_number then
            found = true
        else
            new_bps = new_bps + [breakpoints[i]]
        end if
        i = i + 1
    end while
    if found then
        breakpoints = new_bps
        breakpoint_count = breakpoint_count - 1
    end if
    return found
end function

pub function create_debug_start_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Debug", ID_DEBUG_START)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_stop_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Stop", ID_DEBUG_STOP)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_step_into_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Into", ID_DEBUG_STEP_INTO)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_step_over_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Over", ID_DEBUG_STEP_OVER)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_step_out_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Out", ID_DEBUG_STEP_OUT)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_continue_button(x: i32, y: i32, enabled: bool) -> widget_data
    let mut btn = create_button(x, y, 60, 24, "Cont", ID_DEBUG_CONTINUE)
    btn.enabled = enabled
    return btn
end function

pub function create_debug_status_label(x: i32, y: i32, width: i32, height: i32) -> widget_data
    return create_label(x, y, width, height, "", 0)
end function

pub function create_call_stack_panel(x: i32, y: i32, width: i32, height: i32) -> widget_data
    let mut panel = create_label(x, y, width, height, "Call Stack", ID_DEBUG_CALL_STACK)
    panel.visible = debug_active
    return panel
end function

pub function create_variables_panel(x: i32, y: i32, width: i32, height: i32) -> widget_data
    let mut panel = create_label(x, y, width, height, "Variables", ID_DEBUG_VARIABLES)
    panel.visible = debug_active
    return panel
end function

pub function create_breakpoints_panel(x: i32, y: i32, width: i32, height: i32) -> widget_data
    let mut panel = create_label(x, y, width, height, "Breakpoints", ID_DEBUG_BREAKPOINTS)
    panel.visible = debug_active
    return panel
end function

pub function debug_ipc_client_connect(socket_path: text) -> bool
    return winplat::debug_ipc_client_connect(socket_path)
end function

pub function debug_get_socket_path() -> text
    return winplat::debug_get_socket_path()
end function

pub function debug_start_debugging(file_path: text) -> bool
    debug_active = true
    debug_state = "running"
    current_debug_file = file_path
    current_debug_line = 1
    return true
end function

pub function debug_stop_debugging()
    debug_active = false
    debug_state = "stopped"
    current_debug_file = ""
    current_debug_line = 0
end function

pub function update_debug_state(state: text, file: text, line: i32)
    debug_state = state
    current_debug_file = file
    current_debug_line = line
    if state == "stopped" or state == "detached" then
        debug_active = false
    else
        debug_active = true
    end if
end function

pub function debug_step_into() -> bool
    if debug_active then
        winplat::debug_step_into()
        return true
    end if
    return false
end function

pub function debug_step_over() -> bool
    if debug_active then
        winplat::debug_step_over()
        return true
    end if
    return false
end function

pub function debug_step_out() -> bool
    if debug_active then
        winplat::debug_step_out()
        return true
    end if
    return false
end function

pub function debug_continue() -> bool
    if debug_active then
        winplat::debug_continue()
        return true
    end if
    return false
end function

pub function refresh_debug_data()
    // Stub - would refresh call stack and variables from debugger
    let stub = 0
end function

pub function get_current_debug_file() -> text
    return current_debug_file
end function

pub function get_current_debug_line() -> i32
    return current_debug_line
end function

pub function is_debug_active() -> bool
    return debug_active
end function
