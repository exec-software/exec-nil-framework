# Markup documents

`read nil.std.markup` from Matrix, or `import nil.std.markup.escaping` from Ny
and call through `nil.std.markup::`. The module holds the
document type a `begin markup` literal produces ([NIL-0027](../design/NIL-0027-structured-markup.md))
and the escaping that literal lowers to. The functions are ordinary public
functions, not compiler internals: the escaping a generated page depends on is
readable, testable source, and a document can be assembled by hand where a
literal does not fit.

## The document type

```text
pub struct document
    content: text
end struct
```

A `document` is **serialized, already-escaped markup**. The distinction from
`text` is the point of the type: interpolating a document into another markup
block splices it, while interpolating text escapes it. Without the type, every
composition would either double-escape or silently trust its input.

| Function | Result |
| --- | --- |
| `render(doc)` | the serialized text; the only supported way out of a document |
| `raw(content)` | adopt already-trusted markup as a document |
| `empty()` | a document that contributes nothing |
| `escape_text(content)` | element-content escaping: `&`, `<`, `>` |
| `escape_attribute(content)` | attribute escaping: `&`, `<`, `>`, `"`, `'` |
| `escape_url(content)` | a whole URL attribute value |
| `escape_url_component(content)` | one component inside a URL |

A markup block calls these for you, choosing by the position the hole landed
in. Calling them directly is supported and is what hand-built documents do.

## Escaping contract

**Element content and attribute values** are escaped for the HTML of this
version: an attribute is always rendered double quoted, and both quote forms
are escaped so hand-written single-quoted markup stays correct.

**A whole URL attribute value** (`href`, `src`, `action`, `formaction`,
`cite`, `poster`, `data`, `srcset`, `background`, `ping`, `manifest`) is scheme
filtered first. `javascript:`, `vbscript:` and `data:` are neutralised by
percent-encoding the scheme's colon, which leaves an inert relative reference:
the page still renders, nothing executes, and the original text stays visible
to anyone reading the output. A colon that appears after the first `/` belongs
to a path, not a scheme, and is left alone. Comparison folds ASCII case.

**A component inside a URL** percent-encodes the reserved and unsafe ASCII set
and passes other bytes, including UTF-8, through for the browser to encode.
`%` is encoded first, so nothing is encoded twice.

Escaping is byte-oriented and allocates a new text per pass. It is correct
before it is fast; a single-pass escaper is a later optimisation behind the
same contract.

## What this module does not do

No sanitizer, no allow-list filter, no DOM. `raw` adopts text without
inspecting it, so content from outside the program must be proven safe by the
caller or escaped by passing it through a hole instead. The module renders
HTML; XML serialization and whitespace-collapsing output are future work.
