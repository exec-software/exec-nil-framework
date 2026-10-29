# NIL-0017: Versioned NILData and one shared value model

Status: implemented initial bounded document profile.

NILData is the framework's declarative data notation. Its named statements and
explicit blocks follow Matrix's prose conventions, while its values remain
independent of program execution. Loading a document never evaluates an
expression, expands an environment variable, imports code, or runs interpolation.

The former NyData implementations disagreed: the compiler encoded arrays as
numbered objects and inferred scalar types from text; the standard library had
separate list blocks and only text-valued scalars. Both accepted incomplete
input. Renaming those implementations would preserve incompatible data models.

## Decision

Define a versioned UTF-8 grammar and one shared implementation in
`nilcompiler/src/nildata.cpp`, linked into the compiler and runtime. The CLI,
interpreter, JIT and native programs use the same parser, validation, value
operations and serializers. The public library is `nil.std.nildata`.

Values are null, boolean, integer, decimal, text, object or array. **Array is the
only sequence kind**, including empty, heterogeneous and nested sequences.
Objects with numeric keys remain objects. Number tokens retain their exact
decimal spelling instead of being converted through binary floating point.

The library's `Value` contains a validated JSON text representation. Each
operation revalidates that representation because callers can construct public
records themselves. Updates return independent values. This avoids hidden
document handles and recursive native record layouts, and gives both execution
routes the same ownership behavior. It also means repeated queries and updates
reparse and copy documents; this initial API is for bounded documents, with no
streaming or constant-time access claim.

The serialization engine uses JSON as an internal representation and interchange
format. NILData's public grammar is independent; JSON's punctuation does not
appear in object and array declarations. Strings use explicit quoting and
escapes so text can contain whitespace, newlines and closing-marker words
without ambiguity. The [format and API contract](../standard-library/nildata.md)
defines the exact grammar and limits.

## Compatibility

`ny.lang.nydata` and `nil.std.nydata` imports resolve to `nil.std.nildata`.
This preserves module spelling, not the old recursive types or function
signatures. The new checked API deliberately replaces the old API.

Legacy document loading is explicit. Separate importers cover the old library
and compiler wrappers. They reject incomplete blocks and duplicate members;
scalar fields remain text and numbered objects remain objects. Information
discarded by an old writer cannot be recovered by inferring its appearance.
There is no legacy fallback inside `loads` and no legacy writer.

## Scope

The version header, bounds, full-input checks and deterministic output make the
format suitable for bounded configuration and tool messages. Schemas, source
editing that preserves comments, streaming, signing/canonicalization and a
Matrix shell integration are separate work. Sorted keys and preserved numeric
lexemes are deterministic serialization, not a semantic canonical form for
cryptographic signatures. This decision makes no change to the language's
typed `array<T>` syntax or storage ABI.
