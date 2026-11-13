# NIL-0027: Multiline formatted text and structured markup literals

- **Status:** Proposed. Staged; nothing in this record ships yet.
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
| `formatted begin text ... end text` | yes | none, multiline | §2 of this record |
| `begin markup ... end markup` | yes | elements, attributes, control flow | §3 of this record |

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

Ny spells the same construct `$"""` ... `"""`.

The block follows the existing raw-block rules exactly — line-anchored
terminator, common leading indentation removed, deeper indentation kept,
leading and trailing blank lines dropped, no escape sequences — and adds
`{expression}` holes with `{{` and `}}` for literal braces. A single hole
stays on one physical line, which keeps the expression lexer unchanged.

The one implementation constraint, named as follow-up work in NIL-0014: **the
dedent must not move the holes.** Indentation removal applies to literal
segments at lowering. The lexer records each segment's original span and the
number of stripped bytes per line, and hole tokens keep their original
coordinates, so definition, completion, signature help and local rename behave
as they do inside a single-line formatted literal.

This is the smallest useful step and it is worth taking on its own: it is what
a C# raw interpolated string gives, and it removes most `plus` ladders from
ordinary code. It does not solve §3's problem, because the control flow still
lives outside the document.

## 3. Markup literals

```text
public page-shell receives title as text, signed-in as boolean, who as text
                  returns markup begin paragraph
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

Elements `<name attributes>children</name>`, self-closing `<name/>`, text,
comments, holes, and directives. Directives are spelled with the language's own
control words, so the block introduces no second vocabulary:

```text
<ul class="projects">
  {repeat for each p in projects}
    <li class="{p.status}" data-id="{p.id}">{p.name}</li>
  {end repeat}
</ul>
```

`{when ...}` / `{otherwise when ...}` / `{otherwise}` / `{end when}` and
`{repeat for each ... in ...}` / `{repeat for ... from ... to ...}` /
`{end repeat}` are the complete directive set. A directive may stand between
elements, between attributes, or inside a text node or attribute value. Its
arms must each be well-formed: a directive cannot open an element in one arm
and close it in another, because that reintroduces exactly the fragment
assembly this record exists to remove.

**A hole cannot name an element or an attribute.** Both are fixed at compile
time, which is what makes the tree checkable. Dynamic attribute sets are
future work (§6).

### 3.2 Hole typing, by position

| Position | Accepted | Notes |
| --- | --- | --- |
| Text node | `text`, scalar, `markup`, `array of markup`, `nothing` | `markup` splices as nodes, never as escaped text |
| Attribute value | `text`, scalar, `nothing` | `nothing` omits the attribute entirely |
| URL attribute | `text` | scheme-checked; see §3.4 |
| Raw-text element body | `text` via `{raw ...}` only | see §3.3 |

`{raw expression}` splices text without escaping. It is deliberately a word a
reviewer can grep for, and it is the only way to put unescaped text into a
document.

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

### 3.4 Decision: the parser is dialect-free

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

### 3.5 Decision: the value is a tree, not text

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

### 3.6 Representation: an arena, not a record graph

The obvious implementation — a `node` record holding `array of node` children —
does not execute. Native arrays of records, choices and nested arrays are
unsupported (MATRIX-GUIDE §17), and an array of class handles compiles and then
fails at run time (§16). A tree of record handles would therefore be
interpreter-only, which is not acceptable for the one construct whose whole
purpose is to be used everywhere.

A `markup` value is instead an **arena**: parallel arrays of scalars
(`kind`, `name-id`, `parent`, `first-child`, `next-sibling`, `text-id`) beside
a text pool `array of text`. Arrays of scalars and arrays of text are supported
on both routes today. Composition is an append of one arena into another with
an index offset. Rendering is one pre-order walk.

This also gives the compile-time optimisation for free: a literal's static
subtrees have known shape, so the compiler emits their serialized bytes as
constant segments and the render becomes a sequence of appends with the holes
patched in. A page with twelve holes costs about twelve appends, not a tree
traversal.

### 3.7 Decision: the text builder is a prerequisite

The 2026-10-15 standard-library audit (item 7) records that the experimental
text builder concatenates on every append, so appending *n* segments is
quadratic. Rendering is the one workload that makes this visible on every
request. An amortized-growth builder in `nil.std.text` lands before the
renderer, and the renderer preflights its output size the way
`nil.std.text`'s pattern replacement already does.

## 4. What this is not

Not a template file format: there is no second language in a second file, and
holes are checked against the enclosing paragraph's parameters like any other
expression. Not a component system, a virtual DOM or a binding framework. No
precision or presentation grammar inside holes — those remain library calls, as
NIL-0014 decided. No automatic minification. No implicit database, network or
file access from a document.

## 5. Staging

1. §2 multiline formatted text. Self-contained, and useful the day it lands.
2. Amortized text builder (§3.7).
3. `begin markup` with the HTML table: parse, check balance, holes, `{when}`,
   `{repeat}`, `{raw}`, render to text.
4. `nil.std.markup` constructors, so a document can also be assembled from
   values rather than written literally, and the XML table.
5. Diffing two arenas. Out of scope here; named so that §3.6 is not mistaken
   for speculation.

## 6. Open questions

- **Dynamic attribute sets.** `{spread attrs}` in attribute position, taking a
  list of name/value pairs, would cover `data-*` generation. Deferred: it
  weakens compile-time checking and is not needed by the first consumers.
- **Whitespace.** The initial rule is to preserve the block's text as written,
  because HTML whitespace is sometimes significant, with collapsing offered by
  `render ... compact`. Worth revisiting once there is real output to compare.
- **Editor support.** Holes keep original spans by construction (§2), but the
  VS Code grammar needs markup-inside-Matrix highlighting or the construct will
  look worse in an editor than the string it replaces.
- **Performance claims.** §3.6 asserts an append count, not a measurement.
  Benchmark the rendered page against the concatenated equivalent before the
  claim appears in any guide.
- **Ny spelling.** `$"""` for §2 is settled by analogy; the Ny surface for
  §3 is not. `markup { ... }` is the obvious candidate and is not yet decided.

## 7. Compatibility

Every construct here is additive under NIL-0026 §1: no existing program
observes a difference, because `formatted` before `begin text` is currently a
parse error, `markup` is a new contextual word before `begin`, and no existing
text literal changes meaning. Ships on by default, no edition gate.
