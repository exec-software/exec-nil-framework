module visual_test
import winplat

// Minimal visual test - draws an IDE-like layout using only winplat primitives
// This bypasses the widget system to test the full compilation pipeline

pub function main()
    // Create window
    let handle = winplat.create_window("NYCromicon IDE - Visual Test", 1024, 700)
    if handle == 0 then
        return
    end if

    winplat.show_window(handle)

    // Colors (dark theme)
    let bg_color = 0x1E1E2E          // Dark background
    let toolbar_color = 0x181825      // Toolbar
    let sidebar_color = 0x1E1E2E     // Sidebar
    let editor_color = 0x1E1E2E      // Editor
    let statusbar_color = 0x181825   // Status bar
    let border_color = 0x45475A      // Borders
    let text_color = 0xCDD6F4        // Primary text
    let accent_color = 0x89B4FA      // Accent/buttons
    let tab_active_color = 0x313244  // Active tab
    let tab_inactive_color = 0x1E1E2E // Inactive tab
    let gutter_color = 0x181825      // Line number gutter
    let line_num_color = 0x6C7086    // Line numbers
    let keyword_color = 0xCBA6F7     // Keywords
    let string_color = 0xA6E3A1      // Strings
    let comment_color = 0x6C7086     // Comments
    let selection_color = 0x45475A   // Selection highlight
    let cursor_color = 0xF5E0DC      // Cursor
    let treeview_arrow_color = 0x89B4FA  // Treeview arrows

    // Layout constants
    let toolbar_height = 32
    let tabbar_height = 28
    let sidebar_width = 200
    let statusbar_height = 24
    let gutter_width = 48
    let line_height = 16
    let char_width = 8

    let mut running = true
    let mut mouse_x = 0
    let mut mouse_y = 0
    let mut frame_count = 0

    while running do
        winplat.poll_events()

        if winplat.should_close() then
            running = false
        end if

        // Process events
        let mut evt = winplat.get_next_event()
        while evt.event_type != 0 do
            if evt.event_type == winplat.EVENT_WINDOW_CLOSE() then
                running = false
            elseif evt.event_type == winplat.EVENT_MOUSE_MOTION() then
                mouse_x = evt.mouse_x
                mouse_y = evt.mouse_y
            elseif evt.event_type == winplat.EVENT_KEY_PRESS() then
                if evt.key_code == 27 then
                    running = false
                end if
            end if
            evt = winplat.get_next_event()
        end while

        // Begin drawing
        let dc = winplat.begin_draw(handle)
        winplat.clear_window(dc, bg_color)

        // === TOOLBAR ===
        winplat.fill_rect(handle, 0, 0, 1024, toolbar_height, toolbar_color)
        // Toolbar buttons
        winplat.fill_rect(handle, 8, 6, 50, 20, accent_color)
        winplat.draw_text(handle, 14, 8, "New", 0x1E1E2E)
        winplat.fill_rect(handle, 66, 6, 50, 20, accent_color)
        winplat.draw_text(handle, 72, 8, "Open", 0x1E1E2E)
        winplat.fill_rect(handle, 124, 6, 50, 20, accent_color)
        winplat.draw_text(handle, 130, 8, "Save", 0x1E1E2E)
        winplat.fill_rect(handle, 190, 6, 50, 20, accent_color)
        winplat.draw_text(handle, 196, 8, "Build", 0x1E1E2E)
        winplat.fill_rect(handle, 248, 6, 40, 20, accent_color)
        winplat.draw_text(handle, 254, 8, "Run", 0x1E1E2E)
        // Toolbar border
        winplat.fill_rect(handle, 0, toolbar_height - 1, 1024, 1, border_color)

        // === TAB BAR ===
        let tab_y = toolbar_height
        winplat.fill_rect(handle, sidebar_width, tab_y, 824, tabbar_height, toolbar_color)
        // Active tab
        winplat.fill_rect(handle, sidebar_width, tab_y, 120, tabbar_height, tab_active_color)
        winplat.draw_text(handle, sidebar_width + 8, tab_y + 6, "main.ny", text_color)
        winplat.draw_text(handle, sidebar_width + 104, tab_y + 6, "x", 0x6C7086)
        // Inactive tab
        winplat.fill_rect(handle, sidebar_width + 120, tab_y, 120, tabbar_height, tab_inactive_color)
        winplat.draw_text(handle, sidebar_width + 128, tab_y + 6, "widget.ny", 0x6C7086)
        // Tab bar border
        winplat.fill_rect(handle, sidebar_width, tab_y + tabbar_height - 1, 824, 1, border_color)

        // === SIDEBAR (TreeView) ===
        let content_y = toolbar_height
        let content_height = 700 - toolbar_height - statusbar_height
        winplat.fill_rect(handle, 0, content_y, sidebar_width, content_height, sidebar_color)
        // Sidebar header
        winplat.fill_rect(handle, 0, content_y, sidebar_width, 24, toolbar_color)
        winplat.draw_text(handle, 8, content_y + 4, "EXPLORER", 0x6C7086)
        // Tree entries
        let tree_y = content_y + 28
        winplat.draw_text(handle, 8, tree_y, ">", treeview_arrow_color)
        winplat.draw_text(handle, 20, tree_y, "nycromicon/", text_color)
        winplat.draw_text(handle, 24, tree_y + 18, ">", treeview_arrow_color)
        winplat.draw_text(handle, 36, tree_y + 18, "src/", text_color)
        // Files (indented)
        winplat.fill_rect(handle, 0, tree_y + 36, sidebar_width, 16, selection_color)
        winplat.draw_text(handle, 48, tree_y + 36, "main.ny", accent_color)
        winplat.draw_text(handle, 48, tree_y + 54, "editor.ny", 0x6C7086)
        winplat.draw_text(handle, 8, tree_y + 76, ">", treeview_arrow_color)
        winplat.draw_text(handle, 20, tree_y + 76, "nilaix/", text_color)
        winplat.draw_text(handle, 8, tree_y + 94, ">", treeview_arrow_color)
        winplat.draw_text(handle, 20, tree_y + 94, "nillibrary/", text_color)
        // Sidebar border
        winplat.fill_rect(handle, sidebar_width - 1, content_y, 1, content_height, border_color)

        // === EDITOR AREA ===
        let editor_x = sidebar_width
        let editor_y = toolbar_height + tabbar_height
        let editor_width = 1024 - sidebar_width
        let editor_height = content_height - tabbar_height

        // Editor background
        winplat.fill_rect(handle, editor_x, editor_y, editor_width, editor_height, editor_color)

        // Line number gutter
        winplat.fill_rect(handle, editor_x, editor_y, gutter_width, editor_height, gutter_color)

        // Draw sample code with syntax highlighting
        let mut line_y = editor_y + 4
        let code_x = editor_x + gutter_width + 8

        // Line 1
        winplat.draw_text(handle, editor_x + 16, line_y, "1", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "module", keyword_color)
        winplat.draw_text(handle, code_x + 56, line_y, "nycromicon", text_color)
        line_y = line_y + line_height

        // Line 2
        winplat.draw_text(handle, editor_x + 16, line_y, "2", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "import", keyword_color)
        winplat.draw_text(handle, code_x + 56, line_y, "winplat", text_color)
        line_y = line_y + line_height

        // Line 3
        winplat.draw_text(handle, editor_x + 16, line_y, "3", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "import", keyword_color)
        winplat.draw_text(handle, code_x + 56, line_y, "widget", text_color)
        line_y = line_y + line_height

        // Line 4 (blank)
        winplat.draw_text(handle, editor_x + 16, line_y, "4", line_num_color)
        line_y = line_y + line_height

        // Line 5 - comment
        winplat.draw_text(handle, editor_x + 16, line_y, "5", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "// NYCromicon IDE", comment_color)
        line_y = line_y + line_height

        // Line 6
        winplat.draw_text(handle, editor_x + 16, line_y, "6", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "pub", keyword_color)
        winplat.draw_text(handle, code_x + 32, line_y, "function", keyword_color)
        winplat.draw_text(handle, code_x + 104, line_y, "main()", text_color)
        line_y = line_y + line_height

        // Line 7
        winplat.draw_text(handle, editor_x + 16, line_y, "7", line_num_color)
        winplat.draw_text(handle, code_x + 32, line_y, "let", keyword_color)
        winplat.draw_text(handle, code_x + 56, line_y, "title =", text_color)
        winplat.draw_text(handle, code_x + 120, line_y, "\"NYCromicon\"", string_color)
        line_y = line_y + line_height

        // Line 8
        winplat.draw_text(handle, editor_x + 16, line_y, "8", line_num_color)
        winplat.draw_text(handle, code_x + 32, line_y, "let", keyword_color)
        winplat.draw_text(handle, code_x + 56, line_y, "w = winplat.create_window(title, 1200, 800)", text_color)
        line_y = line_y + line_height

        // Line 9
        winplat.draw_text(handle, editor_x + 16, line_y, "9", line_num_color)
        winplat.draw_text(handle, code_x + 32, line_y, "winplat.show_window(w)", text_color)
        line_y = line_y + line_height

        // Line 10
        winplat.draw_text(handle, editor_x + 8, line_y, "10", line_num_color)
        winplat.draw_text(handle, code_x, line_y, "end", keyword_color)
        winplat.draw_text(handle, code_x + 32, line_y, "function", keyword_color)
        line_y = line_y + line_height

        // Cursor (blinking)
        if frame_count / 30 - (frame_count / 30 / 2) * 2 == 0 then
            winplat.fill_rect(handle, code_x + 56, editor_y + 4 + 6 * line_height, 2, line_height, cursor_color)
        end if

        // === STATUS BAR ===
        let sb_y = 700 - statusbar_height
        winplat.fill_rect(handle, 0, sb_y, 1024, statusbar_height, statusbar_color)
        winplat.fill_rect(handle, 0, sb_y, 1024, 1, border_color)
        winplat.draw_text(handle, 8, sb_y + 4, "Ln 7, Col 5", text_color)
        winplat.draw_text(handle, 200, sb_y + 4, "main.ny", 0x6C7086)
        winplat.draw_text(handle, 400, sb_y + 4, "NY Language", 0x6C7086)
        winplat.draw_text(handle, 800, sb_y + 4, "NYCromicon v4.0", accent_color)

        winplat.end_draw(dc)

        frame_count = frame_count + 1
    end while

    winplat.destroy_window(handle)
end function
