# NIL Framework design records

Design records describe the shared framework and its Ny and Matrix front ends.
A design document is not evidence that its command, protocol, or package ships.
Each record states its implementation status and distinguishes current source
paths from future work.

## Identifier conventions

| Prefix | Scope |
| --- | --- |
| `NIL-` | Shared compiler, intermediate language, runtime, library, tools, and release contracts |
| `NY-` | Ny-specific syntax and language decisions |
| `MAT-` | Matrix-specific syntax and language decisions |

Assign an unused number within the appropriate prefix. Framework records use
four digits; retained Ny feature references keep their original three-digit
numbers. The identifier belongs in a record's filename, title, and references.
An implementation in shared code does not by itself change the scope of an
older language decision. Matrix's numbered discussion lists are local lists,
not assigned architecture identifiers.

## Framework records

| Identifier | Record | Status |
| --- | --- | --- |
| NIL-0001 | [Production applications are executable conformance specifications](../../notebook/decisions/NIL-0001-production-apps-are-conformance.md) | Accepted |
| NIL-0002 | [`nilinterpreter` is the runtime command](../../notebook/decisions/NIL-0002-nilinterpreter-is-the-runtime-command.md) | Implemented |
| NIL-0003 | [Serialized NIL is a toolchain boundary](../../notebook/decisions/NIL-0003-serialized-nil-is-a-toolchain-boundary.md) | Implemented as a preview |
| NIL-0004 | [Debugger state belongs to the VM](../../notebook/decisions/NIL-0004-debugger-state-belongs-to-the-vm.md) | Implemented as a preview |
| NIL-0005 | [Generated non-void fallthrough is explicit](../../notebook/decisions/NIL-0005-generated-nonvoid-fallthrough-is-explicit.md) | Implemented and verified |
| NIL-0006 | [Opt-in AI control for NIL programs](NIL-0006-ai-module.md) | Partial Windows prototype; interpreter introspection and Unix transport remain proposed |
| NIL-0007 | [Integer literal widths and range validation](NIL-0007-integer-literal-widths.md) | Implemented |
| NIL-0008 | [Shared project context and default analysis](NIL-0008-project-tooling.md) | Implemented initial local-project profile |
| NIL-0009 | [Workspace source snapshots for analysis](NIL-0009-workspace-analysis.md) | Implemented initial source-overlay contract |
| NIL-0010 | [Compiler bindings for source navigation](NIL-0010-semantic-source-index.md) | Implemented initial partial source index and definition navigation |
| NIL-0011 | [Completion queries at checked lexical scopes](NIL-0011-completion-queries.md) | Implemented initial value-identifier completion query |
| NIL-0012 | [Signature queries at resolved calls](NIL-0012-signature-queries.md) | Implemented initial source-backed call signatures |
| NIL-0013 | [Project-scoped references and transactional rename](NIL-0013-project-semantic-editing.md) | Proposed |
| NIL-0014 | [Explicit formatted text with original expression locations](NIL-0014-formatted-text.md) | Implemented initial scalar/text profile |
| NIL-0015 | [Structural tuples](NIL-0015-structural-tuples.md) | Implemented nonempty typed values and local destructuring |
| NIL-0016 | [Shared text patterns and collected queries](NIL-0016-patterns-and-queries.md) | Implemented initial byte-pattern and eager array-query profiles |
| NIL-0017 | [Versioned NILData and one shared value model](NIL-0017-nildata.md) | Implemented initial bounded document profile |
| NIL-0019 | [Explicit binding-reference parameters](NIL-0019-binding-reference-parameters.md) | Accepted; initial scalar/text implementation |
| NIL-0020 | [Declaration annotations as shared metadata](NIL-0020-declaration-annotations.md) | Accepted; initial scalar metadata profile |
| NIL-0021 | [Switch statements and expressions with automatic fallthrough](NIL-0021-fallthrough-switch.md) | Accepted; initial literal and payload-free enum selector profile |
| NIL-0022 | [Incremental project builds and NILData configuration](NIL-0022-incremental-project-builds.md) | Implemented initial local project graph and target scheduling profile |
| NIL-0023 | [Dynamic values and Boolean vocabulary](NIL-0023-dynamic-values-and-booleans.md) | Implemented |
| NIL-0093 | [Executable context is artifact self-description](NIL-0093-executable-context.md) | Implemented within the documented inspection profile |

## Retained Ny feature references

These identifiers already appeared in implementation comments. Their original
standalone records are not in this repository; the links identify the code
that retains each reference. They do not expand the supported release profile.

| Identifier | Subject | Implementation |
| --- | --- | --- |
| NY-001 | Scalar annotations and return-boundary lowering | [Code generator](../../nilcompiler/src/codegen.cpp) |
| NY-002 | Method dispatch from declared types when checking is disabled | [Code generator](../../nilcompiler/src/codegen.cpp) |
| NY-003 | Floating-point text formatting | [VM](../../nilcompiler/src/vm.cpp) |
| NY-020 | Iterator protocol for `for-in` loops | [VM](../../nilcompiler/src/vm.cpp) |
| NY-050 | Constant declarations | [Parser](../../nilcompiler/src/parser.cpp), [AST](../../nilcompiler/src/ast.hpp) |

## Other design work

- [Matrix design and status](../../matrix/README.md), including the current
  [front-end plan](../../matrix/docs/front-end-plan.md).
- [nypm package-manager proposal](nypm.md); no `nypm` command is currently built.
- [Research notebook](../../notebook/README.md) for dated implementation evidence
  and proposals that have not been assigned architecture identifiers.

## Identifier migration

The public framework naming pass on 2026-10-16 made these changes. Old spellings
appear only in this crosswalk so that earlier development notes remain traceable.

| Previous reference | Current reference |
| --- | --- |
| `NYD-0001` | `NIL-0001` |
| `Decision 0002` through `Decision 0005` | `NIL-0002` through `NIL-0005` |
| `NYG-002` | `NIL-0006` (avoids a collision with the runtime-command record) |
| `ADR 0093` | `NIL-0093` |
| `NYL-001`, `NYL-002`, `NYL-003`, `NYL-020`, `NYL-050` | `NY-001`, `NY-002`, `NY-003`, `NY-020`, `NY-050` |
