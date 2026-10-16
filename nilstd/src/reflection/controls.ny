# Ny spells control bytes with escapes; Matrix's literals intentionally remain
# raw. Keep these private constants in the same logical reflection module.
module nil.std.reflection

fn json_carriage_return() -> text
    return "\r"
end fn

fn json_form_feed() -> text
    return "\f"
end fn

fn json_vertical_tab() -> text
    return "\v"
end fn
