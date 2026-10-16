module dialog_system_test
# Test application for dialog/modal system with overlay and focus trapping

import widget
import theme
import winplat
import string_utils

let mut window_handle: i32 = -1
let mut running: bool = true
let mut main_button: widget::widget_data
let mut dialog_visible: bool = false
let mut dialog_overlay: widget::widget_data
let mut dialog_title: widget::widget_data
let mut dialog_message: widget::widget_data
let mut dialog_input: widget::widget_data
let mut dialog_ok_button: widget::widget_data
let mut dialog_cancel_button: widget::widget_data
let mut saved_focus_id: i32 = -1

pub function main()
    window_handle = winplat.create_window("Dialog System Test", 800, 600)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    create_main_widgets()
    create_dialog_widgets()

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function create_main_widgets()
    main_button = widget.create_button(350, 275, 100, 30, "Show Dialog", 300)
end function

pub function create_dialog_widgets()
    // Dialog overlay (semi-transparent background)
    dialog_overlay = widget.create_panel(0, 0, 800, 600, "", 400)
    dialog_overlay.visible = false
    dialog_overlay.enabled = false

    // Dialog components
    dialog_title = widget.create_label(300, 200, 200, 25, "Input Dialog", 401)
    dialog_title.visible = false

    dialog_message = widget.create_label(300, 240, 200, 40, "Please enter your name:", 402)
    dialog_message.visible = false

    dialog_input = widget.create_text_field(300, 290, 200, 25, "", 403)
    dialog_input.visible = false

    dialog_ok_button = widget.create_button(320, 340, 70, 25, "OK", 404)
    dialog_ok_button.visible = false

    dialog_cancel_button = widget.create_button(410, 340, 70, 25, "Cancel", 405)
    dialog_cancel_button.visible = false
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
    if dialog_visible then
        // Dialog is active - only process dialog clicks
        if widget.check_widget_click(mouse_x, mouse_y, dialog_input) then
            // Input field clicked - already handled
        elseif widget.check_widget_click(mouse_x, mouse_y, dialog_ok_button) then
            // OK button clicked
            close_dialog(true)
        elseif widget.check_widget_click(mouse_x, mouse_y, dialog_cancel_button) then
            // Cancel button clicked
            close_dialog(false)
        end if
    else
        // Main interface active
        widget.check_widget_click(mouse_x, mouse_y, main_button)
    end if
end function

pub function handle_key_press(event: winplat::event)
    if dialog_visible then
        // Dialog active - trap focus within dialog
        if event.key_code == 0x1001B then  // Escape key
            close_dialog(false)
        elseif event.key_code == 0x1000D then  // Enter key
            close_dialog(true)
        else
            // Route other keys to dialog input
            widget.process_key_event(event, dialog_input)
        end if
    else
        // Main interface active
        widget.process_key_event(event, main_button)
    end if
end function

pub function show_dialog()
    dialog_visible = true
    saved_focus_id = widget.get_focused_widget_id()

    // Show dialog components
    dialog_overlay.visible = true
    dialog_title.visible = true
    dialog_message.visible = true
    dialog_input.visible = true
    dialog_ok_button.visible = true
    dialog_cancel_button.visible = true

    // Enable dialog components
    dialog_overlay.enabled = true
    dialog_input.enabled = true
    dialog_ok_button.enabled = true
    dialog_cancel_button.enabled = true

    // Set focus to input field
    widget.set_focused_widget_id(dialog_input.id)
    dialog_input.cursor_pos = string_utils.length(dialog_input.text)
end function

pub function close_dialog(accepted: bool)
    dialog_visible = false

    // Hide dialog components
    dialog_overlay.visible = false
    dialog_title.visible = false
    dialog_message.visible = false
    dialog_input.visible = false
    dialog_ok_button.visible = false
    dialog_cancel_button.visible = false

    // Disable dialog components
    dialog_overlay.enabled = false
    dialog_input.enabled = false
    dialog_ok_button.enabled = false
    dialog_cancel_button.enabled = false

    // Restore focus
    widget.set_focused_widget_id(saved_focus_id)

    // Process dialog result
    if accepted then
        let input_text = dialog_input.text
        if string_utils.length(input_text) > 0 then
            main_button.text = "Hello, " + input_text + "!"
        else
            main_button.text = "No input provided"
        end if
    else
        main_button.text = "Dialog cancelled"
    end if
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 800, 600, theme.bg_primary())

    // Draw main interface
    widget.draw_widget(window_handle, main_button)

    // Draw instructions
    winplat.draw_text(window_handle, 50, 50, "Dialog System Test", theme.text_primary())
    winplat.draw_text(window_handle, 50, 80, "Click the button to show a modal dialog.", theme.text_secondary())
    winplat.draw_text(window_handle, 50, 100, "Dialog features:", theme.text_secondary())
    winplat.draw_text(window_handle, 50, 120, "• Overlay with semi-transparent background", theme.text_secondary())
    winplat.draw_text(window_handle, 50, 140, "• Focus trapping within dialog", theme.text_secondary())
    winplat.draw_text(window_handle, 50, 160, "• Escape to cancel, Enter to accept", theme.text_secondary())

    if dialog_visible then
        // Draw dialog overlay (semi-transparent)
        winplat.fill_rect(window_handle, 0, 0, 800, 600, 0x40000000)  // Semi-transparent black

        // Draw dialog background
        winplat.fill_rect(window_handle, 280, 180, 240, 200, theme.panel_bg())
        winplat.fill_rect(window_handle, 280, 180, 240, 1, theme.border_primary())
        winplat.fill_rect(window_handle, 280, 379, 240, 1, theme.border_primary())
        winplat.fill_rect(window_handle, 280, 180, 1, 200, theme.border_primary())
        winplat.fill_rect(window_handle, 519, 180, 1, 200, theme.border_primary())

        // Draw dialog components
        widget.draw_widget(window_handle, dialog_title)
        widget.draw_widget(window_handle, dialog_message)
        widget.draw_widget(window_handle, dialog_input)
        widget.draw_widget(window_handle, dialog_ok_button)
        widget.draw_widget(window_handle, dialog_cancel_button)
    end if

    // Process signals
    process_signals()

    winplat.present(window_handle)
end function

pub function process_signals()
    while widget.has_pending_signals() do
        let sig = widget.pop_signal()

        if sig.signal_type == widget.SIGNAL_CLICKED() then
            if sig.widget_id == main_button.id and not dialog_visible then
                show_dialog()
            end if
        end if
    end while
end function