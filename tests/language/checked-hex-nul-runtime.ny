module ny.tests.checked_hex_nul_runtime

import ny.lang.core.*

fn main() -> i32
    # Native text literals currently reject embedded NULs before execution.
    # Exercise the decoder's byte-exact validation on interpreter routes.
    let invalid: array<text> = ["\0f", "f\0", "\0\0", "00\0f", "00ff\0\0"]
    for malformed in invalid
        match Convert.try_hex_to_bytes(malformed)
            Result.Ok(_) -> return 10
            Result.Error(message) ->
                if message != "hex_to_bytes: input contains a non-hexadecimal digit" then return 11 end if
        end match
    end for
    println("embedded-nul-rejected")
    return 0
end fn
