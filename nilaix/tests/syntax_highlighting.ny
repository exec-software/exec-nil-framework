module syntax_highlighting_test
# Test application for syntax highlighting in text editor
# Demonstrates NY language syntax highlighting with various code samples

import widget
import theme
import winplat
import string_utils
import syntax_highlighter

let mut window_handle: i32 = -1
let mut running: bool = true
let mut text_editor: widget::widget_data
let mut current_sample: i32 = 0
let mut status_text: text = "Syntax Highlighting Test - Press 1-5 for different code samples"

// Code samples for testing syntax highlighting
let mut code_samples: array<text> = []
let mut sample_names: array<text> = []

pub function main()
    window_handle = winplat.create_window("Syntax Highlighting Test", 900, 700)
    if window_handle == -1 then
        print("Failed to create window")
        return
    end if

    theme.set_theme(theme.THEME_DARK())

    initialize_code_samples()
    create_text_editor()

    while running do
        process_events()
        render_frame()
        winplat.wait_events()
    end while

    winplat.destroy_window(window_handle)
end function

pub function initialize_code_samples()
    // Sample 1: Basic NY syntax
    code_samples = code_samples + ["module hello_world\n# Basic NY program with syntax highlighting\nimport widget\nimport string_utils\n\nfunction main()\n    let message = \"Hello, NY Programming Language!\"\n    let count = 42\n    \n    if count > 0 then\n        print(message)\n        print(\"Count: \" + string_utils.int_to_text(count))\n    else\n        print(\"Count is zero or negative\")\n    end if\n    \n    return true\nend function\n\n# Call the main function\nlet result = main()\nif result then\n    print(\"Program completed successfully\")\nend if"]

    sample_names = sample_names + ["Basic NY Syntax"]

    // Sample 2: Function definitions and types
    code_samples = code_samples + ["module advanced_types\n# Advanced type definitions and functions\n\nstruct person\n    name: text\n    age: i32\n    active: bool\n    scores: array<f32>\nend struct\n\nstruct point\n    x: f64\n    y: f64\nend struct\n\nfunction create_person(name: text, age: i32) -> person\n    let p = person {\n        name: name,\n        age: age,\n        active: true,\n        scores: [0.0, 0.0, 0.0]\n    }\n    return p\nend function\n\nfunction distance(p1: point, p2: point) -> f64\n    let dx = p2.x - p1.x\n    let dy = p2.y - p1.y\n    return (dx * dx + dy * dy) ^ 0.5\nend function\n\nfunction main()\n    let person1 = create_person(\"Alice\", 25)\n    let origin = point { x: 0.0, y: 0.0 }\n    let target = point { x: 3.0, y: 4.0 }\n    \n    let dist = distance(origin, target)  // Should be 5.0\n    print(\"Distance: \" + string_utils.f64_to_text(dist))\n    \n    return true\nend function"]

    sample_names = sample_names + ["Types and Functions"]

    // Sample 3: Loops and control flow
    code_samples = code_samples + ["module control_flow\n# Loops and conditional statements\n\nfunction factorial(n: i32) -> i32\n    if n <= 1 then\n        return 1\n    else\n        return n * factorial(n - 1)\n    end if\nend function\n\nfunction process_numbers(items: array<i32>) -> i32\n    let mut total = 0\n    let mut count = 0\n    \n    while count < items.count do\n        let item = items.items[count]\n        \n        if item > 0 and item < 100 then\n            total = total + item\n        elseif item == 0 then\n            break  // Stop on zero\n        else\n            continue  // Skip negative numbers\n        end if\n        \n        count = count + 1\n    end while\n    \n    return total\nend function\n\nfunction main()\n    let numbers = [1, 2, 3, 4, 5, 0, -1, 6]\n    let sum = process_numbers(numbers)\n    let fact = factorial(5)\n    \n    print(\"Sum: \" + string_utils.int_to_text(sum))\n    print(\"5! = \" + string_utils.int_to_text(fact))\n    \n    return true\nend function"]

    sample_names = sample_names + ["Loops and Control Flow"]

    // Sample 4: Module system and directives
    code_samples = code_samples + ["@module module_system\n@export public\n\n@import \"widget.ny\"\n@import \"theme.ny\"\n@import \"string_utils.ny\"\n\n@pub struct config\n    width: i32\n    height: i32\n    title: text\n    theme_name: text\nend struct\n\n@function create_window_from_config(cfg: config) -> i32\n    if cfg.width <= 0 or cfg.height <= 0 then\n        print(\"Invalid window dimensions\")\n        return -1\n    end if\n    \n    @native create_window(cfg.title, cfg.width, cfg.height)\nend function\n\n@function apply_theme(theme_name: text) -> bool\n    if theme_name == \"dark\" then\n        theme.set_theme(theme.THEME_DARK())\n        return true\n    elseif theme_name == \"light\" then\n        theme.set_theme(theme.THEME_LIGHT())\n        return true\n    else\n        print(\"Unknown theme: \" + theme_name)\n        return false\n    end if\nend function\n\n@function main() -> i32\n    let cfg = config {\n        width: 800,\n        height: 600,\n        title: \"NY Application\",\n        theme_name: \"dark\"\n    }\n    \n    let window_id = create_window_from_config(cfg)\n    if window_id != -1 then\n        apply_theme(cfg.theme_name)\n    end if\n    \n    return window_id\nend function"]

    sample_names = sample_names + ["Module System"]

    // Sample 5: Advanced operations and built-ins
    code_samples = code_samples + ["module advanced_operations\n# String manipulation, arrays, and built-ins\n\nfunction analyze_text(text: text) -> text\n    if string_utils.length(text) == 0 then\n        return \"Empty text\"\n    end if\n    \n    let words = string_utils.split(text, \" \")\n    let mut word_count = words.count\n    let char_count = string_utils.length(text)\n    let mut has_numbers = false\n    \n    // Check for numbers in text\n    let mut i = 0\n    while i < char_count do\n        let char = string_utils.char_at(text, i)\n        if char >= \"0\" and char <= \"9\" then\n            has_numbers = true\n            break\n        end if\n        i = i + 1\n    end while\n    \n    let result = \"Words: \" + string_utils.int_to_text(word_count) + \n               \", Chars: \" + string_utils.int_to_text(char_count)\n    \n    if has_numbers then\n        result = result + \", Contains numbers: true\"\n    else\n        result = result + \", Contains numbers: false\"\n    end if\n    \n    return result\nend function\n\nfunction process_data(data: array<text>) -> array<i32>\n    let mut lengths: array<i32> = []\n    \n    for item in data do\n        let clean_item = string_utils.trim(item)\n        let len = string_utils.length(clean_item)\n        lengths = lengths + [len]\n    end for\n    \n    return lengths\nend function\n\nfunction main()\n    let sample_text = \"Hello NY123 World! This is a test.\"\n    let analysis = analyze_text(sample_text)\n    \n    print(\"Input: \" + sample_text)\n    print(\"Analysis: \" + analysis)\n    \n    let test_data = [\"  hello  \", \"world\", \"  NY\", \"  programming  \"]\n    let lengths = process_data(test_data)\n    \n    print(\"Lengths: [\" + string_utils.join(lengths, \", \") + \"]\")\n    \n    return true\nend function"]

    sample_names = sample_names + ["String Operations"]
end function

pub function create_text_editor()
    let initial_content = code_samples.items[0]
    text_editor = widget.create_text_editor(10, 80, 880, 580, initial_content, 100)
end function

pub function process_events()
    let mut event = winplat::event { event_type: 0, key_code: 0, mouse_x: 0, mouse_y: 0 }

    while winplat.poll_event(event) do
        if event.event_type == winplat::EVENT_QUIT() then
            running = false
        elseif event.event_type == winplat::EVENT_MOUSE_PRESS() then
            widget.check_widget_click(event.mouse_x, event.mouse_y, text_editor)
        elseif event.event_type == winplat::EVENT_KEY_PRESS() then
            handle_key_press(event)
        end if
    end while
end function

pub function handle_key_press(event: winplat::event)
    let focused_id = widget.get_focused_widget_id()

    if focused_id == text_editor.id then
        widget.process_key_event(event, text_editor)
    end if

    // Handle number keys for switching samples
    if event.key_code == 49 then  // 1
        switch_sample(0)
    elseif event.key_code == 50 then  // 2
        switch_sample(1)
    elseif event.key_code == 51 then  // 3
        switch_sample(2)
    elseif event.key_code == 52 then  // 4
        switch_sample(3)
    elseif event.key_code == 53 then  // 5
        switch_sample(4)
    elseif event.key_code == 84 or event.key_code == 116 then  // T or t
        // Toggle theme
        if theme.get_theme() == theme.THEME_DARK() then
            theme.set_theme(theme.THEME_LIGHT())
            status_text = "Switched to Light theme"
        else
            theme.set_theme(theme.THEME_DARK())
            status_text = "Switched to Dark theme"
        end if
    end if
end function

pub function switch_sample(index: i32)
    if index >= 0 and index < code_samples.count then
        current_sample = index
        text_editor.text = code_samples.items[index]
        text_editor.cursor_pos = 0
        text_editor.selected_index = 0
        status_text = "Sample: " + sample_names.items[index] + " | Press 1-5 to switch samples"
    end if
end function

pub function render_frame()
    // Clear background
    winplat.fill_rect(window_handle, 0, 0, 900, 700, theme.bg_primary())

    // Draw title
    winplat.draw_text(window_handle, 10, 10, "NY Syntax Highlighting Test", theme.text_primary())
    winplat.draw_text(window_handle, 10, 30, "Samples: 1=Basic, 2=Types, 3=Loops, 4=Modules, 5=Strings | T=Toggle theme", theme.text_secondary())

    // Draw current sample name
    let sample_text = "Current: " + sample_names.items[current_sample]
    winplat.draw_text(window_handle, 10, 50, sample_text, theme.focus_ring())

    // Draw syntax highlighting legend
    draw_syntax_legend()

    // Draw text editor
    widget.draw_widget(window_handle, text_editor)

    // Draw status bar
    winplat.fill_rect(window_handle, 0, 670, 900, 30, theme.bg_status_bar())
    winplat.fill_rect(window_handle, 0, 670, 900, 1, theme.border_primary())
    winplat.draw_text(window_handle, 10, 675, status_text, theme.text_primary())

    winplat.present(window_handle)
end function

pub function draw_syntax_legend()
    let legend_x = 10
    let legend_y = 680

    winplat.draw_text(window_handle, legend_x, legend_y, "Syntax: ", theme.text_secondary())
    winplat.draw_text(window_handle, legend_x + 60, legend_y, "Keywords", theme.syntax_keyword())
    winplat.draw_text(window_handle, legend_x + 140, legend_y, "Strings", theme.syntax_string())
    winplat.draw_text(window_handle, legend_x + 200, legend_y, "Comments", theme.syntax_comment())
    winplat.draw_text(window_handle, legend_x + 280, legend_y, "Numbers", theme.syntax_number())
    winplat.draw_text(window_handle, legend_x + 350, legend_y, "Operators", theme.syntax_operator())
    winplat.draw_text(window_handle, legend_x + 440, legend_y, "Functions", theme.syntax_function())
    winplat.draw_text(window_handle, legend_x + 520, legend_y, "Types", theme.syntax_type())
    winplat.draw_text(window_handle, legend_x + 580, legend_y, "Built-ins", theme.syntax_builtin())
    winplat.draw_text(window_handle, legend_x + 650, legend_y, "Directives", theme.syntax_directive())
end function