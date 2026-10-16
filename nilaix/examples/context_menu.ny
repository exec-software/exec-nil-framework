# Standalone Context Menu Demo
# Minimal implementation without complex widget dependencies

import winplat
import theme

# Simple context menu structure
let mut context_menu_visible: bool = false
let mut context_menu_x: i32 = 0
let mut context_menu_y: i32 = 0
let mut context_menu_items: array<text> = [""]
let mut context_menu_actions: array<i32> = [0]
let mut context_menu_selected: i32 = -1

function show_context_menu(x: i32, y: i32, items: array<text>, actions: array<i32])
    context_menu_visible = true
    context_menu_x = x
    context_menu_y = y
    context_menu_items = items
    context_menu_actions = actions
    context_menu_selected = -1
end function

function hide_context_menu()
    context_menu_visible = false
    context_menu_selected = -1
end function

function draw_context_menu(dc: i32)
    if not context_menu_visible then return end if

    let item_height = 25
    let padding = 8
    let mut max_width = 120

    # Calculate maximum width needed
    let mut i = 0
    while i < 8 do  # Assuming max 8 items
        if i < array.length(context_menu_items) then
            let text_len = 0
            let mut j = 0
            while j < 20 do  # Max string length check
                if j < string.length(context_menu_items[i]) then
                    text_len = text_len + 1
                end if
                j = j + 1
            end while
            let text_width = text_len * 8 + padding * 2
            if text_width > max_width then max_width = text_width end if
        end if
        i = i + 1
    end while

    let menu_height = array.length(context_menu_items) * item_height + 4

    # Ensure menu stays on screen
    let menu_x = context_menu_x
    let menu_y = context_menu_y

    # Draw background
    winplat.fill_rect(dc, menu_x, menu_y, max_width, menu_height, theme.menu_background())

    # Draw border
    winplat.draw_rect(dc, menu_x, menu_y, max_width, menu_height, theme.menu_border())

    # Draw items
    let mut i = 0
    while i < array.length(context_menu_items) do
        let item_y = menu_y + 2 + i * item_height

        # Highlight selected item
        if i == context_menu_selected then
            winplat.fill_rect(dc, menu_x + 2, item_y, max_width - 4, item_height - 2, theme.menu_selected())
        end if

        # Draw text
        winplat.draw_text(dc, menu_x + padding, item_y + 5, context_menu_items[i], theme.menu_text())

        i = i + 1
    end while
end function

function handle_context_menu_click(x: i32, y: i32) -> i32
    if not context_menu_visible then return -1 end if

    let item_height = 25
    let menu_x = context_menu_x
    let menu_y = context_menu_y
    let max_width = 120

    # Check if click is within menu bounds
    if x >= menu_x and x < menu_x + max_width and y >= menu_y and y < menu_y + array.length(context_menu_items) * item_height + 4 then
        let clicked_item = (y - menu_y - 2) / item_height
        if clicked_item >= 0 and clicked_item < array.length(context_menu_items) then
            let action = context_menu_actions[clicked_item]
            hide_context_menu()
            return action
        end if
    else
        hide_context_menu()
    end if

    return -1
end function

function main()
    let window = winplat.create_window("Standalone Context Menu Demo", 800, 600)

    let mut running = true

    while running do
        # Process events
        let event = winplat.get_next_event()
        if event.event_type == winplat.EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat.EVENT_KEY_PRESS() then
            if event.key_code == 27 then  # Escape key
                running = false
            else
                hide_context_menu()  # Hide on any key press
            end if
        elseif event.event_type == winplat.EVENT_MOUSE_CLICK() then
            # Handle context menu (Ctrl+Click for right-click)
            if event.modifiers and winplat.MODIFIER_CTRL() != 0 then
                # Show context menu at mouse position
                let items = ["Cut", "Copy", "Paste", "", "Select All", "", "Undo", "Redo"]
                let actions = [1, 2, 3, 0, 4, 0, 5, 6]
                show_context_menu(event.mouse_x, event.mouse_y, items, actions)
            else
                # Handle context menu clicks
                let context_action = handle_context_menu_click(event.mouse_x, event.mouse_y)
                if context_action > 0 then
                    # Context menu action selected
                    if context_action == 1 then print("Cut selected")
                    elseif context_action == 2 then print("Copy selected")
                    elseif context_action == 3 then print("Paste selected")
                    elseif context_action == 4 then print("Select All selected")
                    elseif context_action == 5 then print("Undo selected")
                    elseif context_action == 6 then print("Redo selected")
                    end if
                end if
            end if
        end if

        # Draw everything
        let dc = winplat.begin_draw(window)
        winplat.clear_window(dc, theme.background())

        # Draw instructions
        winplat.draw_text(dc, 50, 50, "Context Menu Demo", theme.text())
        winplat.draw_text(dc, 50, 80, "Ctrl+Click anywhere to show context menu", theme.text())
        winplat.draw_text(dc, 50, 110, "Press any key to hide menu", theme.text())
        winplat.draw_text(dc, 50, 140, "Press Escape to quit", theme.text())

        # Draw context menu if visible
        draw_context_menu(dc)

        winplat.end_draw(dc)
    end while

    winplat.destroy_window(window)
    return 0
end function

let result = main()