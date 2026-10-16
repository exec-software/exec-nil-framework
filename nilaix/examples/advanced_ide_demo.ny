module advanced_ide_demo
# Comprehensive IDE showcasing all advanced NILAIX features
# Syntax highlighting, code completion, split panes, context menus

import widget
import theme
import winplat
import string_utils
import syntax_highlighter
import code_completion
import split_pane
import context_menu

let mut window_handle: i32 = -1
let mut running: bool = true
let mut status_text: text = "Advanced NILAIX IDE - All features integrated"

// IDE components
let mut menu_bar: widget::widget_data
let mut toolbar: widget::widget_data
let mut tabbar: widget::widget_data
let mut main_split: split_pane::split_pane_data
let mut file_tree: widget::widget_data
let mut output_panel: widget::widget_data
let mut status_bar: widget::widget_data

// Text editors for split panes
let mut editor1: widget::widget_data
let mut editor2: widget::widget_data

// Current files
let mut current_files: array<text> = []
let mut file_contents: array<text> = []

pub function main()
    window_handle = winplat.create_window("Advanced NILAIX IDE", 1400, 900)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    // Initialize all systems
    code_completion.initialize_completion()
    context_menu.initialize_context_menu()

    setup_ide_components()
    load_sample_files()

    while running do
        process_events()
        render_ide()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function setup_ide_components()
    // Menu bar
    menu_bar = widget.create_menu(0, 0, 1400, 25, "File,Edit,View,Tools,Help", 1000)

    // Toolbar
    toolbar = widget.create_toolbar(0, 25, 1400, 30, "New,Open,Save,Build,Run,Debug,Split,Theme", 1001)

    // Tab bar
    tabbar = widget.create_tabbar_closeable(0, 55, 1400, 25, "main.ny,utils.ny,config.ny,README.md", 1002)

    // Create text editors with syntax highlighting
    let content1 = "# Advanced NILAIX IDE - Editor 1\n# Syntax highlighting, code completion, context menus\n\nmodule advanced_editor\n\nimport widget\nimport theme\nimport syntax_highlighter\n\nfunction create_enhanced_editor()\n    # Editor with all features enabled\n    let editor = widget.create_text_editor(\n        x: 100, y: 100,\n        width: 600, height: 400,\n        content: \"\",\n        id: 1001\n    )\n    \n    # Enable syntax highlighting\n    editor.options = \"line_numbers,syntax\"\n    \n    return editor\nend function\n\n# Try typing to see:\n# - Syntax highlighting for keywords\n# - Code completion (Ctrl+Space)\n# - Context menus (right-click)\n# - Split pane editing"

    let content2 = "# Advanced NILAIX IDE - Editor 2\n# Split pane demonstration\n\nmodule split_pane_demo\n\nimport split_pane\n\nfunction create_quad_view()\n    # Create 4-pane editor layout\n    let editor1 = create_editor(\"Editor 1\")\n    let editor2 = create_editor(\"Editor 2\")\n    let editor3 = create_editor(\"Editor 3\")\n    let editor4 = create_editor(\"Editor 4\")\n    \n    # Horizontal split for top\n    let top_split = split_pane.create_split_pane(\n        0, 0, 800, 400,\n        split_pane.SPLIT_HORIZONTAL(),\n        editor1, editor2, 2001\n    )\n    \n    # Vertical split for bottom\n    let bottom_split = split_pane.create_split_pane(\n        0, 400, 800, 400,\n        split_pane.SPLIT_VERTICAL(),\n        editor3, editor4, 2002\n    )\n    \n    return [top_split, bottom_split]\nend function\n\n# Features in this pane:\n# - Resizable splitters\n# - Independent editing\n# - Focus management"

    editor1 = widget.create_text_editor(0, 0, 700, 400, content1, 2001)
    editor2 = widget.create_text_editor(0, 0, 700, 400, content2, 2002)

    // Create split pane
    main_split = split_pane.create_split_pane(10, 90, 1000, 600, split_pane.SPLIT_HORIZONTAL(), editor1, editor2, 3000)

    // File tree
    let tree_data = "0:project|1:src|2:main.ny|2:utils.ny|2:config.ny|1:docs|2:README.md|1:tests|2:test_suite.ny|1:lib|2:nilaix|2:theme|0:build|1:debug|2:release"
    file_tree = widget.create_treeview(1020, 90, 360, 600, tree_data, 3000)

    // Output panel
    output_panel = widget.create_text_field(1020, 500, 360, 120, "IDE ready. All advanced features integrated successfully.", 3001)
    output_panel.enabled = false

    // Status bar
    status_bar = widget.create_statusbar(0, 870, 1400, 25, "Advanced IDE | Line 1, Col 1 | NY Language | All features: ON", 3002)
end function

pub function load_sample_files()
    current_files = ["main.ny", "utils.ny", "config.ny", "README.md"]

    file_contents = file_contents + ["# Main application file\n\nmodule main\n\nfunction main()\n    print(\"Hello from advanced IDE!\")\n    return true\nend function"]
    file_contents = file_contents + ["# Utility functions\n\nmodule utils\n\nfunction format_text(text: text) -> text\n    return string_utils.trim(text)\nend function\n\nfunction validate_input(input: text) -> bool\n    return string_utils.length(input) > 0\nend function"]
    file_contents = file_contents + ["# Configuration module\n\nmodule config\n\nstruct app_config\n    app_name: text\n    version: text\n    debug: bool\nend struct\n\nlet settings = app_config {\n    app_name: \"Advanced IDE\",\n    version: \"1.0.0\",\n    debug: true\n}"]
    file_contents = file_contents + ["# Project Documentation\n\n# Advanced NILAIX IDE\n\nA comprehensive IDE showcasing all NILAIX features:\n\n## Features\n- Syntax highlighting\n- Code completion\n- Split pane editing\n- Context menus\n- Theme switching\n- Integrated tools\n\n## Usage\n1. Right-click for context menus\n2. Type for code completion\n3. Drag splitters to resize\n4. Use toolbar for actions"]
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_mouse_press(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_MOUSE_RELEASE() then
            handle_mouse_release()
        elseif event.event_type == winplat::EVENT_MOUSE_MOTION() then
            handle_mouse_motion(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_mouse_press(mouse_x: i32, mouse_y: i32)
    // Handle context menu first
    if context_menu.is_context_menu_visible() then
        if context_menu.hit_test_context_menu(mouse_x, mouse_y) then
            let item_index = context_menu.get_item_at_position(mouse_x, mouse_y)
            if item_index >= 0 then
                context_menu.current_menu.selected_index = item_index
                let action_id = context_menu.execute_selected_item()
                handle_ide_action(action_id)
            end if
        else
            context_menu.hide_context_menu()
        end if
        return
    end if

    // Handle right-click for context menus
    if winplat.get_mouse_button() == 2 then  // Right click
        if widget.is_point_in_widget(mouse_x, mouse_y, editor1) then
            show_editor_context_menu(mouse_x, mouse_y)
        elseif widget.is_point_in_widget(mouse_x, mouse_y, editor2) then
            show_editor_context_menu(mouse_x, mouse_y)
        elseif widget.is_point_in_widget(mouse_x, mouse_y, file_tree) then
            show_tree_context_menu(mouse_x, mouse_y)
        end if
        return
    end if

    // Handle toolbar and menu clicks
    if widget.check_widget_click(mouse_x, mouse_y, menu_bar) then
        handle_menu_action()
    elseif widget.check_widget_click(mouse_x, mouse_y, toolbar) then
        handle_toolbar_action()
    elseif widget.check_widget_click(mouse_x, mouse_y, tabbar) then
        handle_tab_action()
    elseif widget.check_widget_click(mouse_x, mouse_y, file_tree) then
        status_text = "File tree item selected"
    else
        // Handle split pane and widgets
        if not split_pane.handle_split_pane_mouse_press(main_split, mouse_x, mouse_y) then
            // Not a splitter, try individual editors
            widget.check_widget_click(mouse_x, mouse_y, editor1)
            widget.check_widget_click(mouse_x, mouse_y, editor2)
        end if
    end if
end function

pub function handle_mouse_release()
    split_pane.handle_split_pane_mouse_release(main_split)
end function

pub function handle_mouse_motion(mouse_x: i32, mouse_y: i32)
    if not split_pane.handle_split_pane_mouse_motion(main_split, mouse_x, mouse_y) then
        // Update status with position when not dragging
        status_text = "Advanced IDE | Mouse: " + string_utils.int_to_text(mouse_x) + ", " + string_utils.int_to_text(mouse_y)
    end if
end function

pub function handle_key_press(event: winplat::event)
    // Handle context menu navigation
    if context_menu.is_context_menu_visible() then
        if event.key_code == 0x10028 then  // Down
            context_menu.select_next_item()
        elseif event.key_code == 0x10026 then  // Up
            context_menu.select_previous_item()
        elseif event.key_code == 0x1000D then  // Enter
            let action_id = context_menu.execute_selected_item()
            handle_ide_action(action_id)
        elseif event.key_code == 0x1001B then  // Escape
            context_menu.hide_context_menu()
        end if
        return
    end if

    // Route to focused editor
    let focused_id = widget.get_focused_widget_id()
    if focused_id == editor1.id then
        widget.process_key_event(event, editor1)
    elseif focused_id == editor2.id then
        widget.process_key_event(event, editor2)
    end if

    // Handle global shortcuts
    if event.key_code == 84 or event.key_code == 116 then  // T
        toggle_theme()
    elseif event.key_code == 49 then  // 1
        split_pane.set_split_ratio(main_split, 0.3)
        status_text = "Split ratio: 30%/70%"
    elseif event.key_code == 50 then  // 2
        split_pane.set_split_ratio(main_split, 0.5)
        status_text = "Split ratio: 50%/50%"
    elseif event.key_code == 51 then  // 3
        split_pane.set_split_ratio(main_split, 0.7)
        status_text = "Split ratio: 70%/30%"
    elseif event.key_code == 83 then  // S
        split_pane.swap_widgets(main_split)
        status_text = "Swapped editor panes"
    end if
end function

pub function show_editor_context_menu(x: i32, y: i32)
    let items = context_menu.create_editor_context_menu()
    context_menu.show_context_menu(x, y, items)
    status_text = "Editor context menu - Text editing operations"
end function

pub function show_tree_context_menu(x: i32, y: i32)
    let items = context_menu.create_tree_context_menu()
    context_menu.show_context_menu(x, y, items)
    status_text = "Tree context menu - Project operations"
end function

pub function handle_menu_action()
    status_text = "Menu action - File/Edit/View/Tools/Help"
end function

pub function handle_toolbar_action()
    status_text = "Toolbar action - New/Open/Save/Build/Run/Debug/Split/Theme"
end function

pub function handle_tab_action()
    status_text = "Tab action - Switch or close file"
end function

pub function handle_ide_action(action_id: i32)
    if action_id >= 2001 and action_id <= 2011 then
        status_text = "Editor action: " + get_action_name(action_id)
    elseif action_id >= 3001 and action_id <= 3012 then
        status_text = "Tree action: " + get_action_name(action_id)
    end if
end function

pub function get_action_name(action_id: i32) -> text
    if action_id == 2001 then return "Undo"
    elseif action_id == 2002 then return "Redo"
    elseif action_id == 2003 then return "Cut"
    elseif action_id == 2004 then return "Copy"
    elseif action_id == 2005 then return "Paste"
    elseif action_id == 2006 then return "Select All"
    elseif action_id == 2007 then return "Find"
    elseif action_id == 2008 then return "Replace"
    elseif action_id == 2009 then return "Toggle Word Wrap"
    elseif action_id == 2010 then return "Toggle Line Numbers"
    elseif action_id == 2011 then return "Toggle Syntax"
    elseif action_id == 3001 then return "Open"
    elseif action_id == 3002 then return "Open in Tab"
    elseif action_id == 3003 then return "Cut"
    elseif action_id == 3004 then return "Copy"
    elseif action_id == 3005 then return "Paste"
    elseif action_id == 3006 then return "Rename"
    elseif action_id == 3007 then return "Delete"
    elseif action_id == 3008 then return "Properties"
    elseif action_id == 3009 then return "Refresh"
    else return "Unknown"
    end if
end function

pub function toggle_theme()
    if theme.get_theme() == theme.THEME_DARK() then
        theme.set_theme(theme.THEME_LIGHT())
        status_text = "Switched to Light theme"
    else
        theme.set_theme(theme.THEME_DARK())
        status_text = "Switched to Dark theme"
    end if
end function

pub function render_ide()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 1400, 900, theme.bg_primary())

    // Draw IDE components
    widget.draw_widget(window_handle, menu_bar)
    widget.draw_widget(window_handle, toolbar)
    widget.draw_widget(window_handle, tabbar)

    // Draw split panes
    split_pane.draw_split_pane(window_handle, main_split)

    // Draw file tree and output
    widget.draw_widget(window_handle, file_tree)
    widget.draw_widget(window_handle, output_panel)

    // Draw status bar
    widget.draw_widget(window_handle, status_bar)

    // Draw context menu if visible
    context_menu.draw_context_menu(window_handle)

    // Draw feature panel
    draw_feature_panel()

    winplat.present(window_handle)
end function

pub function draw_feature_panel()
    let panel_x = 1020
    let panel_y = 630
    let panel_w = 360
    let panel_h = 230

    // Draw panel background
    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, panel_h, theme.panel_bg())
    winplat.fill_rect(window_handle, panel_x, panel_y, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y + panel_h - 1, panel_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x, panel_y, 1, panel_h, theme.border_primary())
    winplat.fill_rect(window_handle, panel_x + panel_w - 1, panel_y, 1, panel_h, theme.border_primary())

    // Panel header
    winplat.fill_rect(window_handle, panel_x + 1, panel_y + 1, panel_w - 2, 22, theme.panel_header_bg())
    winplat.draw_text(window_handle, panel_x + 6, panel_y + 4, "Advanced Features Active", theme.text_primary())

    // Draw feature list
    let text_y = panel_y + 30
    let line_height = 18
    let features = [
        "✓ Syntax Highlighting - NY language aware",
        "✓ Code Completion - Tab to complete",
        "✓ Split Panes - Drag to resize",
        "✓ Context Menus - Right-click",
        "✓ Theme System - T to toggle",
        "✓ Widget Library - 15 types",
        "✓ Signal System - Event handling",
        "✓ Layout System - Auto-arrange",
        "✓ AI Control - Automation ready",
        "✓ Multi-file Editing - Tab interface"
    ]

    let mut i = 0
    while i < features.count do
        let mut color = theme.text_secondary()
        if i < 6 then color = theme.syntax_function() end if
        winplat.draw_text(window_handle, panel_x + 10, text_y + (i * line_height), features.items[i], color)
        i = i + 1
    end while
end function