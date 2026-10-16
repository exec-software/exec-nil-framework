module main
import widget
import winplat
import theme
import string_utils

# ============================================================================= #
# Basic Nyaix Example Application
# Demonstrates widget creation, layout management,
# and basic event handling.
# ============================================================================= #

pub function main() -> i32
    # Create main window
    let handle = winplat::create_window("Nyaix Basic Example", 800, 600)

    if handle == 0 then
        println("ERROR: Failed to create window")
        return 1
    end if

    println("Window created successfully!")

    # Create widgets
    let label_hello = widget::create_label(50, 50, 200, 30, "Hello, Nyaix!", 1)
    let label_status = widget::create_label(50, 90, 200, 20, "Status: Ready", 2)
    let button_click = widget::create_button(50, 130, 120, 30, "Click Me!", 3)
    let text_input = widget::create_text_field(50, 180, 300, 40, "Type here...", 4)

    # Apply layouts
    let mut widgets: vec<widget> = []
    widgets.push(label_hello)
    widgets.push(label_status)
    widgets.push(button_click)
    widgets.push(text_input)

    println("Created " + string_utils.int_to_text(widgets.length()) + " widgets")

    # Main event loop
    let mut running = true

    while running do
        let event = winplat::next_event()

        if event.event_type == 0 then
            # Window event
            if event.key_code == 1 then
                # ESC key
                println("ESC pressed, exiting...")
                running = false
            end if
        end if

        winplat::begin_draw_impl(handle, 0)
        winplat::end_draw_impl(handle, 0)

        winplat::show_window_impl(handle)
        winplat::poll_events_impl()
    end while

    # Cleanup
    winplat::destroy_window(handle)
    println("Application exited")

    return 0
end function
