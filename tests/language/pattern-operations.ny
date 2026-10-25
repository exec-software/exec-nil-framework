module validation.pattern_operations
import nil.std.core
import nil.std.text
import nil.std.collections

fn require(ok: bool, label: text)
    assert_true(ok, label)
end fn

fn same(actual: array<text>, expected: array<text>, label: text)
    require(actual.len() == expected.len(), label)
    for index in range(actual.len())
        require(actual[index] == expected[index], label)
    end for
end fn

fn check_case(source: text, input: text, matches: array<text>, fields: array<text>, replacement: text, replaced: text, label: text)
    let compiled = compile_pattern(source).unwrap()
    same(find_all_matches(compiled, input).unwrap(), matches, label + ": matches")
    same(split_matches(compiled, input).unwrap(), fields, label + ": split")
    require(replace_matches(compiled, input, replacement).unwrap() == replaced, label + ": replacement")
end fn

fn main() -> i32
    check_case("([0-9]+)", "A12B003C", ["12", "003"], ["A", "B", "C"], "#", "A#B#C", "captures are whole matches")
    check_case("([a-z]+)([0-9]+)?", "a1 b", ["a1", "b"], ["", " ", ""], "#", "# #", "absent optional captures do not change results")
    check_case("(a)|(b)", "ab", ["a", "b"], ["", "", ""], "#", "##", "alternative captures are excluded from split")
    check_case("(,)", ",a,,b,", [",", ",", ",", ","], ["", "a", "", "b", ""], "#", "#a##b#", "capturing delimiters are excluded from split")
    check_case("(?:ab)+", "xababzab", ["abab", "ab"], ["x", "z", ""], "#", "x#z#", "noncapture groups preserve whole matches")
    check_case("aa", "aaa", ["aa"], ["", "a"], "-", "-a", "search is nonoverlapping")
    check_case("a", "aaa", ["a", "a", "a"], ["", "", "", ""], "-", "---", "adjacent nonempty matches are all retained")
    check_case("x", "abc", [], ["abc"], "-", "abc", "no match returns the original input")
    check_case("x", "", [], [""], "-", "", "no match on empty input retains an empty field")
    check_case("", "ab", ["", "", ""], ["", "a", "b", ""], "-", "-a-b-", "empty pattern advances search without losing bytes")
    check_case("", "", [""], ["", ""], "-", "-", "empty pattern visits empty input once")
    check_case("a*", "a", ["a", ""], ["", "", ""], "-", "--", "greedy terminal empty match is retained")
    check_case("a*", "baac", ["", "aa", "", ""], ["", "b", "", "c", ""], "-", "-b--c-", "empty and nonempty matches share output cursor")
    check_case("a*?", "aa", ["", "", ""], ["", "a", "a", ""], "-", "-a-a-", "lazy empty matches progress one byte")
    check_case("a+", "caaaddaa", ["aaa", "aa"], ["c", "dd", ""], "-", "c-dd-", "nonempty match preserves trailing field")
    check_case("^", "ab", [""], ["", "ab"], "-", "-ab", "start anchor preserves all input")
    check_case("$", "ab", [""], ["ab", ""], "-", "ab-", "end anchor preserves all input")
    check_case("^|$", "ab", ["", ""], ["", "ab", ""], "-", "-ab-", "both endpoint anchors retain empties")
    check_case("a|$", "a", ["a", ""], ["", "", ""], "-", "--", "terminal alternative follows nonempty match")
    check_case("(?=b)", "ab", [""], ["a", "b"], "-", "a-b", "lookahead leaves matched position in output")
    check_case("(?=(b))", "ab", [""], ["a", "b"], "-", "a-b", "lookahead captures are not whole matches")
    check_case("b(?!c)", "bcb", ["b"], ["bc", ""], "-", "bc-", "negative lookahead filters nonempty matches")
    check_case("(?<word>cat|dog)", "cat dogs cat", ["cat", "dog", "cat"], ["", " ", "s ", ""], "#", "# #s #", "named captures preserve complete alternatives")
    check_case("é", "AééB", ["é", "é"], ["A", "", "B"], "-", "A--B", "literal multibyte pattern preserves adjacent matches")
    check_case("([0-9]+)", "A12B", ["12"], ["A", "B"], $"$1\\{{name}}", $"A$1\\{{name}}B", "replacement dollar slash and braces are literal")
    check_case("x", "xx", ["x", "x"], ["", "", ""], "$& $$", "$& $$$& $$", "replacement does not expand regex template tokens")
    check_case("a+", "cabd", ["a"], ["c", "bd"], "", "cbd", "empty replacement removes nonempty matches")
    check_case("", "ab", ["", "", ""], ["", "a", "b", ""], "", "ab", "empty replacement on empty matches preserves input")

    # Byte progress is observable without printing malformed UTF-8 fragments.
    let empty = compile_pattern("").unwrap()
    same(find_all_matches(empty, "é").unwrap(), ["", "", ""], "UTF-8 empty search advances by byte")
    let pieces = split_matches(empty, "é").unwrap()
    require(pieces.len() == 4, "UTF-8 split has byte fields and endpoint empties")
    require(pieces[0] == "" and pieces[3] == "", "UTF-8 split retains endpoint empties")
    require(pieces[1].len() == 1 and pieces[2].len() == 1, "UTF-8 split retains individual bytes")
    require(pieces[1] + pieces[2] == "é", "UTF-8 split bytes reconstruct the input")
    require(replace_matches(empty, "é", "").unwrap() == "é", "empty replacement preserves UTF-8 bytes")

    let mut invalid = compile_pattern("x").unwrap()
    invalid.source = "["
    require(find_all_matches(invalid, "abc").is_error(), "invalid public Pattern fails find-all")
    require(split_matches(invalid, "abc").is_error(), "invalid public Pattern fails split")
    require(replace_matches(invalid, "abc", "-").is_error(), "invalid public Pattern fails replacement")

    let words = compile_pattern("[A-Za-z]+").unwrap()
    let mut input = "a bear cat ox dog ant"
    let matches = find_all_matches(words, input).unwrap()
    let fields = split_matches(words, input).unwrap()
    let replaced = replace_matches(words, input, "#").unwrap()
    input = "changed"
    same(matches, ["a", "bear", "cat", "ox", "dog", "ant"], "retained matches survive source reassignment")
    same(fields, ["", " ", " ", " ", " ", " ", ""], "retained split survives source reassignment")
    require(replaced == "# # # # # #", "retained replacement survives source reassignment")
    let ranked = collect query
        from word in matches
        where word.len() > 2
        order by word.len() descending
        select word
    end query
    same(ranked, ["bear", "cat", "dog", "ant"], "found matches compose with stable filtered queries")
    require(query_count(ranked) == 4, "query terminal counts found matches")
    require(query_first(ranked).unwrap() == "bear", "query terminal retains found text")
    let page = collect query
        from word in matches
        where word.len() > 2
        order by word.len() descending
        skip 1
        take 2
        select word
    end query
    same(page, ["cat", "dog"], "found matches compose with ordered query pagination")
    println("pattern operations PASS")
    return 0
end fn
