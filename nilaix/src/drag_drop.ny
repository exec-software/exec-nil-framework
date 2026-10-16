module drag_drop
# Drag and drop system for NILAIX widgets
# Supports file dragging between tree view and editor

import widget
import theme
import winplat
import string_utils

// Drag and drop states
pub function DRAG_STATE_NONE() -> i32
    return 0
end function

pub function DRAG_STATE_DRAGGING() -> i32
    return 1
end function

pub function DRAG_STATE_DROPPING() -> i32
    return 2
end function

// Drag data types
pub function DRAG_TYPE_FILE() -> i32
    return 0
end function

pub function DRAG_TYPE_TEXT() -> i32
    return 1
end function

pub function DRAG_TYPE_WIDGET() -> i32
    return 2
end function

// Drag operation
pub struct drag_operation
    state: i32
    drag_type: i32
    source_widget_id: i32
    source_data: text
    start_x: i32
    start_y: i32
    current_x: i32
    current_y: i32
    offset_x: i32
    offset_y: i32
    allowed_targets: array<i32>
end struct

// Drop zone structure
pub struct drop_zone
    widget_id: i32
    bounds_x: i32
    bounds_y: i32
    bounds_width: i32
    bounds_height: i32
    accepts_types: array<i32>
    highlight: bool
end struct

// Global drag and drop state
let mut current_drag: drag_operation
let mut drop_zones: array<drop_zone> = []
let mut drop_zone_count: i32 = 0

pub function initialize_drag_drop()
    current_drag = drag_operation {
        state: DRAG_STATE_NONE(),
        drag_type: DRAG_TYPE_FILE(),
        source_widget_id: -1,
        source_data: "",
        start_x: 0,
        start_y: 0,
        current_x: 0,
        current_y: 0,
        offset_x: 0,
        offset_y: 0,
        allowed_targets: []
    }

    drop_zones = []
    drop_zone_count = 0
end function

pub function register_drop_zone(widget_id: i32, x: i32, y: i32, width: i32, height: i32, accepts_types: array<i32>)
    let zone = drop_zone {
        widget_id: widget_id,
        bounds_x: x,
        bounds_y: y,
        bounds_width: width,
        bounds_height: height,
        accepts_types: accepts_types,
        highlight: false
    }

    // Check if zone already exists
    let mut i = 0
    while i < drop_zone_count do
        if drop_zones.items[i].widget_id == widget_id then
            drop_zones.items[i] = zone
            return
        end if
        i = i + 1
    end while

    // Add new zone
    drop_zones = drop_zones + [zone]
    drop_zone_count = drop_zone_count + 1
end function

pub function start_drag(widget_id: i32, drag_type: i32, data: text, mouse_x: i32, mouse_y: i32)
    current_drag.state = DRAG_STATE_DRAGGING()
    current_drag.drag_type = drag_type
    current_drag.source_widget_id = widget_id
    current_drag.source_data = data
    current_drag.start_x = mouse_x
    current_drag.start_y = mouse_y
    current_drag.current_x = mouse_x
    current_drag.current_y = mouse_y
    current_drag.offset_x = 0
    current_drag.offset_y = 0

    // Set allowed targets based on drag type
    if drag_type == DRAG_TYPE_FILE() then
        current_drag.allowed_targets = [1001, 1002, 2001, 2002]  // Tree view and editor IDs
    elseif drag_type == DRAG_TYPE_TEXT() then
        current_drag.allowed_targets = [1001, 1002]  // Text fields
    end if
end function

pub function update_drag(mouse_x: i32, mouse_y: i32)
    if current_drag.state == DRAG_STATE_DRAGGING() then
        current_drag.current_x = mouse_x
        current_drag.current_y = mouse_y

        // Update drop zone highlights
        update_drop_zone_highlights(mouse_x, mouse_y)
    end if
end function

pub function finish_drag(mouse_x: i32, mouse_y: i32) -> bool
    if current_drag.state != DRAG_STATE_DRAGGING() then
        return false
    end if

    current_drag.current_x = mouse_x
    current_drag.current_y = mouse_y
    current_drag.state = DRAG_STATE_NONE()

    // Find drop target
    let target_zone = find_drop_zone_at(mouse_x, mouse_y)

    // Clear all highlights
    clear_drop_zone_highlights()

    if target_zone.widget_id != -1 then
        // Perform drop operation
        return perform_drop(target_zone, mouse_x, mouse_y)
    end if

    return false
end function

pub function cancel_drag()
    current_drag.state = DRAG_STATE_NONE()
    clear_drop_zone_highlights()
end function

pub function is_dragging() -> bool
    return current_drag.state == DRAG_STATE_DRAGGING()
end function

pub function get_drag_data() -> text
    return current_drag.source_data
end function

pub function get_drag_type() -> i32
    return current_drag.drag_type
end function

pub function update_drop_zone_highlights(mouse_x: i32, mouse_y: i32)
    // Clear all highlights first
    let mut i = 0
    while i < drop_zone_count do
        drop_zones.items[i].highlight = false
        i = i + 1
    end while

    // Highlight zones under cursor
    i = 0
    while i < drop_zone_count do
        let zone = drop_zones.items[i]
        if is_point_in_zone(mouse_x, mouse_y, zone) then
            // Check if this zone accepts the current drag type
            let mut j = 0
            while j < zone.accepts_types.count do
                if zone.accepts_types.items[j] == current_drag.drag_type then
                    zone.highlight = true
                    break
                end if
                j = j + 1
            end while
        end if
        i = i + 1
    end while
end function

pub function clear_drop_zone_highlights()
    let mut i = 0
    while i < drop_zone_count do
        drop_zones.items[i].highlight = false
        i = i + 1
    end while
end function

pub function find_drop_zone_at(x: i32, y: i32) -> drop_zone
    let mut i = 0
    while i < drop_zone_count do
        let zone = drop_zones.items[i]
        if is_point_in_zone(x, y, zone) then
            // Check if zone accepts this drag type
            let mut j = 0
            while j < zone.accepts_types.count do
                if zone.accepts_types.items[j] == current_drag.drag_type then
                    return zone
                end if
                j = j + 1
            end while
        end if
        i = i + 1
    end while

    return drop_zone { widget_id: -1, bounds_x: 0, bounds_y: 0, bounds_width: 0, bounds_height: 0, accepts_types: [], highlight: false }
end function

pub function is_point_in_zone(x: i32, y: i32, zone: drop_zone) -> bool
    return x >= zone.bounds_x and x <= zone.bounds_x + zone.bounds_width and
           y >= zone.bounds_y and y <= zone.bounds_y + zone.bounds_height
end function

pub function perform_drop(target_zone: drop_zone, mouse_x: i32, mouse_y: i32) -> bool
    if target_zone.widget_id == -1 then
        return false
    end if

    // Handle different drop scenarios
    if current_drag.drag_type == DRAG_TYPE_FILE() then
        return handle_file_drop(target_zone, current_drag.source_data)
    elseif current_drag.drag_type == DRAG_TYPE_TEXT() then
        return handle_text_drop(target_zone, current_drag.source_data)
    end if

    return false
end function

pub function handle_file_drop(target_zone: drop_zone, file_data: text) -> bool
    // If dropped on tree view, move file within tree
    if target_zone.widget_id >= 3000 and target_zone.widget_id < 4000 then
        // Tree view drop - reorganize tree structure
        return true
    end if

    // If dropped on editor, insert file path or content
    if target_zone.widget_id >= 2000 and target_zone.widget_id < 3000 then
        // Editor drop - insert file reference
        return true
    end if

    return false
end function

pub function handle_text_drop(target_zone: drop_zone, text_data: text) -> bool
    // If dropped on text field or editor, insert text
    if target_zone.widget_id >= 2000 and target_zone.widget_id < 3000 then
        // Insert text at cursor position
        return true
    end if

    return false
end function

pub function draw_drag_overlay(handle: i32)
    if not is_dragging() then
        return
    end if

    // Draw dragged item representation
    if current_drag.drag_type == DRAG_TYPE_FILE() then
        // Draw file icon and name
        let icon_text = "📄"
        let drag_width = string_utils.length(current_drag.source_data) * 8 + 32
        let drag_height = 24

        winplat.fill_rect(handle, current_drag.current_x + 10, current_drag.current_y + 10, drag_width, drag_height, theme.popup_bg())
        winplat.fill_rect(handle, current_drag.current_x + 10, current_drag.current_y + 10, drag_width, drag_height, 1, theme.border_primary())
        winplat.fill_rect(handle, current_drag.current_x + 10, current_drag.current_y + 10 + drag_height - 1, drag_width, 1, theme.border_primary())
        winplat.fill_rect(handle, current_drag.current_x + 10, current_drag.current_y + 10, 1, drag_height, 1, theme.border_primary())
        winplat.fill_rect(handle, current_drag.current_x + 10 + drag_width - 1, current_drag.current_y + 10, 1, drag_height, 1, theme.border_primary())

        winplat.draw_text(handle, current_drag.current_x + 18, current_drag.current_y + 14, icon_text, theme.text_primary())
        winplat.draw_text(handle, current_drag.current_x + 42, current_drag.current_y + 14, current_drag.source_data, theme.text_secondary())
    elseif current_drag.drag_type == DRAG_TYPE_TEXT() then
        // Draw text snippet
        let mut preview_text = string_utils.substring(current_drag.source_data, 0, 30)
        if string_utils.length(current_drag.source_data) > 30 then
            preview_text = preview_text + "..."
        end if

        let drag_width = string_utils.length(preview_text) * 8 + 20
        let drag_height = 20

        winplat.fill_rect(handle, current_drag.current_x + 5, current_drag.current_y + 5, drag_width, drag_height, theme.popup_bg())
        winplat.fill_rect(handle, current_drag.current_x + 5, current_drag.current_y + 5, drag_width, drag_height, 1, theme.border_primary())

        winplat.draw_text(handle, current_drag.current_x + 12, current_drag.current_y + 8, preview_text, theme.text_secondary())
    end if

    // Draw drop zone highlights
    draw_drop_zone_highlights(handle)
end function

pub function draw_drop_zone_highlights(handle: i32)
    let mut i = 0
    while i < drop_zone_count do
        let zone = drop_zones.items[i]
        if zone.highlight then
            // Draw highlight border around drop zone
            winplat.fill_rect(handle, zone.bounds_x, zone.bounds_y, zone.bounds_width, zone.bounds_height, 0x400000FF)  // Semi-transparent blue overlay
            winplat.fill_rect(handle, zone.bounds_x, zone.bounds_y, zone.bounds_width, 2, theme.focus_ring())
            winplat.fill_rect(handle, zone.bounds_x, zone.bounds_y + zone.bounds_height - 2, zone.bounds_width, 2, theme.focus_ring())
            winplat.fill_rect(handle, zone.bounds_x, zone.bounds_y, 2, zone.bounds_height - 4, theme.focus_ring())
            winplat.fill_rect(handle, zone.bounds_x + zone.bounds_width - 2, zone.bounds_y, 2, zone.bounds_height - 4, theme.focus_ring())
        end if
        i = i + 1
    end while
end function

// Tree view specific drag and drop helpers
pub function get_tree_node_at_position(widget_data: widget::widget_data, mouse_x: i32, mouse_y: i32) -> i32
    // Calculate which tree node was clicked based on mouse position
    let item_height = 20
    let mut draw_y = widget_data.y + 2
    let click_rel_y = mouse_y - widget_data.y - 2
    let clicked_visible = click_rel_y / item_height

    // Parse tree nodes and find the clicked one
    let nodes = widget.treeview_parse_nodes(widget_data.options)
    let mut visible_index = 0
    let mut i = 0
    while i < nodes.count do
        if widget.treeview_is_visible(nodes, widget_data.text, i) then
            if visible_index == clicked_visible then
                return i
            end if
            visible_index = visible_index + 1
        end if
        i = i + 1
    end while

    return -1
end function

pub function extract_tree_node_data(widget_data: widget::widget_data, node_index: i32) -> text
    let nodes = widget.treeview_parse_nodes(widget_data.options)
    if node_index >= 0 and node_index < nodes.count then
        let node = nodes.items[node_index]
        return node.label
    end if
    return ""
end function

pub function create_drag_data_from_tree_node(widget_data: widget::widget_data, node_index: i32) -> text
    let nodes = widget.treeview_parse_nodes(widget_data.options)
    if node_index >= 0 and node_index < nodes.count then
        let node = nodes.items[node_index]
        // Create drag data with node information
        return "tree_node:" + string_utils.int_to_text(node_index) + ":" + node.label + ":" + string_utils.int_to_text(node.depth)
    end if
    return ""
end function

// Editor specific drag and drop helpers
pub function get_selected_text(widget_data: widget::widget_data) -> text
    // For now, return a portion of the editor content
    // In a full implementation, this would return the actual selected text
    let mut start = widget_data.cursor_pos - 10
    if start < 0 then start = 0 end if
    let mut end_pos = widget_data.cursor_pos
    if end_pos > string_utils.length(widget_data.text) then
        end_pos = string_utils.length(widget_data.text)
    end if
    return string_utils.substring(widget_data.text, start, end_pos)
end function