# Keyboard Shortcuts Test
# Demonstrates comprehensive keyboard shortcuts in text editor

import widget
import theme
import winplat

function main()
    let window = winplat.create_window("Keyboard Shortcuts Test", 800, 600)
    theme.set_theme(theme.THEME_DARK())

    # Create a text editor with sample content
    let editor_content = "# Keyboard Shortcuts Test\n\nTry these shortcuts:\n\nEditing:\n- Ctrl+A: Select All\n- Ctrl+C: Copy\n- Ctrl+V: Paste\n- Ctrl+X: Cut\n- Ctrl+Z: Undo\n- Ctrl+Y: Redo\n\nSearch:\n- Ctrl+F: Find\n- Ctrl+G: Find Next\n- Ctrl+H: Replace\n- F3: Find Next\n\nFile:\n- Ctrl+S: Save\n- Ctrl+W: Close Tab\n- Ctrl+T: New Tab\n\nOther:\n- Escape: Cancel/Hide Menu\n\nStart typing to test all features!"

    let editor = widget.create_text_editor(20, 20, 760, 560, editor_content, 1001)

    let mut last_action = "Ready"
    let mut action_timer = 0

    let mut running = true

    while running do
        let event = winplat.get_next_event()

        if event.event_type == winplat.EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat.EVENT_KEY_PRESS() then
            if event.key_code == 27 then  # Escape
                running = false
            end if
        end if

        # Process widget events and get signals
        widget.process_key_event(event, editor)
        widget.process_mouse_click_event(event, editor)

        # Check for keyboard shortcut signals
        let mut i = 0
        while i < widget.get_signal_count() do
            let signal = widget.get_signal_at(i)
            if signal.sender_id == 1001 and signal.signal_type == widget.SIGNAL_CLICKED() then
                last_action = "Shortcut: " + signal.data
                action_timer = 120  # Show for 2 seconds at 60fps
            end if
            i = i + 1
        end while

        # Clear processed signals
        widget.clear_signals()

        # Draw everything
        let dc = winplat.begin_draw(window)
        winplat.clear_window(dc, theme.background())

        # Draw editor
        widget.draw_widget(dc, editor)

        # Draw context menu if visible
        widget.draw_context_menu(dc)

        # Draw status bar
        if action_timer > 0 then
            winplat.draw_text(dc, 20, 580, last_action, theme.statusbar_text())
            action_timer = action_timer - 1
        else
            winplat.draw_text(dc, 20, 580, "Try keyboard shortcuts (Ctrl+A, C, V, X, Z, Y, F, G, H, S, W, T, F3, Esc)", theme.statusbar_text())
        end if

        winplat.end_draw(dc)
    end while

    winplat.destroy_window(window)
    return 0
end function

let result = main()