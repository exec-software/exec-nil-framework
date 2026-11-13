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

A `document` contains **serialized markup**. A compiler-checked block escapes
its ordinary holes before constructing the record; interpolating that document
into element content splices its existing markup rather than escaping it again.
`render` returns `content` without parsing a DOM or walking a runtime tree.

The field is public, and `raw` deliberately adopts arbitrary text. A manually
constructed document therefore requires trusted input: its type does not
validate or sanitize its contents.

| Function | Result |
| --- | --- |
| `render(doc)` | return the document's serialized text |
| `raw(content)` | adopt already-trusted markup as a document |
| `empty()` | a document that contributes nothing |
| `escape_text(content)` | element-content escaping: `&`, `<`, `>` |
| `escape_attribute(content)` | attribute escaping: `&`, `<`, `>`, `"`, `'` |
| `escape_url(content)` | a whole URL attribute value |
| `escape_url_component(content)` | one component inside a URL |

A markup block calls these for you, choosing by the position the hole landed
in. Calling them directly is supported and is what hand-built documents do.

## Hole types and trusted input

An ordinary element-content hole accepts `text`, `bool`, `i32`, `i64`, `u8`,
`f64`, or a `document`. Attribute-value holes accept the same text/scalar types;
an ordinary document hole in an attribute is rejected. Arrays and optional
values are not implicit holes: use a loop, a conditional, or an explicit
conversion. No value automatically removes an attribute.

`{raw expression}` accepts text or a document and bypasses escaping and URL
filtering in element content, a raw-text body, or an already quoted attribute
value. It still cannot stand in for an element/attribute name or appear inside
a comment. The inserted bytes themselves are not parsed or checked, so the
caller must supply trusted content appropriate for that exact position.

## Escaping contract

**Element content and ordinary attribute values** escape their HTML delimiters.
Text escapes `&`, `<` and `>`; attributes additionally escape both quote forms.
The block preserves the author's single or double quotes around an attribute;
the escaper does not add quotes or rewrite literal markup.

**A whole URL attribute value** (`href`, `src`, `action`, `formaction`,
`cite`, `poster`, `data`, `background`, `manifest`, `xlink:href`) uses a limited
scheme filter. The check removes embedded ASCII tab, LF and CR, trims leading
ASCII controls/space as a browser does, and folds ASCII case before comparing
`javascript:`, `vbscript:` and `data:`. A colon after the first `/` belongs to
the path rather than a scheme. When the check matches, colons in the supplied
value are percent-encoded as `%3a`, then attribute escaping is applied. The
attribute is neither omitted nor rejected at run time. Other schemes remain
allowed; this is not a scheme allowlist.

**A component inside a URL** percent-encodes the reserved and unsafe ASCII set
and passes other bytes, including UTF-8, through for the browser to encode.
`%` is encoded before the other replacements. Such a hole needs a fixed safe
prefix, for example `href="/search?q={query}"`, `src="https://example.com/{path}"`,
`href="?q={query}"` or `href="#section-{id}"`. Variable scheme assembly such as
`href="{scheme}:example"` is rejected. A complete dynamic URL should occupy the
whole attribute value and use the whole-value filter instead.

**Code-bearing and URL-list attributes** need an explicit trusted `{raw ...}`
hole: `on*` event handlers, `style`, `srcdoc`, `srcset`, `imagesrcset` and `ping`.
HTML quote escaping does not make JavaScript, CSS, nested HTML or URL lists safe.
Literal attribute contents remain the author's responsibility.

**Directives inside URL attribute values** are rejected in this profile.
Compute the URL in ordinary code beforehand or conditionally emit the complete
element. Other directive arms must restore both the element stack and the
surrounding tag/attribute context; an arm cannot leave a quote open for another
arm to close. Conditional bare boolean attributes are not supported: emit a
complete quoted attribute such as `disabled=""` inside the conditional, or
conditionally emit the complete element.

Escaping is byte-oriented and allocates a new text per pass. A single-pass
escaper is a later optimisation behind the same contract.

## What this module does not do

There is no general HTML, JavaScript, CSS or URL-list sanitizer, URL scheme
allowlist, DOM, XML serialization or compact renderer. `raw` adopts text without
inspecting it. Pass untrusted text through an ordinary hole in a supported
context, or validate it explicitly before using a trusted bypass. Self-closing
syntax is limited to HTML void elements, and the parser does not model SVG or
MathML namespace semantics. Static markup and composed document
bytes are emitted as written; they are not revalidated by `render`.
