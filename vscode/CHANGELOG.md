# Changelog

All notable changes to the NIL Framework VS Code extension are documented here.

## 1.0.0-rc.1

- Add `.nildata` syntax highlighting, comments, indentation, and folding for the
  versioned NILData format, with strict JSON scalar and escape highlighting. Keep
  data documents outside compiler analysis, source tasks, and debugging.

- Highlight explicit Ny and Matrix interpolation, tuple types and Matrix numeric
  signs; add tuple/destructuring/formatted-text snippets. The tolerant language
  service indexes flat tuple bindings and preserves completion inside supported
  interpolation expressions while excluding their literal text.
- Offer Matrix `negative`, `tuple of` and `formatted` in keyword completion.

- Extend compiler completion to supported receiver fields and methods, including
  imported declarations, generic substitutions and accessibility checks.
- Add compiler-backed signature help with original argument slots, Matrix named
  parameter selection, nested calls, imported functions and immutable callbacks.
  Retain syntax diagnostics during narrow bare-`with` query recovery.

- Add compiler-backed identifier completion for visible locals, parameters and
  imported functions in Ny and Matrix, including unresolved typed prefixes.
  Validate complete source snapshots and replace whole tokens; retain tolerant
  completion for unsupported contexts and older SDKs.
- Resolve Matrix named arguments against the checked callable, including imported
  functions and proven immutable callbacks, preserving argument order under local
  rename. Require positional arguments when callback labels cannot be proven.

- Add complete references and rename for compiler-certified local variables and
  loop binders in Ny and Matrix. Reject ambiguous coverage, reserved names,
  capture risks and multiple open aliases; recheck proposed edits and verify
  preserved bindings before returning a workspace edit.
- Resolve Matrix paragraph values with the same declared-name folding as direct
  calls, preserving escaped spelling and lexical shadowing.

- Resolve Go to Definition through checked compiler bindings for source variables,
  parameters and direct/imported functions across Ny and Matrix, including dirty
  and new sources. Preserve exact UTF-8 name spans and lexical binding identities.
- Validate the complete semantic source snapshot before navigation; suppress stale,
  unsupported, failed-check and binary-library destinations. Gate editing on
  explicit compiler completeness capabilities.
- Check unsaved imported Ny and Matrix buffers through bounded compiler workspace
  input, including new named untitled modules and their nested project settings.
- Cancel checks when any captured buffer or workspace context changes; publish
  imported Problems only against matching current buffers or verified disk text.
- Preserve compiler import precedence and guard conflicting aliases of one source.
- Exclude clean cached models after editor tabs close while retaining every dirty
  named buffer, so closed aliases cannot block unsaved source checks.
- Add `nil.project.json` validation and `nilbuild` project check/build/run commands
  and tasks for VM and native targets; reuse project imports, SDK, and analyzer
  policy in current-document checks.
- Display compiler semantic warnings for unreachable code and discarded checked
  outcomes, with manifest suppression and warnings-as-errors settings.
- Place imported compiler diagnostics in Problems only against verified source
  snapshots; aggregate reports and refresh dependents after source changes.

- Present the extension as NIL Framework (Ny + Matrix) and discover the renamed
  `nilcompiler`, `nilinterpreter`, `nildebugger`, and `nillsp` SDK commands.
- Preserve the extension identifier, `ny.*` settings/commands, task/debug types,
  and the legacy `$nycompiler` problem matcher for existing workspaces.
- Integrate Matrix `.matrix`/`.mat` files with the language server, compiler
  tasks, Problems, native/VM commands, and F5 source debugging.
- Correct Matrix highlighting and snippets for case-insensitive keywords,
  hyphenated names, declarations, named calls, exact-width types, and raw text.
- Add Matrix completion, hover, navigation, and outline support through a
  Matrix-aware SDK server, including lexical scopes and known receiver members.
- Fix executable language-server startup and add isolated VS Code host tests
  for completion, compilation, diagnostics, and debugger integration.
- Align the extension and all SDK tools with the central NIL Framework product version.
- Add F5 source-level NIL debugging through `nildebugger --dap`, opt-in source
  maps, lexical locals, breakpoints, stepping, and evaluation.
- Refine document-local completion, definition and hover for lexical scope and
  known aggregate receivers; retain the documented project-wide LSP limits.
- Package notices for bundled production dependencies.
- Keep execution restricted to trusted local workspaces. Native debugging and
  whole-project semantic completion are not part of this candidate.

## 0.1.0

- Add `.ny` language registration and compiler-aligned TextMate highlighting.
- Add editing configuration and comprehensive Ny snippets.
- Add trusted-workspace discovery for `nilcompiler`, `nilinterpreter`, and
  `nillsp`.
- Add the stdio language client with restart, output, and trace controls.
- Add compile, run, LLVM native, SDK build, and CTest commands and tasks.
- Add the current parse-diagnostic problem matcher.
- Add Node-only unit tests for assets, grammar, snippets, tool discovery, and
  task argument construction.
- Document current LSP, diagnostic, and debugger limitations.
