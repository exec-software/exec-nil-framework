module advanced_widgets_test
# Comprehensive test of all advanced NILAIX widgets
# Tests Text Editor, Tab Bar, Tree View, and integration features

import widget
import theme
import winplat
import string_utils

// Test state
let mut window_handle: i32 = -1
let mut running: bool = true
let mut test_text_editor: widget::widget_data
let mut test_tabbar: widget::widget_data
let mut test_treeview: widget::widget_data
let mut test_slider: widget::widget_data
let mut test_listbox: widget::widget_data
let mut test_progress: widget::widget_data
let mut status_text: text = "Advanced Widget Test - Ready"

// Sample content for text editor
let mut editor_content = "function hello_world()\n    print(\"Hello, NILAIX!\")\n    return true\nend function\n\n# Multi-line text editor test\n# Features:\n# - Line numbers\n# - Syntax highlighting (future)\n# - Cursor navigation\n# - Scroll support\n\nlet result = hello_world()\nif result then\n    print(\"Success!\")\nend if"

// Sample tree data
let mut tree_data = "0:Project Root|1:src|2:main.ny|2:utils.ny|1:docs|2:README.md|1:tests|2:test_widgets.ny|0:External Libraries|1:nilaix|2:widget.ny|2:theme.ny"

// Sample list data
let mut list_items = "Button,Text Field,Label,Dropdown,Menu,Checkbox,RadioButton,StatusBar,Slider,ProgressBar,ListBox,Toolbar,Panel,Text Editor,Tab Bar,Tree View"

pub function main()
    // Initialize window system
    window_handle = winplat.create_window("Advanced NILAIX Widgets Test", 1024, 768)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    // Set theme to dark mode
    theme.set_theme(theme.THEME_DARK())

    // Create test widgets
    create_test_widgets()

    // Main event loop
    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    // Cleanup
    winplat.destroy_window(window_handle)
end function

pub function create_test_widgets()
    // Text Editor (top-left, large)
    test_text_editor = widget.create_text_editor(20, 20, 480, 300, editor_content, 100)

    // Tab Bar (top-right)
    test_tabbar = widget.create_tabbar_closeable(520, 20, 480, 30, "Main,Editor,Preview,Settings,Debug", 101)

    // Tree View (right side, below tabs)
    test_treeview = widget.create_treeview(520, 60, 240, 260, tree_data, 102)

    // Slider (bottom-left area)
    test_slider = widget.create_slider(20, 340, 200, 25, 75, 103)

    // List Box (bottom-center)
    test_listbox = widget.create_listbox(240, 340, 260, 150, list_items, 104)

    // Progress Bar (bottom-right)
    test_progress = widget.create_progressbar(520, 340, 200, 25, 60, 105)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_mouse_click(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_mouse_click(mouse_x: i32, mouse_y: i32)
    // Check each widget for clicks
    if widget.check_widget_click(mouse_x, mouse_y, test_text_editor) then
        status_text = "Text Editor clicked"
    elseif widget.check_widget_click(mouse_x, mouse_y, test_tabbar) then
        status_text = "Tab Bar clicked"
    elseif widget.check_widget_click(mouse_x, mouse_y, test_treeview) then
        status_text = "Tree View clicked"
    elseif widget.check_widget_click(mouse_x, mouse_y, test_slider) then
        status_text = "Slider clicked"
    elseif widget.check_widget_click(mouse_x, mouse_y, test_listbox) then
        status_text = "List Box clicked"
    end if
end function

pub function handle_key_press(event: winplat::event)
    // Route key events to focused widget
    let focused_id = widget.get_focused_widget_id()

    if focused_id == test_text_editor.id then
        widget.process_key_event(event, test_text_editor)
        // Update editor content
        editor_content = test_text_editor.text
    elseif focused_id == test_tabbar.id then
        widget.process_key_event(event, test_tabbar)
    elseif focused_id == test_treeview.id then
        widget.process_key_event(event, test_treeview)
    elseif focused_id == test_slider.id then
        widget.process_key_event(event, test_slider)
        // Update progress bar to match slider
        test_progress.selected_index = test_slider.selected_index
    elseif focused_id == test_listbox.id then
        widget.process_key_event(event, test_listbox)
    end if
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 1024, 768, theme.bg_primary())

    // Draw all widgets
    widget.draw_widget(window_handle, test_text_editor)
    widget.draw_widget(window_handle, test_tabbar)
    widget.draw_widget(window_handle, test_treeview)
    widget.draw_widget(window_handle, test_slider)
    widget.draw_widget(window_handle, test_listbox)
    widget.draw_widget(window_handle, test_progress)

    // Draw status bar
    draw_status_bar()

    // Draw widget info panel
    draw_info_panel()

    // Process signals
    process_signals()

    // Present frame
    winplat.present(window_handle)
end function

pub function draw_status_bar()
    let status_y = 768 - 30
    winplat.fill_rect(window_handle, 0, status_y, 1024, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, status_y, 1024, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, status_y + 8, status_text, theme.text_primary())

    // Draw theme info
    let theme_text = "Theme: "
    if theme.get_theme() == theme.THEME_DARK() then
        theme_text = theme_text + "Dark"
    else
        theme_text = theme_text + "Light"
    end if
    winplat.draw_text(window_handle, 900, status_y + 8, theme_text, theme.text_secondary())
end function

pub function draw_info_panel()
    // Draw info panel (right side, below tree view)
    let panel_x = 780
    let panel_y = 60
    let panel_w = 220
    let panel_h = 260

    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, panel_h, theme.panel_bg())
    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y + panel_h - 1, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y, 1, panel_h, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x + panel_w - 1, panel_y, 1, panel_h, theme.border_primary())

    // Panel header
    winplat.fill_rect(window_handle, panel_x + 1, panel_y + 1, panel_w - 2, 22, theme.panel_header_bg())
    winplat.draw_text(window_handle, panel_x + 6, panel_y + 4, "Widget Info", theme.text_primary())

    // Widget information
    let info_y = panel_y + 30
    let line_height = 18

    // Text Editor info
    let editor_lines = widget.editor_get_line_count(test_text_editor.text)
    let editor_line = widget.editor_cursor_to_line(test_text_editor.text, test_text_editor.cursor_pos)
    let editor_col = widget.editor_cursor_to_col(test_text_editor.text, test_text_editor.cursor_pos)
    winplat.draw_text(window_handle, panel_x + 6, info_y, "Text Editor:", theme.text_primary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height, "  Lines: " + string_utils.int_to_text(editor_lines), theme.text_secondary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 2, "  Cursor: " + string_utils.int_to_text(editor_line + 1) + ":" + string_utils.int_to_text(editor_col), theme.text_secondary())

    // Tab Bar info
    let tab_parts = string_utils.split(test_tabbar.options, ",")
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 4, "Tab Bar:", theme.text_primary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 5, "  Tabs: " + string_utils.int_to_text(tab_parts.count), theme.text_secondary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 6, "  Active: " + string_utils.trim(tab_parts.items[test_tabbar.selected_index]), theme.text_secondary())

    // Tree View info
    let tree_nodes = widget.treeview_parse_nodes(test_treeview.options)
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 8, "Tree View:", theme.text_primary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 9, "  Nodes: " + string_utils.int_to_text(tree_nodes.count), theme.text_secondary())
    if test_treeview.selected_index >= 0 and test_treeview.selected_index < tree_nodes.count then
        let selected_node = tree_nodes.items[test_treeview.selected_index]
        winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 10, "  Selected: " + selected_node.label, theme.text_secondary())
    end if

    // Other widgets info
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 12, "Slider: " + string_utils.int_to_text(test_slider.selected_index) + "%", theme.text_secondary())
    winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 13, "Progress: " + string_utils.int_to_text(test_progress.selected_index) + "%", theme.text_secondary())

    let list_parts = string_utils.split(test_listbox.options, ",")
    if test_listbox.selected_index >= 0 and test_listbox.selected_index < list_parts.count then
        let selected_item = string_utils.trim(list_parts.items[test_listbox.selected_index])
        winplat.draw_text(window_handle, panel_x + 6, info_y + line_height * 14, "List: " + selected_item, theme.text_secondary())
    end if
end function

pub function process_signals()
    while widget.has_pending_signals() do
        let sig = widget.pop_signal()

        if sig.signal_type == widget.SIGNAL_TEXT_CHANGED() then
            status_text = "Text changed: " + string_utils.substring(sig.data, 0, 20) + "..."
        elseif sig.signal_type == widget.SIGNAL_CLICKED() then
            status_text = "Clicked: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_SELECTED_CHANGED() then
            status_text = "Selected: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_TAB_CHANGED() then
            status_text = "Tab changed: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_TAB_CLOSED() then
            status_text = "Tab closed: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_NODE_TOGGLED() then
            status_text = "Node toggled: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_CHECKED_CHANGED() then
            status_text = "Checked: " + sig.data
        elseif sig.signal_type == widget.SIGNAL_KEY_DOWN() then
            status_text = "Key: " + sig.data
        end if
    end while
end function