# Drag and Drop Test
# Demonstrates comprehensive drag and drop functionality

import widget
import theme
import winplat

function main()
    let window = winplat.create_window("Drag and Drop Test", 900, 700)
    theme.set_theme(theme.THEME_DARK())

    # Create various widgets for drag and drop testing
    let editor1 = widget.create_text_editor(20, 20, 400, 300, "Text Editor 1\n\nDrag selected text from here to other widgets.\n\nSelect some text and start dragging!", 1001)
    let editor2 = widget.create_text_editor(480, 20, 400, 300, "Text Editor 2\n\nDrop text here.\n\nAccepts text and file drops.", 1002)

    # Create draggable buttons
    let button1 = widget.create_button(20, 340, 180, 40, "Drag Me Button", 1003)
    let button2 = widget.create_button(220, 340, 180, 40, "Drag Too Button", 1004)

    # Create labels for drop targets
    let label1 = widget.create_label(20, 400, 180, 30, "Drop Zone 1 - Text Only", 1005)
    let label2 = widget.create_label(220, 400, 180, 30, "Drop Zone 2 - Any", 1006)

    # Create panels as drop zones
    let panel1 = widget.create_panel(420, 340, 460, 120, "Panel Drop Zone", 1007)
    let panel2 = widget.create_panel(420, 480, 460, 120, "Universal Drop Zone", 1008)

    # Status display
    let mut last_action = "Ready for drag and drop testing"
    let mut action_timer = 0
    let mut drag_status = "No active drag"

    # Array of all widgets
    let widgets = [editor1, editor2, button1, button2, label1, label2, panel1, panel2]

    # Set up drop zones
    let mut i = 0
    while i < array.length(widgets) do
        widget.update_widget_drop_zones(widgets[i])
        i = i + 1
    end while

    let mut running = true

    while running do
        let event = winplat.get_next_event()

        if event.event_type == winplat.EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat.EVENT_KEY_PRESS() then
            if event.key_code == 27 then  # Escape
                if widget.is_drag_active() then
                    widget.cancel_drag()
                    last_action = "Drag cancelled with Escape"
                    action_timer = 120
                else
                    running = false
                end if
            end if
        end if

        # Handle widget events
        let mut j = 0
        while j < array.length(widgets) do
            widget.process_key_event(event, widgets[j])
            widget.process_mouse_click_event(event, widgets[j])

            # Handle drag start for each widget
            if event.event_type == winplat.EVENT_MOUSE_CLICK() and not widget.is_drag_active() then
                widget.handle_widget_drag_start(event, widgets[j])
            end if

            j = j + 1
        end while

        # Process drag motion
        widget.process_drag_mouse_motion(event)

        # Check for drag and drop signals
        let mut k = 0
        while k < widget.get_signal_count() do
            let signal = widget.get_signal_at(k)

            match signal.signal_type
                case widget.SIGNAL_DRAG_STARTED():
                    last_action = "Drag started from widget " + string_utils.int_to_text(signal.sender_id)
                    action_timer = 120
                case widget.SIGNAL_DRAG_ENDED():
                    last_action = "Drag ended: " + signal.data
                    action_timer = 120
                case widget.SIGNAL_DROPPED():
                    last_action = "Dropped on widget " + string_utils.int_to_text(signal.sender_id)
                    action_timer = 120
                case widget.SIGNAL_DRAG_ENTER():
                    drag_status = "Drag over widget " + string_utils.int_to_text(signal.sender_id)
                case widget.SIGNAL_DRAG_EXIT():
                    drag_status = "No drag over"
                case widget.SIGNAL_CLICKED():
                    if string_utils.contains(signal.data, "drag") then
                        last_action = "Drag action: " + signal.data
                        action_timer = 120
                    end if
            end match

            k = k + 1
        end while

        # Clear processed signals
        widget.clear_signals()

        # Update drag status
        if widget.is_drag_active() then
            let drag_pos = widget.get_drag_position()
            let drag_x = widget.extract_drag_x(drag_pos)
            let drag_y = widget.extract_drag_y(drag_pos)
            drag_status = "Dragging at (" + string_utils.int_to_text(drag_x) + ", " + string_utils.int_to_text(drag_y) + ")"
        else
            drag_status = "No active drag"
        end if

        # Draw everything
        let dc = winplat.begin_draw(window)
        winplat.clear_window(dc, theme.background())

        # Draw all widgets
        let mut l = 0
        while l < array.length(widgets) do
            widget.draw_widget(dc, widgets[l])
            l = l + 1
        end while

        # Draw drop zone indicators
        widget.draw_drop_zone_indicators(dc, widgets)

        # Draw drag feedback
        widget.draw_drag_feedback(dc)

        # Draw context menu if visible
        widget.draw_context_menu(dc)

        # Draw status bar
        winplat.fill_rect(dc, 0, 640, 900, 60, theme.statusbar_background())
        winplat.draw_rect(dc, 0, 640, 900, 60, theme.statusbar_border())

        if action_timer > 0 then
            winplat.draw_text(dc, 10, 650, last_action, theme.statusbar_text())
            action_timer = action_timer - 1
        else
            winplat.draw_text(dc, 10, 650, "Try dragging text, buttons, and widgets between areas", theme.statusbar_text())
        end if

        winplat.draw_text(dc, 10, 670, drag_status, theme.statusbar_text())
        winplat.draw_text(dc, 10, 690, "Press Escape to cancel drag or quit", theme.statusbar_text())

        winplat.end_draw(dc)
    end while

    winplat.destroy_window(window)
    return 0
end function

let result = main()