# Text comparison is lexicographic over unsigned bytes, with no locale or
# case folding. This table independently specifies UTF-8/control/prefix order.
import ny.lang.collections

fn require(ok: bool, label: text)
    if not ok then panic(label) end if
end fn

fn check(left: text, right: text, relation: i32)
    require((left == right) == (relation == 0), "text equality")
    require((left != right) == (relation != 0), "text inequality")
    require((left < right) == (relation < 0), "text less")
    require((left <= right) == (relation <= 0), "text less or equal")
    require((left > right) == (relation > 0), "text greater")
    require((left >= right) == (relation >= 0), "text greater or equal")
end fn

fn join(left: text, right: text) -> text
    return left + right
end fn

fn clone_text(value: text) -> text
    return join(value, "")
end fn

fn main() -> i32
    require("a" < "b" and "a" <= "a", "literal less")
    require("z" > "a" and "z" >= "z", "literal greater")
    require("a" == "a" and "a" != "b", "literal equality")
    let ordered: array<text> = ["", "\t", "\n", "\r", " ", "A", "AA", "Aa", "B", "\\", "a", "a\t", "a\n", "aa", "z", "é", "ÿ", "Ω", "中", "😀"]
    for left in range(ordered.len())
        for right in range(ordered.len())
            let relation = left - right
            check(ordered[left], ordered[right], relation)
            check(clone_text(ordered[left]), clone_text(ordered[right]), relation)
            # Compare directly after GET_INDEX and CALL, outside check's
            # parameter ABI, so provenance is exercised at every producer.
            require((ordered[left] < ordered[right]) == (left < right), "indexed less")
            require((ordered[left] >= ordered[right]) == (left >= right), "indexed greater or equal")
            require((clone_text(ordered[left]) <= clone_text(ordered[right])) == (left <= right), "returned less or equal")
            require((clone_text(ordered[left]) > clone_text(ordered[right])) == (left > right), "returned greater")
        end for
    end for
    println("text byte order across literals parameters results and arrays")

    let first = join("separately", " allocated")
    let second = join("separately ", "allocated")
    check(first, second, 0)
    check(join(first, "!"), second, 1)
    check(first, join(second, "!"), -1)
    check(join("a", "z"), join("b", "a"), -1)
    check(join("", ""), "", 0)
    println("text content equality and prefix order across allocations")

    let empty: array<text> = []
    let filled = new_with_value<text>(2, "")
    check(empty[0], "", 0)
    check(empty[-1], "a", -1)
    check("a", empty[100], 1)
    check(filled[0], "", 0)
    check(filled[1], "a", -1)
    check(get<text>(filled, -1), "", 0)
    check("a", get<text>(filled, 2), 1)
    check(get<text>(empty, 0), empty[0], 0)
    println("text array default reads compare as empty strings")
    return 0
end fn
