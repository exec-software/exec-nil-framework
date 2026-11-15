# Secure hex draws on every execution route, paired with the Matrix fixture.
# random_bytes is deliberately absent: it yields arbitrary bytes, which can
# include NUL, so it has no native provider and stays interpreter-only.
import nil.std.core
import nil.std.io
import nil.std.security
import nil.std.text

fn require(ok: bool, message: text)
    if not ok then panic(message) end if
end fn

fn hex_only(drawn: text) -> bool
    for index in range(drawn.len())
        if not nil::std::text::contains("0123456789abcdef", nil::std::text::char_at(drawn, index)) then
            return false
        end if
    end for
    return true
end fn

fn draw(count: i64) -> text
    match nil::std::security::random_hex(count)
        Result.Error(message) ->
            panic("random_hex rejected a valid count: " + message)
            return ""
        Result.Ok(value) -> return value
    end match
end fn

fn main() -> i32
    let identifier = draw(16)
    require(identifier.len() == 32, "16 bytes is 32 hex characters")
    require(hex_only(identifier), "draws are lower-case hex")
    require(draw(1).len() == 2, "one byte is two hex characters")
    require(draw(32).len() == 64, "32 bytes is 64 hex characters")
    # Two draws colliding would mean the source is not random at all.
    require(draw(16) != draw(16), "independent draws differ")
    # A count outside the documented range is an error, not a short draw.
    require(nil::std::security::random_hex(0).is_error(), "zero bytes is rejected")
    require(nil::std::security::random_hex(-1).is_error(), "a negative count is rejected")
    require(nil::std::security::random_hex(1025).is_error(), "an oversized draw is rejected")
    println("secure hex draws")
    return 0
end fn
