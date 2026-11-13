# NIL-0027: Multiline formatted text and structured markup literals

- **Status:** The initial §2 and §3 profile is implemented for both surfaces
  on 2026-11-13. The current contract below and §3.6 supersede the original
  runtime-tree, optional-hole and dialect-renderer proposals.
- **Scope:** Ny and Matrix syntax, shared hole lexing and AST, checking,
  lowering, `nil.std.markup`, text builder
- **Date:** 2026-11-13

## Context

A program that generates a document today concatenates one. The shape recurs
in every generator written against this framework:

```text
value nav is when signed-in
    give "Dashboard | Projects | Logout"
    otherwise give "Login | Register"
end when
value html is "<html><head><title>" plus title plus "</title></head><body>"
give html plus "<nav>" plus nav plus "</nav>"
```

Three separate defects, and only the first one is about typing:

1. **The document is not editable as a document.** Its structure is spread
   across bindings and `plus` chains, so a change to the markup is a change to
   control flow. This is the failure mode of a generation of server-rendered
   business applications, where one function is a ladder of conditionals each
   writing fragments of tags.
2. **Nothing checks the structure.** `"<div>" plus x` cannot be known to be
   balanced, so an unclosed element is discovered by a browser.
3. **Escaping is manual and context-blind.** The correct escape for a value
   differs between a text node, an attribute value and a URL attribute. A
   concatenated document applies none of them, and an interpolated string
   cannot apply the right one either, because it does not know where the hole
   landed.

NIL-0014 introduced explicit holes but confined a formatted literal to one
physical source line. Raw `begin text` blocks are multiline but deliberately
inert. Neither carries structure.

NIL-0016 is the precedent for the answer. A text pattern is not a string that
happens to be a regular expression, and a query is not a loop the author wrote
out; both are declared in the language's own words and lower to a checked
value. **A document deserves the same treatment,** and this record is the third
member of that family.

## 1. One hole grammar, three levels of structure

| Surface | Holes | Structure | Status |
| --- | --- | --- | --- |
| `formatted "..."` | yes | none, one line | NIL-0014, implemented |
| `formatted begin text ... end text` / `$"""` | yes | none, multiline | §2, implemented |
| `begin markup ... end markup` / `$markup` | yes | elements, attributes, control flow | §3, implemented |

**Current implementation contract.** A markup literal is checked as a document
at compile time, then evaluates to `nil.std.markup.document`, a record of
serialized text. Static markup stays literal; holes and directives assemble
the result at run time. `render` returns that text. There is no runtime DOM,
XML mode, compact renderer or automatic attribute omission. Ordinary holes
accept text and the formatted-text scalar types; a document splices only into
element content. Arrays and optional values need an explicit loop or
conversion. The original tree and renderer proposals are retained in §3.4–3.5
as design history, not available APIs. The library reference records the
[current escaping policy](../standard-library/markup.md).

All three use the hole lexer NIL-0014 already built, which emits an expression's
tokens with their original byte offsets rather than re-lexing decoded text.
Everything in this record inherits that contract, and no construct here
introduces an expression language of its own: a hole holds an ordinary
expression in its frontend's grammar, and nothing else.

## 2. Multiline formatted text

```text
value page is formatted begin text
    <p>Signed in as {who}</p>
    <p>{count plus 1} projects</p>
end text
```

Ny spells the same construct `$"""` ... `"""`. One difference is forced by the
surface: Matrix's `end text` owns its line, while Ny closes on the first line
whose content *begins* with `"""` and resumes lexing immediately after it,
because Ny is expression oriented and the closing `""")` must be allowed in
a call. The `$"""` opener must end its source line, apart from spaces or tabs;
content starts on the following line.

The block follows the existing raw-block rules exactly — line-anchored
terminator, common leading indentation removed, deeper indentation kept,
leading and trailing blank lines dropped, no escape sequences — and adds
`{expression}` holes with `{{` and `}}` for literal braces. A single hole
stays on one physical line, which keeps the expression lexer unchanged.

The implementation constraint, named as follow-up work in NIL-0014: **the
dedent must not move the holes.** The block scanner removes common indentation
and retains a per-line mapping back to the source. Hole tokens therefore keep
their original coordinates, so definition, completion, signature help and
local rename behave as they do inside a single-line formatted literal.

This is the smallest useful step and it is worth taking on its own: it is what
a C# raw interpolated string gives, and it removes most `plus` ladders from
ordinary code. It does not solve §3's problem, because the control flow still
lives outside the document.

**Implemented for Matrix on 2026-11-13.** `scan_formatted_block` in
`nilcompiler/src/formatted_text.hpp` scans the dedented block content, and
`matrix::lexer::scan_formatted_text_block` maps every piece back through a
per-line offset table before emitting the existing `formatted_string_begin` /
`formatted_text` / `interpolation_*` tokens. The parser, checker, lowering and
both execution routes are untouched, because the token stream and therefore the
AST are the ones the quoted form already produced. Coverage: the
`formatted-text-matrix` parity fixture across the VM, binary-NIL and native
routes; malformed blocks rejected on every route in
`tests/tools/formatted_text_contract.py`; and a CRLF block in
`tests/tools/semantic_contract.py` whose hole is renamed while an identical
word inside doubled braces in the dedented literal is left alone, which is the
proof that indentation removal did not move the expression.

**Ny followed on 2026-11-13.** `lexer::formatted_block` mirrors the Matrix
scanner and shares `scan_formatted_block`, so both surfaces produce the same
token stream and the same AST. A block is raw in both: Ny's backslash escapes
apply to a quoted `$"..."` and not inside `$"""`.

## 3. Markup literals

```text
read nil.std.markup

public page-shell receives title as text, signed-in as boolean, who as text
                  returns nil.std.markup.document begin paragraph
    give begin markup
        <html>
          <head><title>{title}</title></head>
          <body>
            <nav>
              {when signed-in}
                <a href="/dashboard">Dashboard</a>
                <a href="/logout">Log out, {who}</a>
              {otherwise}
                <a href="/login">Log in</a>
                <a href="/register">Register</a>
              {end when}
            </nav>
          </body>
        </html>
    end markup
end paragraph
```

The conditional is *inside* the document, where the variation actually is. The
page remains one unit a person can read, edit and paste.

### 3.1 What the block contains

Elements `<name attributes>children</name>`, HTML void elements such as `<br/>`,
text, comments, holes, and directives. Self-closing syntax is accepted only for
HTML void elements; a non-void element needs its matching closing tag. This
profile uses HTML tables and does not model SVG or MathML namespace semantics. Directives are spelled with the language's own
control words, so the block introduces no second vocabulary:

```text
<ul class="projects">
  {repeat for each p in projects}
    <li class="{p.status}" data-id="{p.id}">{p.name}</li>
  {end repeat}
</ul>
```

`{when ...}` / `{otherwise when ...}` / `{otherwise}` / `{end when}` and
`{repeat for each ... in ...}` / `{end repeat}` are the implemented Matrix
directives. Ny uses `{if ...}`, `{elseif ...}`, `{else}`, `{end if}`,
`{for ... in ...}` and `{end for}`. Numeric-range directives from the original
proposal are not implemented.

A directive may stand between elements, between attributes, or inside text or
an ordinary attribute value. Each arm must restore both the element stack and
the surrounding tag/attribute context: it cannot leave an element or quote
open for another arm to close. Directives inside URL attribute values are
rejected in this profile; calculate the URL beforehand or conditionally emit
the complete element. A bare boolean attribute cannot be an arm's entire
content: use a complete quoted attribute such as `disabled=""` inside that arm,
or conditionally emit the complete element.

**A hole cannot name an element or an attribute.** Both are fixed at compile
time, which is what makes the tree checkable. Dynamic attribute sets are
future work (§6).

### 3.2 Hole typing, by position

The initial proposal included arrays of documents and `nothing` holes. Those
are not implemented; this table describes the current checker.

| Position | Accepted | Behavior |
| --- | --- | --- |
| Element content | `text`, `bool`, `i32`, `i64`, `u8`, `f64`, `document` | Text/scalars escape; a document splices its serialized content |
| Ordinary attribute value | `text` and the same scalar types | Escape within the source's existing quotes; no implicit omission |
| URL attribute | `text` and the same scalar types | Whole-value scheme filter or fixed-prefix component encoding; see §3.4 |
| Code-bearing attribute or raw-text element body | Explicit `{raw ...}` only | Trusted bypass, not an escaping or sanitizing operation |

Use a loop for an array and explicit control flow for an optional value.
`{raw expression}` is the explicit trusted bypass. It accepts text or a
document in content or an already quoted attribute value, and does not
validate or escape the inserted bytes. Even `raw` cannot name an element or
attribute or appear in a comment. A manually constructed document and `nil.std.markup::raw` likewise require trusted input;
the public record is not a security boundary.

### 3.3 Decision: raw-text elements are literal

CSS and JavaScript are full of braces, and an author must be able to paste
them. Inside `<script>` and `<style>`, braces are ordinary characters and no
hole is recognised — **except** when the element's body is exactly one hole:

```text
<style>body { margin: 0 }</style>
<script>{raw bundle-source}</script>
```

The rule is a single lookahead over the element body and needs no escaping
convention, which keeps the no-escapes property of Matrix text intact in the
place where escaping would hurt most.

### 3.4 Original proposal: a dialect-free renderer (superseded)

**Design history.** The following renderer/table proposal preceded §3.6.
The implementation instead uses shared compiler-side HTML tables and library
escaping. Source quotes and self-closing spelling are preserved, and only HTML
is supported. The proposed scheme allowlist and runtime rejection below were
not implemented; see the current policy after the list.

The block parses generic elements, attributes, text, comments and holes, and
checks balance. Everything specific to HTML lives in one table the **renderer**
owns: void elements, raw-text elements, boolean attributes, URL-bearing
attributes, and the escape sets. The parser accepts `<br/>`; an HTML render
emits `<br>`, an XML render emits `<br/>`.

This is the difference between adding a construct and welding a document format
into the compiler. `begin markup as xml` then costs a table, not a grammar.

Escaping is per context and belongs to the same table:

- text node: `&`, `<`, `>`
- attribute value: `&`, `<`, `>`, `"`; output is always double-quoted
- URL attribute (`href`, `src`, `action`, `formaction`, `cite`, `poster`,
  `data`, `srcset`): percent-encoded per component, with a scheme allowlist.
  A hole producing a `javascript:` or `data:` URL is refused at run time unless
  it came through `{raw}`.

**Current URL policy.** A whole-value hole is checked for `javascript:`,
`vbscript:` and `data:` after browser-relevant ASCII control/space normalization
and ASCII case folding. A match encodes the colons as `%3a`, then applies
attribute escaping; it does not throw or omit the attribute. Other schemes
are not allowlisted. A component hole requires a fixed safe prefix such as
`/search?q=`, `https://example.com/`, `?q=` or `#section-`; variable scheme
assembly is rejected. Ordinary holes in `on*`, `style`, `srcdoc`, `srcset`,
`imagesrcset` and `ping` attributes require an explicit trusted `raw` bypass.
This policy is not an HTML, JavaScript, CSS or URL-list sanitizer.

### 3.5 Original proposal: a runtime tree (superseded)

**Design history.** This argument motivated the first design. §3.6 replaced
its runtime representation and rendering API; `render ... as xml` and
`render ... compact` below are proposals, not implemented syntax.

`begin markup` evaluates to `markup`. `nil.std.markup::render with document
being page` produces `text`; `render ... as xml` and a `compact` option select
serialization.

A string result would be simpler and would give up all four of the reasons for
doing this at all:

1. Context-correct escaping requires knowing where a value sits.
2. Balance is checkable only while the structure still exists.
3. **Composition stays structural.** A paragraph returning `markup`
   interpolates into another block as nodes — `<main>{page-header with title
   being title}</main>` — so factoring a page into components does not route
   back through text, and nothing is escaped twice.
4. A tree can be rendered to HTML, rendered to XML, or diffed. The last is
   what a future webview host wants, and it is unavailable from a string
   forever after.

### 3.6 Representation: revised — compiled serialization with runtime holes

**This section replaced its original proposal on 2026-11-13, during
implementation.** It first specified a runtime arena: parallel scalar arrays
holding a node tree, because §3.5 argued for a tree over text. Building it
turned up two things.

The first is that the obvious implementation does not execute. A `node` record
holding `array of node` children fails: native arrays of records, choices and
nested arrays are unsupported (MATRIX-GUIDE §17) and an array of class handles
compiles and then fails at run time (§16). An arena of parallel scalar and text
arrays does work on both routes, so the plan was sound.

The second is that it buys almost nothing here. Of §3.5's four reasons for a
tree, three do not need one at run time:

- **Context selection** happens while the block is parsed. The generated
  code still calls the escaping functions at run time, but does not traverse
  a runtime tree to decide which escaper to use.
- **Balance checking** happens at parse time, whatever the value becomes.
- **Composition without seams** needs only that a document be a distinct
  *type*, not a distinct *shape*: an already-escaped fragment spliced into
  another document is correct whether it is a node list or serialized bytes.

Only the fourth — rendering one document to several dialects, or diffing two —
needs a runtime tree, and that was already deferred to §5.

The implemented value type is `nil.std.markup.document`, a record holding
serialized text whose ordinary holes have already been escaped. Static
segments are compile-time constants; holes, control flow and result assembly
execute at run time. The type distinguishes
composition from ordinary text interpolation. Its public field and `raw`
constructor still allow trusted callers to supply arbitrary markup; the type
does not certify that manually constructed content is safe. A future arena
would need to preserve the public API or explicitly migrate its callers.

What this costs, stated plainly: the dialect is fixed when the block is
compiled rather than when it is rendered, so `begin markup as xml` becomes a
second lowering rather than a second renderer, and diffing two documents will
need the arena after all. Both were out of scope for this version, and neither
is reachable from a program written against this one.

### 3.7 Decision: the text builder — not yet needed

The original plan made an amortized text builder a prerequisite, because the
2026-10-15 audit (item 7) records that the experimental builder concatenates on
every append. With §3.6's revision the renderer is not a tree walk at all: a
page is a sequence of `STR_CONCAT` appends the compiler emits directly, and the
builder never enters the path. The audit item stands on its own merits and is
still worth fixing; it is no longer blocking.

What rendering does depend on is `nil.std.text`, and one gap surfaced there:
`to_lower` reaches the `_char_code` interop, which has no native ABI, so a page
using it would compile for the VM and fail natively. The URL scheme check folds
case with `replace` instead. Making `to_lower` native is separate follow-up
work.

## 4. What this is not

Not a template file format: there is no second language in a second file, and
holes are checked against the enclosing paragraph's parameters like any other
expression. Not a component system, a virtual DOM or a binding framework. No
precision or presentation grammar inside holes — those remain library calls, as
NIL-0014 decided. No automatic minification. No implicit database, network or
file access from a document.

## 5. Staging

1. §2 multiline formatted text. **Done 2026-11-13.**
2. ~~Amortized text builder~~ — not a prerequisite after §3.6; see §3.7.
3. `begin markup` with the HTML table: parse, check balance, holes, `{when}`,
   `{repeat}`, `{raw}`, render to text. **Done 2026-11-13.** The literal is
   lexed by the block scanner from §2, parsed as markup by
   `parser::parse_markup_expression`, checked in `type_checker::visit`
   (escape versus splice) and emitted by `emit_markup` as ordinary NIL, so both
   execution routes are covered by one emitter. Coverage: the `markup-matrix`
   parity fixtures across the VM, binary, native and native-toolchain routes;
   `tests/tools/markup_contract.py` checks malformed-document rejection and
   context-specific escaping on the supported routes.
4. `nil.std.markup` constructors beyond `raw` and `empty`, so a document can be
   assembled from values rather than written literally, and the XML table.
   **Deferred.**
5. Diffing two documents, which is what would bring §3.6's arena back.
   **Deferred.**

Ny reached the initial profile on 2026-11-13: `markup_structure` holds document
parsing, structural checks and escape decisions. Each frontend supplies its
own directive words and expression lexing; parity fixtures exercise the shared
semantics.

## 6. Open questions

- **Dynamic attribute sets.** `{spread attrs}` in attribute position, taking a
  list of name/value pairs, would cover `data-*` generation. Deferred: it
  weakens compile-time checking and is not needed by the first consumers.
- **Whitespace.** The initial rule preserves text after the block's common
  indentation and surrounding blank lines are removed. A composed document
  keeps its own remaining indentation. `render ... compact` is a deferred
  proposal; there is no whitespace-collapsing renderer in this version.
- **Editor support.** Holes keep original spans by construction (§2), but the
  VS Code grammar needs markup-inside-Matrix highlighting or the construct will
  look worse in an editor than the string it replaces.
- **Performance claims.** §3.6 asserts an append count, not a measurement.
  Benchmark the rendered page against the concatenated equivalent before the
  claim appears in any guide.
- ~~**Ny spelling.**~~ Settled while implementing. `markup { ... }`, this
  record's original candidate, does not survive contact with the content: a
  markup block is full of braces — holes, and CSS or JavaScript inside a
  raw-text element — so a brace-delimited block would end at the first `}` in a
  stylesheet. The terminator has to be a line-anchored keyword, which is what
  lets the block hold any bytes without escapes. Ny therefore opens with
  `$markup` at the end of its opening line and closes with line-leading
  `end markup`. Ny resumes the surrounding expression after that terminator;
  Matrix requires the terminator to occupy its line. The `$` is what keeps
  it unambiguous: Ny has no `begin`, so a bare word before a newline could be
  an ordinary name, while `$` already means "a literal with holes in it" in
  that surface. Matrix needs no sigil because `begin` supplies the same
  disambiguation.

## 7. Compatibility

Every construct here is additive under NIL-0026 §1: no existing program
observes a difference, because `formatted` before `begin text` is currently a
parse error, `markup` is a new contextual word before `begin`, and no existing
text literal changes meaning. Ships on by default, no edition gate.
