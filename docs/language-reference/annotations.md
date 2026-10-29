# Annotations

Annotations attach metadata to declarations. Ny and Matrix share the same
metadata model and accept the familiar `@name` spelling. Matrix also offers
`note` and `context` forms for prose-oriented code.

An annotation does not call a function or wrap a declaration. Custom names
describe data for tools and analyzers. Only annotations recognized by the
compiler or a runtime provider have built-in behavior.

## Declaration syntax

Annotations precede the declaration they describe and can be stacked:

```matrix
@doc(summary="Advance the counter", returns="The updated value")
@Company.review(owner="runtime", approved=true, revision=2)
advance receives reference count as number returns number begin paragraph
    set count to count plus 1
    give count
end paragraph
```

The equivalent Ny declaration uses the same annotation syntax:

```ny
@doc(summary="Advance the counter", returns="The updated value")
@Company.review(owner="runtime", approved=true, revision=2)
fn advance(ref count: i64) -> i64
    count = count + 1
    return count
end fn
```

A marker has no arguments: `@inline`. Parenthesized arguments can span lines.
Arguments are comma-separated positional values or named properties:

```matrix
@Company.review(
    "runtime",
    revision=2,
    approved=true,
    tolerance=-0.25,
    policy=Policy.Strict
)
```

This example is an annotation prefix; follow it with a supported declaration.
Metadata values are text, booleans, signed integers, floating-point numbers,
or symbols. A symbol such as `Policy.Strict` is a metadata name, not a variable
lookup or an expression to execute. Arrays, maps, and computed expressions are
outside the scalar metadata profile.

Positional arguments must precede named properties, and property names cannot
repeat. Integer values must fit signed 64-bit storage; floating-point values
must be finite and representable as `f64`.

Annotation names can be qualified with `.` or `::`; the shared representation
uses `::`. Matrix normalizes known built-in names to their canonical lowercase
spelling. Custom names retain case, and Matrix hyphens become underscores.

Text follows the source language's rules. Ny strings recognize Ny escapes.
Matrix strings remain raw: a backslash does not escape a quote, including
inside an `@...` annotation. The annotation delimiters do not change the rest
of Matrix's expression grammar.

## Matrix prose forms

Use `note` for the same scalar metadata without annotation punctuation:

```matrix
note doc with summary being "Advance the counter", returns being "The updated value"
note Company.review with "runtime", revision being 2, approved being true
advance receives reference count as number returns number begin paragraph
    set count to count plus 1
    give count
end paragraph
```

The structured `with` form stays on one source line. Positional values and
named `key being value` properties use commas between arguments. Existing
forms remain available:

```matrix
note inline
note intrinsic "console.writeln"
context "Writes one line to the configured console."
write-line receives line as text
```

`context "..."` is a shorthand for `ai_context` metadata. Existing flat
`note name ...` source spellings remain supported, with literal boundaries
preserved in retained metadata. Use `with` when tools need individual typed
properties.

## Targets and behavior

Matrix supports annotations on named paragraphs, records, classes, choices,
and their supported method declarations. They do not attach to fields,
parameters, local bindings, contracts, aliases, tests, or implementation and
extension containers. An unsupported target is diagnosed instead of silently
discarding its metadata.

The common annotation vocabulary includes:

| Name | Meaning |
| --- | --- |
| `doc` | Documentation metadata such as `summary`, `description`, and `returns` |
| `ai_context` | Retained descriptive context for humans and tools |
| `inline` | Retained function metadata and inline flag; does not guarantee a native optimizer action |
| `intrinsic` | Existing runtime-provider dispatch declaration |
| `interop` | Existing foreign-provider metadata; availability depends on the linked provider |
| Custom name | Inert metadata for an analyzer or application-specific consumer |

Do not infer runtime behavior from a custom annotation's name. For example,
`@deprecated(...)` alone does not install a deprecation analyzer, and
`@Company.review(...)` does not execute a `Company.review` function. Provider
annotations still require an implementation on each execution route.

## Inspection and tooling

Compiler reflection exposes declaration annotations and their retained raw
argument text. Scalar arguments also have a parsed representation for tools.
Legacy Ny composite and nested payloads remain available as opaque metadata
for any annotation name, including custom names. For example,
`@custom(outer(inner("payload")))` retains its argument text without executing
`outer` or `inner`. Consumers must check whether arguments were parsed before
reading typed properties. Matrix remains strict about scalar arguments; this
Ny compatibility does not add nested forms, lists, or maps to Matrix.

Each reflection annotation retains `owner_id`, `name`, `arguments`, and
`line`, and adds `arguments_parsed` plus `parsed_arguments`. A parsed argument
has `name` (null for positional values), `kind`, and `value`. Kinds are `text`,
`boolean`, `integer`, `floating`, and `symbol`; `value` is text containing the
decoded string or validated scalar/symbol spelling. An opaque legacy payload
reports `arguments_parsed: false` and an empty parsed list.

Generated standard-library reflection snapshots expose
`annotation_arguments_parsed(annotation_id)`, `annotation_argument_count()`,
and `annotation_argument(index)`. Each argument's `annotation-id` identifies
its annotation entry, rather than the annotated declaration.

`nilcompiler --inspect-context` reads retained `ai_context` metadata without
executing the artifact. Native context embedding remains an explicit
`--embed-context` operation; see
[executable context](../advanced/executable-context.md) for format and route
limits.

The VS Code extension highlights annotation names and properties, offers
annotation snippets, and completes common Matrix annotation names and
documentation properties. Custom metadata names remain valid without being
listed in completion. Annotation payloads are excluded from ordinary local
symbol navigation. Hover and signature help do not yet render annotation
documentation.

## Preprocessing

Both frontends run the shared preprocessor before declaration parsing.
Directives are separate from metadata and use their exact, case-sensitive
spellings, including in Matrix:

| Directives | Purpose |
| --- | --- |
| `@include`, `@pragma once` | Include source fragments and suppress repeat inclusion |
| `@define`, `@undef` | Define and remove macros |
| `@if`, `@ifdef`, `@ifndef`, `@elif`, `@else`, `@endif` | Select source conditionally |
| `@pragma link`, `@schema` | Existing build/schema metadata |
| `@error`, `@warning`, `@message` | Emit preprocessing diagnostics |

For example, a Matrix source fragment can use:

```matrix
@define FEATURE_ENABLED 1
@ifdef FEATURE_ENABLED
@doc(summary="Available in feature-enabled builds")
feature-enabled returns boolean begin paragraph
    give true
end paragraph
@endif
```

Macro names are case-sensitive and respect complete Matrix identifiers,
including hyphens and Unicode. Expansion is single-pass; replacement text is
not recursively rescanned. Function macros support grouped arguments, zero
arguments, and variadics, but stringification (`#`) and token pasting (`##`)
are unsupported.

Matrix raw quoted text, escaped identifiers, `!!!` comments, formatted text,
`begin text` blocks, and complete parenthesized annotation regions are protected
from macro expansion. A macro name inside an annotation is retained metadata,
not a request to replace its value. Inactive conditional branches do not apply
their includes, macro changes, pragmas, or diagnostic directives.

Includes and macro/conditional transformations do not yet have a full original
source map. Compiler analysis conservatively withholds source-edit spans and
signature-query offsets when transformations prevent reliable correspondence.
Simple blank-line and recognized `@pragma once`/`@schema` elision can preserve
locations. The fallback document-local index does not evaluate conditional
branches; use compiler analysis for the active program. Workspace analysis
uses its source snapshot for includes and enforces the existing source-output,
include-depth, and include-visit limits.

These directives do not invoke decorator functions or run arbitrary code
during compilation. See
[NIL-0020](../design/NIL-0020-declaration-annotations.md) for the shared design
and compatibility boundary.
