# Changelog

All notable changes to the NIL Framework VS Code extension are documented here.

## 1.0.0-rc.1

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
