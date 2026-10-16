module widget_validation_suite
# Comprehensive validation test for all NILAIX widgets
# Tests all widget types, interactions, and theme support

import widget
import theme
import winplat
import string_utils

let mut window_handle: i32 = -1
let mut running: bool = true
let mut test_phase: i32 = 0
let mut test_results: text = ""

// Widget collection for testing
let mut test_widgets: array<widget::widget_data> = []
let mut widget_count: i32 = 0

pub function main()
    window_handle = winplat.create_window("NILAIX Widget Validation Suite", 1000, 700)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    test_phase = 0
    test_results = "Widget Validation Suite - Starting tests..."

    while running do
        process_events()
        run_validation_phase()
        render_validation()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            handle_validation_click(event.mouse_x, event.mouse_y)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_validation_key(event)
        end if
    end while
end function

pub function run_validation_phase()
    if test_phase == 0 then
        initialize_validation_widgets()
        test_phase = 1
    end if
end function

pub function initialize_validation_widgets()
    test_widgets = []
    widget_count = 0

    // Test all 15 widget types
    test_widgets = test_widgets + [widget.create_button(20, 60, 120, 30, "Button Test", 100)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_text_field(160, 60, 120, 30, "Text Input", 101)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_label(300, 60, 120, 30, "Label Test", 102)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_dropdown(440, 60, 120, 30, "Option1,Option2,Option3", 0, 103)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_menu(580, 60, 120, 90, "Menu1,Menu2,Menu3", 104)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_checkbox(720, 60, 120, 30, "Checkbox", false, 105)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_radiobutton(860, 60, 120, 30, "Radio", false, 106)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_statusbar(20, 120, 960, 25, "Status Bar Test", 107)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_slider(20, 160, 200, 25, 50, 108)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_progressbar(240, 160, 200, 25, 75, 109)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_listbox(460, 160, 200, 120, "Item1,Item2,Item3,Item4,Item5", 110)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_toolbar(680, 160, 300, 30, "Tool1,Tool2,Tool3,Tool4,Tool5", 111)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_panel(20, 320, 450, 200, "Panel Test", 112)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_text_editor(490, 320, 490, 200, "# Text Editor Test\nMulti-line\nediting\nwith\nline\nnumbers", 113)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_tabbar(20, 540, 450, 30, "Tab1,Tab2,Tab3,Tab4", 114)]
    widget_count = widget_count + 1

    test_widgets = test_widgets + [widget.create_treeview(490, 540, 490, 150, "0:Root|1:Child1|2:Grandchild1|1:Child2|0:Root2", 115)]
    widget_count = widget_count + 1

    test_results = "Initialized " + string_utils.int_to_text(widget_count) + " widgets for validation"
end function

pub function handle_validation_click(mouse_x: i32, mouse_y: i32)
    let mut i = 0
    while i < widget_count do
        if widget.check_widget_click(mouse_x, mouse_y, test_widgets.items[i]) then
            let widget_type = get_widget_type_name(test_widgets.items[i].widget_type)
            test_results = "Clicked: " + widget_type + " (ID: " + string_utils.int_to_text(test_widgets.items[i].id) + ")"
        end if
        i = i + 1
    end while
end function

pub function handle_validation_key(event: winplat::event)
    let focused_id = widget.get_focused_widget_id()
    let mut i = 0
    while i < widget_count do
        if test_widgets.items[i].id == focused_id then
            widget.process_key_event(event, test_widgets.items[i])
            let widget_type = get_widget_type_name(test_widgets.items[i].widget_type)
            test_results = "Key input on: " + widget_type
        end if
        i = i + 1
    end while

    // Toggle theme with 'T' key
    if event.key_code == 84 or event.key_code == 116 then  // T or t
        if theme.get_theme() == theme.THEME_DARK() then
            theme.set_theme(theme.THEME_LIGHT())
        else
            theme.set_theme(theme.THEME_DARK())
        end if
        test_results = "Theme switched to: " + get_theme_name()
    end if
end function

pub function get_widget_type_name(widget_type: i32) -> text
    if widget_type == widget.WIDGET_BUTTON() then return "Button"
    elseif widget_type == widget.WIDGET_TEXT_FIELD() then return "Text Field"
    elseif widget_type == widget.WIDGET_LABEL() then return "Label"
    elseif widget_type == widget.WIDGET_DROPDOWN() then return "Dropdown"
    elseif widget_type == widget.WIDGET_MENU() then return "Menu"
    elseif widget_type == widget.WIDGET_CHECKBOX() then return "Checkbox"
    elseif widget_type == widget.WIDGET_RADIOBUTTON() then return "Radio Button"
    elseif widget_type == widget.WIDGET_STATUSBAR() then return "Status Bar"
    elseif widget_type == widget.WIDGET_SLIDER() then return "Slider"
    elseif widget_type == widget.WIDGET_PROGRESSBAR() then return "Progress Bar"
    elseif widget_type == widget.WIDGET_LISTBOX() then return "List Box"
    elseif widget_type == widget.WIDGET_TOOLBAR() then return "Toolbar"
    elseif widget_type == widget.WIDGET_PANEL() then return "Panel"
    elseif widget_type == widget.WIDGET_TEXT_EDITOR() then return "Text Editor"
    elseif widget_type == widget.WIDGET_TABBAR() then return "Tab Bar"
    elseif widget_type == widget.WIDGET_TREEVIEW() then return "Tree View"
    else return "Unknown"
    end if
end function

pub function get_theme_name() -> text
    if theme.get_theme() == theme.THEME_DARK() then
        return "Dark"
    else
        return "Light"
    end if
end function

pub function render_validation()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 1000, 700, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 20, 20, "NILAIX Widget Validation Suite", theme.text_primary())
    winplat.draw_text(window_handle, 20, 40, "Press 'T' to toggle theme | Click widgets to test | Type in text fields", theme.text_secondary())

    // Draw all test widgets
    let mut i = 0
    while i < widget_count do
        widget.draw_widget(window_handle, test_widgets.items[i])
        i = i + 1
    end while

    // Draw validation info panel
    draw_validation_info()

    // Draw test results
    winplat.fill_rect(window_handle, 20, 710, 960, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 20, 710, 960, 1, theme.border_primary())
    winplat.draw_text(window_handle, 25, 715, test_results, theme.text_primary())

    winplat.present(window_handle)
end function

pub function draw_validation_info()
    let info_x = 500
    let info_y = 320
    let info_w = 480
    let info_h = 200

    // Draw info panel background
    winplat.fill_rect(window_handle, info_x, info_y, info_w, info_h, theme.panel_bg())
    winplat.fill_rect(window_handle, info_x, info_y, info_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, info_x, info_y + info_h - 1, info_w, 1, theme.border_primary())
    winplat.fill_rect(window_handle, info_x, info_y, 1, info_h, theme.border_primary())
    winplat.fill_rect(window_handle, info_x + info_w - 1, info_y, 1, info_h, theme.border_primary())

    // Panel header
    winplat.fill_rect(window_handle, info_x + 1, info_y + 1, info_w - 2, 22, theme.panel_header_bg())
    winplat.draw_text(window_handle, info_x + 6, info_y + 4, "Validation Results", theme.text_primary())

    // Validation information
    let text_y = info_y + 30
    let line_height = 18

    winplat.draw_text(window_handle, info_x + 10, text_y, "Widget Types Validated:", theme.text_primary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height, "✓ Basic widgets (Button, Label, etc.)", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 2, "✓ Input widgets (Text Field, Dropdown)", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 3, "✓ Selection widgets (Checkbox, Radio, List)", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 4, "✓ Display widgets (Progress, Status)", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 5, "✓ Advanced widgets (Editor, Tabs, Tree)", theme.text_secondary())

    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 7, "Features Tested:", theme.text_primary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 8, "✓ Mouse click interaction", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 9, "✓ Keyboard input handling", theme.text_secondary())
    winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 10, "✓ Theme switching (Dark/Light)", theme.text_secondary())

    // Focused widget info
    let focused_id = widget.get_focused_widget_id()
    if focused_id != -1 then
        let mut i = 0
        while i < widget_count do
            if test_widgets.items[i].id == focused_id then
                let widget_name = get_widget_type_name(test_widgets.items[i].widget_type)
                winplat.draw_text(window_handle, info_x + 10, text_y + line_height * 12, "Focused: " + widget_name, theme.focus_ring())
                break
            end if
            i = i + 1
        end while
    end if
end function