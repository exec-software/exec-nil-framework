# NIL-0020: Declaration annotations as shared metadata

- **Status:** Accepted; initial scalar metadata profile
- **Scope:** Ny and Matrix source syntax, shared annotation parsing,
  declaration reflection, and editor support

## Decision

Expose declaration metadata through stackable `@name` and
`@name(positional, property=value)` annotations in both languages. Matrix keeps
`note name ...` and `context "..."`, and adds the structured prose form
`note name with positional, property being value`.

The familiar `@` prefix provides a compact, recognizable declaration boundary.
It does not introduce Python's executable decorator semantics. Annotation
names are metadata identities, not calls or references to runtime functions.
Existing compiler-recognized annotations keep their established effects.

## Scalar argument profile

Arguments contain text, booleans, signed integers, floating-point numbers, or
symbols. Positional arguments and named properties retain their order. Symbols
are metadata values and are not evaluated. Arrays, maps, general expressions,
and executable transformations are deferred.

Positional values precede named properties, duplicate property keys are
rejected, integers fit signed 64-bit storage, and floating-point values must
be finite and representable as `f64`.

Parenthesized annotations may span lines. Matrix's prose `with` form remains
single-line. Each frontend retains its string rules: Ny recognizes its usual
escapes, while Matrix quoted text remains raw. Qualified annotation names
accept `.` and `::` and use `::` in their shared identity. Matrix canonicalizes
known built-in names and maps hyphens to underscores while preserving custom
name case.

## Attachment and retention

Matrix attaches annotations to named paragraphs, records, classes, choices,
and supported methods. It rejects unsupported attachments rather than dropping
metadata. Fields, parameters, local bindings, contracts, aliases, tests, and
implementation or extension containers are not part of this profile.

The shared representation retains raw annotation arguments for existing
consumers and adds parsed scalar arguments for reflection. Legacy Ny composite
and nested payloads remain available as opaque metadata for any annotation
name, including custom names, with `arguments_parsed: false`. For example,
`@custom(outer(inner("payload")))` is retained as inert argument text; its
nested forms are not calls. Consumers cannot assume every historical
annotation is representable as scalar properties. Matrix keeps the strict
scalar profile and does not inherit this Ny compatibility path.

Annotations remain independent of preprocessing. Both frontends run shared
preprocessing directives before declaration parsing. Matrix literal regions
and complete parenthesized annotation payloads are protected from macro
expansion. Directive and macro names retain exact case-sensitive spelling;
macro expansion remains single-pass. Inactive branches cannot apply includes,
macro changes, pragmas, or diagnostics.

Workspace analysis uses its source snapshot and existing include/output
bounds. Includes and nontrivial transformations still lack a full original
source map, so compiler source-edit spans and signature-query offsets are
withheld when correspondence cannot be established. Neither metadata support
nor preprocessing adds automatic decorator execution.

## Editor contract

Highlight annotation names, named properties, scalar values, and delimiters
within a bounded annotation region. Matrix's ordinary expressions continue to
use their existing grammar. Completion and snippets expose common metadata
forms without treating the suggested vocabulary as an allowlist.

The document-local index ignores annotation payloads when discovering symbols
and resolving code references. In particular, multiline properties containing
words such as `value`, `begin`, or `returns` must not create declarations or
change the following declaration's source location. This metadata does not
yet populate compiler signature help or hover documentation.

## Acceptance scope

Regression coverage must compare both source frontends' scalar argument
representations; validate malformed syntax and unsupported attachments;
preserve legacy metadata and executable-context behavior; and verify exact
following-declaration locations, multiline highlighting, raw Matrix strings,
completion boundaries, and metadata exclusion from code navigation.

See the [annotation reference](../language-reference/annotations.md) for public
syntax. This record defines the profile; test results are reported separately
and do not imply general decorator execution or arbitrary metadata values.
