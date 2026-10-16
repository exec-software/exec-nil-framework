module char_utils

import string_utils

@interop("runtime")
function _char_from_int(i: i32) -> text
end function

pub function to_char(keycode: i32) -> text
    // Assuming keycode is ASCII for now.
    // In a real scenario, this might need more robust handling for non-ASCII characters.
    if keycode >= 32 and keycode <= 126 then // Printable ASCII range
        return _char_from_int(keycode)
    end if
    return ""
end function
