// textfield_test.ny - Test NILAIX text field widget functionality

import widget
import winplat
import theme
import char_utils // Import new char utilities
import string_utils // Import new string utilities

pub function main() -> i32
    // Create window
    let window = winplat.create_window("Text Field Widget Test", 600, 400)
    if window == 0 then
        print("ERROR: Failed to create window")
        return 1
    end if

    winplat.show_window(window)

    // Create text field widgets
    let text_field1 = widget.create_text_field(50, 50, 200, 30, "Hello", 1)
    let text_field2 = widget.create_text_field(50, 100, 200, 30, "World", 2)
    let output_label = widget.create_label(50, 150, 500, 30, "Text Field 1: " + text_field1.text, 3)
    let output_label2 = widget.create_label(50, 200, 500, 30, "Text Field 2: " + text_field2.text, 4)

    print("Text field widget test started!")
    print("Text Field 1: at (50, 50), initial text: " + text_field1.text)
    print("Text Field 2: at (50, 100), initial text: " + text_field2.text)

    // Main loop
    while not winplat.should_close()
        winplat.poll_events()

        // Get events
        let event = winplat.get_next_event()

        // Process mouse clicks for focus
        if event.event_type == winplat.EVENT_MOUSE_CLICK() then
            if widget.check_widget_click(event.mouse_x, event.mouse_y, text_field1) then
                print("Text Field 1 clicked!")
            elseif widget.check_widget_click(event.mouse_x, event.mouse_y, text_field2) then
                print("Text Field 2 clicked!")
            end if
        end if

        // Process key events for text input
        if event.event_type == winplat.EVENT_KEY_PRESS() then
            // Attempt to process key for focused widget
            if widget.get_focused_widget_id() == text_field1.id then
                widget.process_key_event(event, text_field1)
            elseif widget.get_focused_widget_id() == text_field2.id then
                widget.process_key_event(event, text_field2)
            end if
        end if

        // Draw everything
        let dc = winplat.begin_draw(window)

        // Clear background
        winplat.clear_window(dc, theme.bg_editor())

        // Draw widgets
        widget.draw_widget(dc, text_field1)
        widget.draw_widget(dc, text_field2)
        widget.draw_widget(dc, output_label)
        widget.draw_widget(dc, output_label2)

        // Update label text based on text field content
        output_label.text = "Text Field 1: " + text_field1.text
        output_label2.text = "Text Field 2: " + text_field2.text

        winplat.end_draw(dc)

        // Handle blinking caret for focused widget
        // This would ideally be driven by a timer, but for simplicity, we'll toggle on each frame
        if winplat.get_simulated_time_ms() % 500 < 250 then
            text_field1.caret_blink_state = true
            text_field2.caret_blink_state = true
        else
            text_field1.caret_blink_state = false
            text_field2.caret_blink_state = false
        end if

        // Check for widget signals
        let signal = widget.get_pending_signal()
        if signal.widget_id != -1 then
            print("Signal received: Widget " + to_string(signal.widget_id) +
                  " Type " + to_string(signal.signal_type) +
                  " Data: " + signal.data)
            widget.clear_signal()
        end if

    end while

    print("Text field widget test completed!")
    winplat.destroy_window(window)
    return 0
end function

@interop("runtime")
function print(s: text) -> void
end function

@interop("runtime")
function to_string(i: i32) -> text
end function