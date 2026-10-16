module nycromicon_integration
# NYCromicon IDE with integrated advanced widgets
# Demonstrates complete IDE interface with all widget types

import widget
import theme
import winplat
import string_utils

// Window and state
let mut window_handle: i32 = -1
let mut running: bool = true
let mut current_theme: i32 = theme.THEME_DARK()

// Main IDE components
let mut menu_bar: widget::widget_data
let mut toolbar: widget::widget_data
let mut tabbar: widget::widget_data
let mut file_tree: widget::widget_data
let mut editor: widget::widget_data
let mut output_panel: widget::widget_data
let mut status_bar: widget::widget_data

// Dialog components
let mut dialog_visible: bool = false
let mut about_dialog: widget::widget_data
let mut about_close: widget::widget_data

// File system simulation
let mut files_content: text = "0:project|1:src|2:main.ny|2:utils.ny|2:widget.ny|1:docs|2:README.md|1:tests|2:test_suite.ny"
let mut current_file: text = "src/main.ny"
let mut editor_content: text = "# NYCromicon IDE - Advanced Widget Integration\n# Main source file\n\nimport widget\nimport theme\nimport winplat\n\nfunction main()\n    # Initialize the IDE interface\n    window = create_window(\"NYCromicon IDE\", 1200, 800)\n    \n    # Set up theme and layout\n    theme.set_theme(theme.THEME_DARK())\n    \n    # Create main components\n    menu_bar = create_menu_bar()\n    toolbar = create_toolbar()\n    file_tree = create_file_tree()\n    editor = create_text_editor()\n    output_panel = create_output_panel()\n    \n    # Main event loop\n    while running do\n        process_events()\n        render_interface()\n    end while\nend function\n\n# Advanced features demonstration:\n# - Multi-line text editing with line numbers\n# - Syntax highlighting (future implementation)\n# - File tree navigation\n# - Tabbed interface\n# - Theme switching\n# - Modal dialogs\n# - Real-time output panel\n\nlet result = main()\nprint(\"IDE initialized successfully\")"

pub function main()
    window_handle = winplat.create_window("NYCromicon IDE - Advanced Widget Integration", 1200, 800)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(current_theme)
    create_ide_components()
    create_dialog_components()

    while running do
        process_events()
        render_ide()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function create_ide_components()
    // Menu bar (top)
    menu_bar = widget.create_menu(0, 0, 1200, 25, "File,Edit,View,Tools,Help", 1000)

    // Toolbar (below menu)
    toolbar = widget.create_toolbar(0, 25, 1200, 30, "New,Open,Save,Build,Run,Debug", 1001)

    // Tab bar (below toolbar)
    tabbar = widget.create_tabbar_closeable(0, 55, 1200, 25, "main.ny,utils.ny,widget.ny,README.md,test_suite.ny", 1002)

    // File tree (left side)
    file_tree = widget.create_treeview(0, 80, 250, 600, files_content, 1003)

    // Editor (main area)
    editor = widget.create_text_editor(250, 80, 950, 550, editor_content, 1004)
    editor.options = "line_numbers,syntax"

    // Output panel (bottom)
    output_panel = widget.create_text_field(250, 630, 950, 120, "IDE ready. All advanced widgets integrated successfully.", 1005)
    output_panel.enabled = false

    // Status bar (bottom)
    status_bar = widget.create_statusbar(0, 750, 1200, 25, "NYCromicon IDE v4.0 | Line 15, Col 1 | NY Language | Build: Success", 1006)
end function

pub function create_dialog_components()
    // About dialog components
    about_dialog = widget.create_panel(400, 250, 400, 200, "About NYCromicon IDE", 2000)
    about_dialog.visible = false

    about_close = widget.create_button(550, 400, 100, 30, "Close", 2001)
    about_close.visible = false
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
        elseif event.event_type == winplat::EVENT_MOUSE_WHEEL() then
            handle_mouse_wheel(event.mouse_y)
        end if
    end while
end function

pub function handle_mouse_click(mouse_x: i32, mouse_y: i32)
    if dialog_visible then
        if widget.check_widget_click(mouse_x, mouse_y, about_close) then
            dialog_visible = false
            about_dialog.visible = false
            about_close.visible = false
        end if
        return
    end if

    // Process main interface clicks
    widget.check_widget_click(mouse_x, mouse_y, menu_bar)
    widget.check_widget_click(mouse_x, mouse_y, toolbar)
    widget.check_widget_click(mouse_x, mouse_y, tabbar)
    widget.check_widget_click(mouse_x, mouse_y, file_tree)
    widget.check_widget_click(mouse_x, mouse_y, editor)
end function

pub function handle_key_press(event: winplat::event)
    if dialog_visible then
        if event.key_code == 0x1001B then  // Escape
            dialog_visible = false
            about_dialog.visible = false
            about_close.visible = false
        end if
        return
    end if

    // Route to focused widget
    let focused_id = widget.get_focused_widget_id()

    if focused_id == editor.id then
        widget.process_key_event(event, editor)
        editor_content = editor.text
        update_status_bar()
    end if
end function

pub function handle_mouse_wheel(wheel_delta: i32)
    let focused_id = widget.get_focused_widget_id()
    if focused_id == editor.id then
        let scroll_amount = wheel_delta / 120
        let new_scroll = editor.selected_index - scroll_amount
        if new_scroll < 0 then new_scroll = 0 end if
        editor.selected_index = new_scroll
    end if
end function

pub function update_status_bar()
    let cursor_line = widget.editor_cursor_to_line(editor.text, editor.cursor_pos)
    let cursor_col = widget.editor_cursor_to_col(editor.text, editor.cursor_pos)
    status_bar.text = "NYCromicon IDE v4.0 | " + current_file + " | Line " + string_utils.int_to_text(cursor_line + 1) + ", Col " + string_utils.int_to_text(cursor_col + 1) + " | NY Language | Theme: " + get_theme_name()
end function

pub function get_theme_name() -> text
    if current_theme == theme.THEME_DARK() then
        return "Dark"
    else
        return "Light"
    end if
end function

pub function render_ide()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 1200, 800, theme.bg_primary())

    // Draw main components
    widget.draw_widget(window_handle, menu_bar)
    widget.draw_widget(window_handle, toolbar)
    widget.draw_widget(window_handle, tabbar)
    widget.draw_widget(window_handle, file_tree)
    widget.draw_widget(window_handle, editor)
    widget.draw_widget(window_handle, output_panel)
    widget.draw_widget(window_handle, status_bar)

    // Draw dialogs if visible
    if dialog_visible then
        winplat.fill_rect(window_handle, 0, 0, 1200, 800, 0x40000000)
        widget.draw_widget(window_handle, about_dialog)
        widget.draw_widget(window_handle, about_close)
    end if

    // Process signals
    process_signals()

    winplat.present(window_handle)
end function

pub function process_signals()
    while widget.has_pending_signals() do
        let sig = widget.pop_signal()

        if sig.signal_type == widget.SIGNAL_CLICKED() then
            if sig.widget_id == menu_bar.id then
                handle_menu_click(sig.data)
            elseif sig.widget_id == toolbar.id then
                handle_toolbar_click(sig.data)
            elseif sig.widget_id == file_tree.id then
                handle_file_tree_click(sig.data)
            end if
        elseif sig.signal_type == widget.SIGNAL_TEXT_CHANGED() then
            if sig.widget_id == editor.id then
                output_panel.text = "File modified: " + current_file
            end if
        elseif sig.signal_type == widget.SIGNAL_TAB_CHANGED() then
            handle_tab_change(sig.data)
        elseif sig.signal_type == widget.SIGNAL_TAB_CLOSED() then
            handle_tab_close(sig.data)
        end if
    end while
end function

pub function handle_menu_click(menu_item: text)
    if menu_item == "File" then
        output_panel.text = "File menu clicked - New, Open, Save, Exit options"
    elseif menu_item == "Edit" then
        output_panel.text = "Edit menu clicked - Cut, Copy, Paste, Find options"
    elseif menu_item == "View" then
        output_panel.text = "View menu clicked - Theme, Layout, Panels options"
        // Toggle theme for demo
        if current_theme == theme.THEME_DARK() then
            current_theme = theme.THEME_LIGHT()
        else
            current_theme = theme.THEME_DARK()
        end if
        theme.set_theme(current_theme)
    elseif menu_item == "Tools" then
        output_panel.text = "Tools menu clicked - Build, Run, Debug, Profile options"
    elseif menu_item == "Help" then
        output_panel.text = "Help menu clicked - Documentation, About options"
        show_about_dialog()
    end if
end function

pub function handle_toolbar_click(tool: text)
    if tool == "New" then
        output_panel.text = "New file created"
    elseif tool == "Open" then
        output_panel.text = "Open file dialog (not implemented in demo)"
    elseif tool == "Save" then
        output_panel.text = "File saved: " + current_file
    elseif tool == "Build" then
        output_panel.text = "Building project... Build successful!"
    elseif tool == "Run" then
        output_panel.text = "Running application... Application started successfully"
    elseif tool == "Debug" then
        output_panel.text = "Starting debugger... Debug session active"
    end if
end function

pub function handle_file_tree_click(node: text)
    if string_utils.contains(node, ".ny") or string_utils.contains(node, ".md") then
        current_file = node
        update_status_bar()

        // Simulate loading different file content
        if node == "utils.ny" then
            editor.text = "# Utility functions for NYCromicon\n\nfunction format_text(text)\n    return string.trim(text)\nend function\n\nfunction validate_syntax(code)\n    # Syntax validation logic\n    return true\nend function"
        elseif node == "widget.ny" then
            editor.text = "# Widget system implementation\n\nmodule widget\n\n# Widget type definitions\ntype widget_data\n    type: i32\n    x: i32\n    y: i32\n    width: i32\n    height: i32\n    text: text\nend type"
        elseif node == "README.md" then
            editor.text = "# NYCromicon IDE\n\nAn advanced IDE for the NY programming language.\n\n## Features\n\n- Advanced text editing\n- Syntax highlighting\n- File management\n- Build system\n- Debugging tools\n\n## Getting Started\n\n1. Install NY compiler\n2. Create new project\n3. Start coding!"
        else
            editor.text = editor_content
        end if
    end if
end function

pub function handle_tab_change(tab_name: text)
    current_file = tab_name
    update_status_bar()
    output_panel.text = "Switched to tab: " + tab_name
end function

pub function handle_tab_close(tab_name: text)
    output_panel.text = "Closed tab: " + tab_name
end function

pub function show_about_dialog()
    dialog_visible = true
    about_dialog.visible = true
    about_close.visible = true

    about_dialog.text = "NYCromicon IDE v4.0\n\nAdvanced IDE for NY Programming Language\n\nFeatures:\n• Multi-line text editor with line numbers\n• Tabbed interface with closeable tabs\n• Hierarchical file tree view\n• Theme switching (Dark/Light)\n• Modal dialog system\n• Integrated toolbar and menu system\n• Real-time output panel\n• Mouse wheel scrolling\n\nBuilt with NILAIX Widget System"
end function