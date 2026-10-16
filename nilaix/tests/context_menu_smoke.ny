# Context Menu Test
# Simple demonstration of the context menu functionality

import widget
import theme
import winplat

function main()
    let window = winplat.create_window("Context Menu Test", 800, 600)

    # Create a text editor
    let editor = widget.create_text_editor(50, 50, 700, 400, "# Context Menu Test\n\nRight-click (Ctrl+Click) in this editor to see the context menu.\n\nTry:\n- Cut, Copy, Paste\n- Select All\n- Undo, Redo", 1001)

    let mut running = true

    while running do
        # Process events
        let event = winplat.get_next_event()
        if event.event_type == winplat.EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat.EVENT_KEY_PRESS() then
            if event.key_code == 27 then  # Escape key
                running = false
            end if
        elseif event.event_type == winplat.EVENT_MOUSE_CLICK() then
            # Handle context menu (Ctrl+Click for right-click)
            if event.modifiers and winplat.MODIFIER_CTRL() != 0 then
                # Show context menu at mouse position
                let items = ["Cut", "Copy", "Paste", "", "Select All", "", "Undo", "Redo"]
                let actions = [1, 2, 3, 0, 4, 0, 5, 6]
                widget.show_context_menu(event.mouse_x, event.mouse_y, items, actions)
            else
                # Handle regular clicks
                let context_action = widget.handle_context_menu_click(event.mouse_x, event.mouse_y)
                if context_action > 0 then
                # Context menu action selected
                if context_action == 1 then print("Cut selected")
                elseif context_action == 2 then print("Copy selected")
                elseif context_action == 3 then print("Paste selected")
                elseif context_action == 4 then print("Select All selected")
                elseif context_action == 5 then print("Undo selected")
                elseif context_action == 6 then print("Redo selected")
                end if
                else
                    # Process regular widget click
                    widget.process_mouse_click_event(event, editor)
                end if
            end if
        end if

        # Handle context menu motion
        if event.event_type == winplat.EVENT_MOUSE_MOTION() then
            widget.handle_context_menu_motion(event.mouse_x, event.mouse_y)
        end if

        # Draw everything
        let dc = winplat.begin_draw(window)
        winplat.clear_window(dc, theme.background())

        # Draw editor
        widget.draw_widget(dc, editor)

        # Draw context menu if visible
        widget.draw_context_menu(dc)

        winplat.end_draw(dc)

        # Hide context menu on escape
        if event.event_type == winplat.EVENT_KEY_PRESS() and event.key_code == 27 then
            widget.hide_context_menu()
        end if
    end while

    winplat.destroy_window(window)
    return 0
end function

let result = main()