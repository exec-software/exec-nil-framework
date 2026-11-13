@schema
@pragma once

module nil.std.markup

import nil.std.core
import nil.std.text

# =============================================================================
# nil.std.markup - escaping and rendering for markup literals (NIL-0027)
# =============================================================================
# A 'begin markup' block lowers to these functions. They are ordinary public
# functions so that a document can also be assembled by hand, and so that the
# escaping a generated page relies on is readable, testable source rather than
# a hidden compiler behaviour.
#
# Escaping is per CONTEXT. The correct escape for a value differs between a
# text node, an attribute value and a URL attribute, which is why a markup
# block decides the context at compile time and calls the matching function
# here. Concatenating markup by hand applies none of them; that is the bug
# class this module exists to remove.

# A serialized document. The field is deliberately named 'content' and is
# already escaped: a value of this type is markup, not text that might still
# contain an unescaped '<'. Compose documents by interpolating one into
# another, never by concatenating their contents.
pub struct document
    content: text
end struct

# The serialized form. This is the only supported way out of a document.
pub fn render(doc: document) -> text
    return doc.content
end fn

# Adopt already-trusted markup. The name is the warning: everything that
# reaches a page through this function bypasses every escape below, so the
# caller owns the proof that it is safe.
pub fn raw(content: text) -> document
    return document { content: content }
end fn

# An empty document, for a conditional arm that contributes nothing.
pub fn empty() -> document
    return document { content: "" }
end fn

# Text-node context: a value may not open an element or an entity.
pub fn escape_text(content: text) -> text
    let ampersands = nil.std.text::replace(content, "&", "&amp;")
    let opening = nil.std.text::replace(ampersands, "<", "&lt;")
    return nil.std.text::replace(opening, ">", "&gt;")
end fn

# Attribute context: a rendered attribute is always double quoted, so the
# quote is escaped as well. Single quotes are escaped too, so the same
# function stays correct if a caller hand-writes single-quoted markup.
pub fn escape_attribute(content: text) -> text
    let ampersands = nil.std.text::replace(content, "&", "&amp;")
    let opening = nil.std.text::replace(ampersands, "<", "&lt;")
    let closing = nil.std.text::replace(opening, ">", "&gt;")
    let quotes = nil.std.text::replace(closing, "\"", "&quot;")
    return nil.std.text::replace(quotes, "'", "&#39;")
end fn

# ASCII lowercase for a scheme. nil.std.text::to_lower reaches '_char_code',
# which has no native ABI, and a markup page must render on both routes - so
# this folds with replace, which does. The input is a scheme, never a document.
fn fold_scheme(scheme: text) -> text
    let mut folded = scheme
    folded = nil.std.text::replace(folded, "A", "a")
    folded = nil.std.text::replace(folded, "B", "b")
    folded = nil.std.text::replace(folded, "C", "c")
    folded = nil.std.text::replace(folded, "D", "d")
    folded = nil.std.text::replace(folded, "E", "e")
    folded = nil.std.text::replace(folded, "F", "f")
    folded = nil.std.text::replace(folded, "G", "g")
    folded = nil.std.text::replace(folded, "H", "h")
    folded = nil.std.text::replace(folded, "I", "i")
    folded = nil.std.text::replace(folded, "J", "j")
    folded = nil.std.text::replace(folded, "K", "k")
    folded = nil.std.text::replace(folded, "L", "l")
    folded = nil.std.text::replace(folded, "M", "m")
    folded = nil.std.text::replace(folded, "N", "n")
    folded = nil.std.text::replace(folded, "O", "o")
    folded = nil.std.text::replace(folded, "P", "p")
    folded = nil.std.text::replace(folded, "Q", "q")
    folded = nil.std.text::replace(folded, "R", "r")
    folded = nil.std.text::replace(folded, "S", "s")
    folded = nil.std.text::replace(folded, "T", "t")
    folded = nil.std.text::replace(folded, "U", "u")
    folded = nil.std.text::replace(folded, "V", "v")
    folded = nil.std.text::replace(folded, "W", "w")
    folded = nil.std.text::replace(folded, "X", "x")
    folded = nil.std.text::replace(folded, "Y", "y")
    folded = nil.std.text::replace(folded, "Z", "z")
    return folded
end fn

# True when a URL's scheme can execute. Compared after folding case and after
# removing the leading whitespace a browser ignores.
fn executable_scheme(content: text) -> bool
    # The URL parser removes these controls anywhere in a URL, not just at
    # the ends. Decide on those same bytes while preserving the original
    # rendered value (apart from an unsafe scheme's colon).
    let tabs = nil.std.text::replace(content, "\t", "")
    let lines = nil.std.text::replace(tabs, "\n", "")
    let normalized = nil.std.text::replace(lines, "\r", "")
    let mut start = 0
    while start < normalized.len() do
        if normalized.substr(start, 1) > " " then break end if
        start = start + 1
    end while
    let trimmed = fold_scheme(normalized.substr(start, normalized.len() - start))
    let colon = nil.std.text::index_of(trimmed, ":")
    if colon < 0 then return false end if
    let slash = nil.std.text::index_of(trimmed, "/")
    # A colon after the first path separator belongs to the path, not a scheme.
    if slash >= 0 && slash < colon then return false end if
    let scheme = trimmed.substr(0, colon as i32)
    return scheme == "javascript" || scheme == "vbscript" || scheme == "data"
end fn

# A whole URL-bearing attribute value. An executable scheme is neutralised by
# percent-encoding its colon rather than by dropping the attribute or by
# faulting: the result is an inert relative reference, the page still renders,
# and the original text remains visible to anyone reading the output.
pub fn escape_url(content: text) -> text
    if executable_scheme(content) then
        return escape_attribute(nil.std.text::replace(content, ":", "%3a"))
    end if
    return escape_attribute(content)
end fn

# One component INSIDE a URL, such as a query value. Reserved and unsafe ASCII
# is percent-encoded; other bytes, including UTF-8, are passed through for the
# browser to encode. '%' is encoded first so nothing is encoded twice.
pub fn escape_url_component(content: text) -> text
    let mut result = nil.std.text::replace(content, "%", "%25")
    result = nil.std.text::replace(result, "\t", "%09")
    result = nil.std.text::replace(result, "\n", "%0a")
    result = nil.std.text::replace(result, "\r", "%0d")
    result = nil.std.text::replace(result, "\v", "%0b")
    result = nil.std.text::replace(result, "\f", "%0c")
    result = nil.std.text::replace(result, " ", "%20")
    result = nil.std.text::replace(result, "\"", "%22")
    result = nil.std.text::replace(result, "#", "%23")
    result = nil.std.text::replace(result, "&", "%26")
    result = nil.std.text::replace(result, "'", "%27")
    result = nil.std.text::replace(result, "+", "%2b")
    result = nil.std.text::replace(result, ",", "%2c")
    result = nil.std.text::replace(result, "/", "%2f")
    result = nil.std.text::replace(result, ":", "%3a")
    result = nil.std.text::replace(result, ";", "%3b")
    result = nil.std.text::replace(result, "<", "%3c")
    result = nil.std.text::replace(result, "=", "%3d")
    result = nil.std.text::replace(result, ">", "%3e")
    result = nil.std.text::replace(result, "?", "%3f")
    result = nil.std.text::replace(result, "@", "%40")
    result = nil.std.text::replace(result, "[", "%5b")
    result = nil.std.text::replace(result, "\\", "%5c")
    result = nil.std.text::replace(result, "]", "%5d")
    result = nil.std.text::replace(result, "^", "%5e")
    result = nil.std.text::replace(result, "`", "%60")
    result = nil.std.text::replace(result, "{", "%7b")
    result = nil.std.text::replace(result, "|", "%7c")
    result = nil.std.text::replace(result, "}", "%7d")
    return result
end fn
