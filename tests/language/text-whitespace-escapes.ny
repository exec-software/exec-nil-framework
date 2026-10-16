# Raw U+000C/U+000B bytes below are independent oracles for Ny's escape syntax.
# Keep them literal: spelling both sides with escapes would hide decoder bugs.
import ny.lang.core
import ny.lang.text.string_utils

fn check(ok: bool, label: text)
    assert_true(ok, label)
end fn

fn main() -> i32
    let form_feed = ""
    let vertical_tab = ""
    check("\f" == form_feed, "form-feed escape matches byte 12")
    check("\v" == vertical_tab, "vertical-tab escape matches byte 11")
    check(form_feed.len() == 1 and vertical_tab.len() == 1, "single control bytes")
    check("\\f".len() == 2 and "\\v".len() == 2, "literal backslashes survive NIL")
    check("\\f" != form_feed and "\\v" != vertical_tab, "decode exactly once")

    check(not ny::lang::text::is_whitespace("f") and not ny::lang::text::is_whitespace("v"), "letters are not whitespace")
    check(not ny::lang::text::is_whitespace("five velvet"), "words are not whitespace")
    check(ny::lang::text::is_whitespace(form_feed) and ny::lang::text::is_whitespace(vertical_tab), "ASCII control whitespace")
    check(ny::lang::text::is_whitespace(" \t\n\r" + form_feed + vertical_tab), "all six ASCII whitespace bytes")
    check(not ny::lang::text::is_whitespace("") and ny::lang::text::is_blank(""), "empty predicates unchanged")
    check(ny::lang::text::is_blank(form_feed + vertical_tab) and not ny::lang::text::is_blank("fv"), "blank uses control bytes")
    check(ny::lang::text::trim("five velvet") == "five velvet", "trim preserves letters")
    check(ny::lang::text::trim(form_feed + "five velvet" + vertical_tab) == "five velvet", "trim control edges")
    check(ny::lang::text::trim_start(form_feed + "five" + vertical_tab) == "five" + vertical_tab, "trim start only")
    check(ny::lang::text::trim_end(form_feed + "five" + vertical_tab) == form_feed + "five", "trim end only")

    let ordinary = ny::lang::text::split_whitespace("five velvet")
    check(ny::lang::text::join(ordinary, "|") == "five|velvet", "split preserves f and v")
    let controls = ny::lang::text::split_whitespace(form_feed + "five" + form_feed + "velvet" + vertical_tab + "f" + vertical_tab)
    check(ny::lang::text::join(controls, "|") == "five|velvet|f", "split actual controls")
    let only_controls = ny::lang::text::split_whitespace(form_feed + vertical_tab)
    check(only_controls.length() == 0, "control-only split")
    println("text whitespace escapes passed")
    return 0
end fn
