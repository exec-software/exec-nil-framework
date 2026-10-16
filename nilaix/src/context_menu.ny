module context_menu
# Context menu system for right-click interactions
# Provides hierarchical menus with icons and shortcuts

import widget
import theme
import winplat
import string_utils

// Menu item types
pub function MENU_ITEM_NORMAL() -> i32
    return 0
end function

pub function MENU_ITEM_SEPARATOR() -> i32
    return 1
end function

pub function MENU_ITEM_SUBMENU() -> i32
    return 2
end function

pub function MENU_ITEM_CHECKABLE() -> i32
    return 3
end function

pub function MENU_ITEM_RADIO() -> i32
    return 4
end function

// Context menu item structure
pub struct menu_item
    text: text
    item_type: i32
    shortcut: text
    icon: text
    checked: bool
    enabled: bool
    action_id: i32
    submenu_items: array<menu_item>
    submenu_count: i32
end struct

// Context menu structure
pub struct context_menu_data
    visible: bool
    x: i32
    y: i32
    width: i32
    height: i32
    item_height: i32
    items: array<menu_item>
    item_count: i32
    selected_index: i32
    parent_menu: i32  // -1 for root menu
end struct

// Global context menu
let mut current_menu: context_menu_data

pub function initialize_context_menu()
    current_menu = context_menu_data {
        visible: false,
        x: 0,
        y: 0,
        width: 200,
        height: 0,
        item_height: 24,
        items: [],
        item_count: 0,
        selected_index: -1,
        parent_menu: -1
    }
end function

pub function create_menu_item(text: text, shortcut: text, icon: text, action_id: i32) -> menu_item
    let item = menu_item {
        text: text,
        item_type: MENU_ITEM_NORMAL(),
        shortcut: shortcut,
        icon: icon,
        checked: false,
        enabled: true,
        action_id: action_id,
        submenu_items: [],
        submenu_count: 0
    }
    return item
end function

pub function create_separator() -> menu_item
    let item = menu_item {
        text: "",
        item_type: MENU_ITEM_SEPARATOR(),
        shortcut: "",
        icon: "",
        checked: false,
        enabled: true,
        action_id: -1,
        submenu_items: [],
        submenu_count: 0
    }
    return item
end function

pub function create_checkable_item(text: text, shortcut: text, checked: bool, action_id: i32) -> menu_item
    let item = menu_item {
        text: text,
        item_type: MENU_ITEM_CHECKABLE(),
        shortcut: shortcut,
        icon: "",
        checked: checked,
        enabled: true,
        action_id: action_id,
        submenu_items: [],
        submenu_count: 0
    }
    return item
end function

pub function create_submenu(text: text, icon: text, submenu_items: array<menu_item>) -> menu_item
    let item = menu_item {
        text: text,
        item_type: MENU_ITEM_SUBMENU(),
        shortcut: "",
        icon: icon,
        checked: false,
        enabled: true,
        action_id: -1,
        submenu_items: submenu_items,
        submenu_count: submenu_items.count
    }
    return item
end function

pub function show_context_menu(x: i32, y: i32, items: array<menu_item>)
    current_menu.x = x
    current_menu.y = y
    current_menu.items = items
    current_menu.item_count = items.count
    current_menu.selected_index = -1
    current_menu.visible = true
    current_menu.parent_menu = -1

    // Calculate menu dimensions
    let mut max_width = 180  // Minimum width
    let mut i = 0
    while i < items.count do
        let item = items.items[i]
        if item.item_type != MENU_ITEM_SEPARATOR() then
            let text_width = string_utils.length(item.text) * 8  // Approximate character width
            let shortcut_width = string_utils.length(item.shortcut) * 6
            let item_width = text_width + shortcut_width + 40  // Padding + icon space
            if item_width > max_width then
                max_width = item_width
            end if
        end if
        i = i + 1
    end while

    current_menu.width = max_width
    current_menu.height = items.count * current_menu.item_height

    // Ensure menu stays within screen bounds (assuming 1024x768 screen)
    if current_menu.x + current_menu.width > 1024 then
        current_menu.x = 1024 - current_menu.width
    end if
    if current_menu.y + current_menu.height > 768 then
        current_menu.y = 768 - current_menu.height
    end if
end function

pub function hide_context_menu()
    current_menu.visible = false
    current_menu.selected_index = -1
end function

pub function is_context_menu_visible() -> bool
    return current_menu.visible
end function

pub function hit_test_context_menu(x: i32, y: i32) -> bool
    if not current_menu.visible then
        return false
    end if

    return x >= current_menu.x and x <= current_menu.x + current_menu.width and
           y >= current_menu.y and y <= current_menu.y + current_menu.height
end function

pub function get_item_at_position(x: i32, y: i32) -> i32
    if not hit_test_context_menu(x, y) then
        return -1
    end if

    let relative_y = y - current_menu.y
    let item_index = relative_y / current_menu.item_height

    if item_index >= 0 and item_index < current_menu.item_count then
        return item_index
    end if

    return -1
end function

pub function select_next_item()
    if not current_menu.visible or current_menu.item_count == 0 then
        return
    end if

    let mut next_index = current_menu.selected_index + 1

    // Skip separators
    while next_index < current_menu.item_count do
        let item = current_menu.items.items[next_index]
        if item.item_type != MENU_ITEM_SEPARATOR() and item.enabled then
            current_menu.selected_index = next_index
            return
        end if
        next_index = next_index + 1
    end while

    // Wrap around to first enabled item
    next_index = 0
    while next_index < current_menu.item_count do
        let item = current_menu.items.items[next_index]
        if item.item_type != MENU_ITEM_SEPARATOR() and item.enabled then
            current_menu.selected_index = next_index
            return
        end if
        next_index = next_index + 1
    end while
end function

pub function select_previous_item()
    if not current_menu.visible or current_menu.item_count == 0 then
        return
    end if

    let mut prev_index = current_menu.selected_index - 1

    // Skip separators
    while prev_index >= 0 do
        let item = current_menu.items.items[prev_index]
        if item.item_type != MENU_ITEM_SEPARATOR() and item.enabled then
            current_menu.selected_index = prev_index
            return
        end if
        prev_index = prev_index - 1
    end while

    // Wrap around to last enabled item
    prev_index = current_menu.item_count - 1
    while prev_index >= 0 do
        let item = current_menu.items.items[prev_index]
        if item.item_type != MENU_ITEM_SEPARATOR() and item.enabled then
            current_menu.selected_index = prev_index
            return
        end if
        prev_index = prev_index - 1
    end while
end function

pub function get_selected_item() -> menu_item
    if current_menu.visible and current_menu.selected_index >= 0 and current_menu.selected_index < current_menu.item_count then
        return current_menu.items.items[current_menu.selected_index]
    end if

    return menu_item { text: "", item_type: 0, shortcut: "", icon: "", checked: false, enabled: false, action_id: -1, submenu_items: [], submenu_count: 0 }
end function

pub function execute_selected_item() -> i32
    let selected = get_selected_item()
    if selected.action_id != -1 then
        hide_context_menu()
        return selected.action_id
    end if
    return -1
end function

pub function draw_context_menu(handle: i32)
    if not current_menu.visible then
        return
    end if

    // Draw menu background
    winplat.fill_rect(handle, current_menu.x, current_menu.y, current_menu.width, current_menu.height, theme.popup_bg())

    // Draw border
    winplat.fill_rect(handle, current_menu.x, current_menu.y, current_menu.width, 1, theme.border_primary())
    winplat.fill_rect(handle, current_menu.x, current_menu.y + current_menu.height - 1, current_menu.width, 1, theme.border_primary())
    winplat.fill_rect(handle, current_menu.x, current_menu.y, 1, current_menu.height, theme.border_primary())
    winplat.fill_rect(handle, current_menu.x + current_menu.width - 1, current_menu.y, 1, current_menu.height, theme.border_primary())

    // Draw menu items
    let mut i = 0
    while i < current_menu.item_count do
        let item = current_menu.items.items[i]
        let item_y = current_menu.y + (i * current_menu.item_height)

        // Highlight selected item
        if i == current_menu.selected_index then
            winplat.fill_rect(handle, current_menu.x + 1, item_y, current_menu.width - 2, current_menu.item_height, theme.listbox_selected_bg())
        end if

        // Draw item based on type
        if item.item_type == MENU_ITEM_SEPARATOR() then
            // Draw separator line
            let separator_y = item_y + (current_menu.item_height / 2)
            winplat.fill_rect(handle, current_menu.x + 4, separator_y, current_menu.width - 8, 1, theme.border_primary())
        else
            let mut text_color = theme.text_primary()
            if not item.enabled then
                text_color = theme.disabled_text()
            end if

            // Draw icon if present
            let mut icon_x = current_menu.x + 6
            if string_utils.length(item.icon) > 0 then
                winplat.draw_text(handle, icon_x, item_y + 4, item.icon, text_color)
                icon_x = icon_x + 16
            end if

            // Draw checkbox or radio button
            if item.item_type == MENU_ITEM_CHECKABLE() then
                if item.checked then
                    winplat.draw_text(handle, icon_x, item_y + 4, "✓", text_color)
                else
                    winplat.fill_rect(handle, icon_x, item_y + 6, 10, 10, theme.textfield_bg())
                    winplat.fill_rect(handle, icon_x, item_y + 6, 10, 1, theme.border_primary())
                    winplat.fill_rect(handle, icon_x, item_y + 15, 10, 1, theme.border_primary())
                    winplat.fill_rect(handle, icon_x, item_y + 6, 1, 10, theme.border_primary())
                    winplat.fill_rect(handle, icon_x + 9, item_y + 6, 1, 10, theme.border_primary())
                end if
                icon_x = icon_x + 16
            elseif item.item_type == MENU_ITEM_RADIO() then
                if item.checked then
                    winplat.draw_text(handle, icon_x, item_y + 4, "●", text_color)
                else
                    winplat.draw_text(handle, icon_x, item_y + 4, "○", text_color)
                end if
                icon_x = icon_x + 16
            end if

            // Draw text
            let text_x = icon_x + 4
            winplat.draw_text(handle, text_x, item_y + 4, item.text, text_color)

            // Draw shortcut if present
            if string_utils.length(item.shortcut) > 0 then
                let shortcut_x = current_menu.x + current_menu.width - string_utils.length(item.shortcut) * 6 - 10
                winplat.draw_text(handle, shortcut_x, item_y + 4, item.shortcut, theme.text_secondary())
            end if

            // Draw submenu arrow
            if item.item_type == MENU_ITEM_SUBMENU() then
                let arrow_x = current_menu.x + current_menu.width - 20
                winplat.draw_text(handle, arrow_x, item_y + 4, "►", text_color)
            end if
        end if

        i = i + 1
    end while
end function

// Utility functions for common context menus
pub function create_file_context_menu() -> array<menu_item>
    let mut items: array<menu_item> = []

    items = items + [create_menu_item("New", "Ctrl+N", "📄", 1001)]
    items = items + [create_menu_item("Open", "Ctrl+O", "📁", 1002)]
    items = items + [create_menu_item("Save", "Ctrl+S", "💾", 1003)]
    items = items + [create_menu_item("Save As...", "Ctrl+Shift+S", "💾", 1004)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Cut", "Ctrl+X", "✂", 1005)]
    items = items + [create_menu_item("Copy", "Ctrl+C", "📋", 1006)]
    items = items + [create_menu_item("Paste", "Ctrl+V", "📋", 1007)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Delete", "Del", "🗑", 1008)]
    items = items + [create_menu_item("Rename", "F2", "✏", 1009)]
    items = items + [create_separator()]
    items = items + [create_checkable_item("Show Hidden Files", "", false, 1010)]
    items = items + [create_menu_item("Properties", "Alt+Enter", "ℹ", 1011)]

    return items
end function

pub function create_editor_context_menu() -> array<menu_item>
    let mut items: array<menu_item> = []

    items = items + [create_menu_item("Undo", "Ctrl+Z", "↶", 2001)]
    items = items + [create_menu_item("Redo", "Ctrl+Y", "↷", 2002)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Cut", "Ctrl+X", "✂", 2003)]
    items = items + [create_menu_item("Copy", "Ctrl+C", "📋", 2004)]
    items = items + [create_menu_item("Paste", "Ctrl+V", "📋", 2005)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Select All", "Ctrl+A", "⬚", 2006)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Find...", "Ctrl+F", "🔍", 2007)]
    items = items + [create_menu_item("Replace...", "Ctrl+H", "🔄", 2008)]
    items = items + [create_separator()]
    items = items + [create_checkable_item("Word Wrap", "", false, 2009)]
    items = items + [create_checkable_item("Line Numbers", "", true, 2010)]
    items = items + [create_checkable_item("Syntax Highlighting", "", true, 2011)]

    return items
end function

pub function create_tree_context_menu() -> array<menu_item>
    let mut items: array<menu_item> = []

    items = items + [create_menu_item("Open", "Enter", "📂", 3001)]
    items = items + [create_menu_item("Open in New Tab", "", "📑", 3002)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Cut", "Ctrl+X", "✂", 3003)]
    items = items + [create_menu_item("Copy", "Ctrl+C", "📋", 3004)]
    items = items + [create_menu_item("Paste", "Ctrl+V", "📋", 3005)]
    items = items + [create_separator()]
    items = items + [create_menu_item("Rename", "F2", "✏", 3006)]
    items = items + [create_menu_item("Delete", "Del", "🗑", 3007)]
    items = items + [create_separator()]

    // Create submenu for "New"
    let mut new_items: array<menu_item> = []
    new_items = new_items + [create_menu_item("Folder", "", "📁", 3010)]
    new_items = new_items + [create_menu_item("File", "", "📄", 3011)]
    new_items = new_items + [create_menu_item("Project", "", "📦", 3012)]
    items = items + [create_submenu("New", "🆕", new_items)]

    items = items + [create_separator()]
    items = items + [create_menu_item("Properties", "Alt+Enter", "ℹ", 3008)]
    items = items + [create_menu_item("Refresh", "F5", "🔄", 3009)]

    return items
end function