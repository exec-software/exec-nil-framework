module ny.tests.checked_hex_reject

import ny.lang.core.*
fn main() -> i32
    # The legacy convenience API must reject the same malformed input as the
    # checked form, including formerly accepted signed two-character pairs.
    println(Convert.hex_to_bytes("-1").length())
    return 0
end fn
