module context_menu_test
# Test application for context menu system
# Demonstrates right-click context menus with various item types

import widget
import theme
import winplat
import string_utils
import context_menu

let mut window_handle: i32 = -1
let mut running: bool = true
let mut status_text: text = "Context Menu Test - Right-click in different areas"

// Test widgets
let mut file_area: widget::widget_data
let mut editor_area: widget::widget_data
let mut tree_area: widget::widget_data
let mut label_widget: widget::widget_data

pub function main()
    window_handle = winplat.create_window("Context Menu Test", 900, 700)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    // Initialize context menu system
    context_menu.initialize_context_menu()

    create_test_widgets()

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function create_test_widgets()
    // File area (simulates file manager)
    file_area = widget.create_panel(20, 80, 280, 300, "File Explorer", 1001)

    // Editor area (simulates text editor)
    let editor_content = "# Text Editor Area\n# Right-click for editor context menu\n\nfunction hello_world()\n    print(\"Hello from context menu!\")\n    return true\nend function\n\n# Features demonstrated:\n# - Undo/Redo\n# - Cut/Copy/Paste\n# - Find/Replace\n# - Word wrap options\n# - Syntax highlighting toggle"
    editor_area = widget.create_text_editor(320, 80, 280, 300, editor_content, 1002)

    // Tree view area (simulates project tree)
    tree_area = widget.create_treeview(620, 80, 260, 300, "0:Project|1:src|2:main.ny|2:utils.ny|1:docs|2:README.md|1:tests|2:test_suite.ny|0:lib|1:external|2:nilaix", 1003)

    // Label for instructions
    label_widget = widget.create_label(20, 400, 860, 60, "Right-click in different areas to see context menus. Each area has context-appropriate menu items.", 1004)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_mouse_press(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_mouse_press(mouse_x: i32, mouse_y: i32)
    // Check if context menu is clicked first
    if context_menu.is_context_menu_visible() then
        if context_menu.hit_test_context_menu(mouse_x, mouse_y) then
            let item_index = context_menu.get_item_at_position(mouse_x, mouse_y)
            if item_index >= 0 then
                context_menu.current_menu.selected_index = item_index
                let action_id = context_menu.execute_selected_item()
                handle_context_menu_action(action_id)
            else
                context_menu.hide_context_menu()
            end if
        else
            context_menu.hide_context_menu()
        end if
        return
    end if

    // Handle right-click for context menus
    if winplat.get_mouse_button() == 2 then  // Right mouse button
        if widget.is_point_in_widget(mouse_x, mouse_y, file_area) then
            show_file_context_menu(mouse_x, mouse_y)
        elseif widget.is_point_in_widget(mouse_x, mouse_y, editor_area) then
            show_editor_context_menu(mouse_x, mouse_y)
        elseif widget.is_point_in_widget(mouse_x, mouse_y, tree_area) then
            show_tree_context_menu(mouse_x, mouse_y)
        end if
    else
        // Handle left-clicks on widgets
        widget.check_widget_click(mouse_x, mouse_y, file_area)
        widget.check_widget_click(mouse_x, mouse_y, editor_area)
        widget.check_widget_click(mouse_x, mouse_y, tree_area)

        // Hide context menu on left-click
        context_menu.hide_context_menu()
    end if
end function

pub function show_file_context_menu(mouse_x: i32, mouse_y: i32)
    let items = context_menu.create_file_context_menu()
    context_menu.show_context_menu(mouse_x, mouse_y, items)
    status_text = "File context menu shown - Navigation and file operations"
end function

pub function show_editor_context_menu(mouse_x: i32, mouse_y: i32)
    let items = context_menu.create_editor_context_menu()
    context_menu.show_context_menu(mouse_x, mouse_y, items)
    status_text = "Editor context menu shown - Text editing operations"
end function

pub function show_tree_context_menu(mouse_x: i32, mouse_y: i32)
    let items = context_menu.create_tree_context_menu()
    context_menu.show_context_menu(mouse_x, mouse_y, items)
    status_text = "Tree context menu shown - Project tree operations"
end function

pub function handle_context_menu_action(action_id: i32)
    if action_id >= 1001 and action_id <= 1011 then
        // File operations
        handle_file_action(action_id)
    elseif action_id >= 2001 and action_id <= 2011 then
        // Editor operations
        handle_editor_action(action_id)
    elseif action_id >= 3001 and action_id <= 3012 then
        // Tree operations
        handle_tree_action(action_id)
    end if
end function

pub function handle_file_action(action_id: i32)
    if action_id == 1001 then
        status_text = "File: New file created"
    elseif action_id == 1002 then
        status_text = "File: Open file dialog"
    elseif action_id == 1003 then
        status_text = "File: File saved"
    elseif action_id == 1004 then
        status_text = "File: Save As dialog"
    elseif action_id == 1005 then
        status_text = "File: Cut to clipboard"
    elseif action_id == 1006 then
        status_text = "File: Copy to clipboard"
    elseif action_id == 1007 then
        status_text = "File: Paste from clipboard"
    elseif action_id == 1008 then
        status_text = "File: Delete selected"
    elseif action_id == 1009 then
        status_text = "File: Rename selected"
    elseif action_id == 1010 then
        status_text = "File: Toggle hidden files"
    elseif action_id == 1011 then
        status_text = "File: Show properties dialog"
    end if
end function

pub function handle_editor_action(action_id: i32)
    if action_id == 2001 then
        status_text = "Editor: Undo performed"
    elseif action_id == 2002 then
        status_text = "Editor: Redo performed"
    elseif action_id == 2003 then
        status_text = "Editor: Cut to clipboard"
    elseif action_id == 2004 then
        status_text = "Editor: Copy to clipboard"
    elseif action_id == 2005 then
        status_text = "Editor: Paste from clipboard"
    elseif action_id == 2006 then
        status_text = "Editor: Select all text"
    elseif action_id == 2007 then
        status_text = "Editor: Find dialog opened"
    elseif action_id == 2008 then
        status_text = "Editor: Replace dialog opened"
    elseif action_id == 2009 then
        status_text = "Editor: Toggle word wrap"
    elseif action_id == 2010 then
        status_text = "Editor: Toggle line numbers"
    elseif action_id == 2011 then
        status_text = "Editor: Toggle syntax highlighting"
    end if
end function

pub function handle_tree_action(action_id: i32)
    if action_id == 3001 then
        status_text = "Tree: Open selected item"
    elseif action_id == 3002 then
        status_text = "Tree: Open in new tab"
    elseif action_id == 3003 then
        status_text = "Tree: Cut to clipboard"
    elseif action_id == 3004 then
        status_text = "Tree: Copy to clipboard"
    elseif action_id == 3005 then
        status_text = "Tree: Paste from clipboard"
    elseif action_id == 3006 then
        status_text = "Tree: Rename selected item"
    elseif action_id == 3007 then
        status_text = "Tree: Delete selected item"
    elseif action_id == 3008 then
        status_text = "Tree: Show properties"
    elseif action_id == 3009 then
        status_text = "Tree: Refresh tree"
    elseif action_id == 3010 then
        status_text = "Tree: New folder created"
    elseif action_id == 3011 then
        status_text = "Tree: New file created"
    elseif action_id == 3012 then
        status_text = "Tree: New project created"
    end if
end function

pub function handle_key_press(event: winplat::event)
    // Handle context menu navigation when visible
    if context_menu.is_context_menu_visible() then
        if event.key_code == 0x10028 then  // Down arrow
            context_menu.select_next_item()
        elseif event.key_code == 0x10026 then  // Up arrow
            context_menu.select_previous_item()
        elseif event.key_code == 0x1000D then  // Enter
            let action_id = context_menu.execute_selected_item()
            handle_context_menu_action(action_id)
        elseif event.key_code == 0x1001B then  // Escape
            context_menu.hide_context_menu()
            status_text = "Context menu cancelled"
        end if
    else
        // Route to focused widget
        let focused_id = widget.get_focused_widget_id()
        if focused_id == editor_area.id then
            widget.process_key_event(event, editor_area)
        end if

        // Handle theme toggle
        if event.key_code == 84 or event.key_code == 116 then  // T or t
            if theme.get_theme() == theme.THEME_DARK() then
                theme.set_theme(theme.THEME_LIGHT())
                status_text = "Switched to Light theme"
            else
                theme.set_theme(theme.THEME_DARK())
                status_text = "Switched to Dark theme"
            end if
        end if
    end if
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 900, 700, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 20, 10, "Context Menu Test", theme.text_primary())
    winplat.draw_text(window_handle, 20, 30, "Right-click in different areas to see context menus | Arrow keys to navigate, Enter to select, Esc to cancel", theme.text_secondary())

    // Draw area labels
    winplat.draw_text(window_handle, 20, 60, "File Explorer:", theme.text_primary())
    winplat.draw_text(window_handle, 320, 60, "Text Editor:", theme.text_primary())
    winplat.draw_text(window_handle, 620, 60, "Project Tree:", theme.text_primary())

    // Draw test widgets
    widget.draw_widget(window_handle, file_area)
    widget.draw_widget(window_handle, editor_area)
    widget.draw_widget(window_handle, tree_area)
    widget.draw_widget(window_handle, label_widget)

    // Draw context menu if visible
    context_menu.draw_context_menu(window_handle)

    // Draw instructions panel
    draw_instructions_panel()

    // Draw status bar
    winplat.fill_rect(window_handle, 0, 670, 900, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, 670, 900, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, 675, status_text, theme.text_primary())

    winplat.present(window_handle)
end function

pub function draw_instructions_panel()
    let panel_x = 20
    let panel_y = 480
    let panel_w = 860
    let panel_h = 180

    // Draw panel background
    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, panel_h, theme.panel_bg())
    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y + panel_h - 1, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y, 1, panel_h, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x + panel_w - 1, panel_y, 1, panel_h, theme.border_primary())

    // Panel header
    winplat.fill_rect(window_handle, panel_x + 1, panel_y + 1, panel_w - 2, 22, theme.panel_header_bg())
    winplat.draw_text(window_handle, panel_x + 6, panel_y + 4, "Context Menu Features", theme.text_primary())

    // Draw features
    let text_y = panel_y + 30
    let line_height = 18
    let col1_x = panel_x + 10
    let col2_x = panel_x + 220
    let col3_x = panel_x + 430
    let col4_x = panel_x + 640

    winplat.draw_text(window_handle, col1_x, text_y, "File Menu:", theme.text_primary())
    winplat.draw_text(window_handle, col1_x, text_y + line_height, "• New/Open/Save", theme.text_secondary())
    winplat.draw_text(window_handle, col1_x, text_y + line_height * 2, "• Cut/Copy/Paste", theme.text_secondary())
    winplat.draw_text(window_handle, col1_x, text_y + line_height * 3, "• Delete/Rename", theme.text_secondary())
    winplat.draw_text(window_handle, col1_x, text_y + line_height * 4, "• Properties", theme.text_secondary())

    winplat.draw_text(window_handle, col2_x, text_y, "Editor Menu:", theme.text_primary())
    winplat.draw_text(window_handle, col2_x, text_y + line_height, "• Undo/Redo", theme.text_secondary())
    winplat.draw_text(window_handle, col2_x, text_y + line_height * 2, "• Select All", theme.text_secondary())
    winplat.draw_text(window_handle, col2_x, text_y + line_height * 3, "• Find/Replace", theme.text_secondary())
    winplat.draw_text(window_handle, col2_x, text_y + line_height * 4, "• Options toggles", theme.text_secondary())

    winplat.draw_text(window_handle, col3_x, text_y, "Tree Menu:", theme.text_primary())
    winplat.draw_text(window_handle, col3_x, text_y + line_height, "• Open/View", theme.text_secondary())
    winplat.draw_text(window_handle, col3_x, text_y + line_height * 2, "• New items", theme.text_secondary())
    winplat.draw_text(window_handle, col3_x, text_y + line_height * 3, "• Organize", theme.text_secondary())
    winplat.draw_text(window_handle, col3_x, text_y + line_height * 4, "• Refresh", theme.text_secondary())

    winplat.draw_text(window_handle, col4_x, text_y, "Navigation:", theme.text_primary())
    winplat.draw_text(window_handle, col4_x, text_y + line_height, "• ↑↓ Navigate", theme.text_secondary())
    winplat.draw_text(window_handle, col4_x, text_y + line_height * 2, "• Enter Select", theme.text_secondary())
    winplat.draw_text(window_handle, col4_x, text_y + line_height * 3, "• Esc Cancel", theme.text_secondary())
    winplat.draw_text(window_handle, col4_x, text_y + line_height * 4, "• Click outside", theme.text_secondary())
end function