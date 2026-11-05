# Reporting security issues

Use GitHub's private vulnerability reporting for this repository when
available. If no private reporting channel is available, open a minimal issue
requesting one without including exploit details, credentials, or private
data. Ordinary bugs can be reported publicly.

Include the affected revision, the tool involved (compiler, interpreter,
assembler, linker, debugger, language server, build or package tool), your
platform, and a minimal reproduction. Do not test against another person's
machine or infrastructure.

## Two specified behaviors that are not vulnerabilities

Please read these before reporting, because both look like findings and
neither is one:

- **An out-of-bounds access passes silently.** This is a deliberate contract,
  not a missing check: a program that reads or writes past the end of an array
  keeps running, and the first visible symptom usually appears somewhere else
  entirely. Set `NIL_STRICT_BOUNDS=1` and the same access reports with the
  index, line, operation and the valid range. See
  [runtime errors](docs/advanced/runtime-errors.md) and
  [NIL-0026](docs/design/NIL-0026-language-evolution.md).
- **The AI module opens a local IPC endpoint.** It is owner-scoped by design:
  a Windows named pipe at `\\.\pipe\ny_ai_<pid>`, or on Unix an owner-only
  socket that checks the connecting user's UID with `SO_PEERCRED` on Linux and
  `getpeereid` on macOS and BSD. See
  [NIL-0006](docs/design/NIL-0006-ai-module.md).

A way to defeat either — bounds reporting that can be suppressed when it was
requested, or that IPC endpoint reachable by another user, from another
machine, or by a process that is not the one that created it — is a security
issue and worth reporting.

## What is in scope

The toolchain compiles, assembles, links, interprets and debugs source and
object files it is handed. Those inputs are code, and building untrusted code
runs untrusted build logic, exactly as with any other toolchain. What we do
treat as a vulnerability is a malformed *input* subverting a tool that is only
meant to read it: a crafted object file, debug payload or package manifest
that achieves memory corruption or code execution in the assembler, linker,
debugger or package resolver, or a path in a manifest or archive that escapes
its intended directory.

Keep local configuration and secrets outside the source tree. If a credential
is committed, revoke or rotate it and review historical exposure; removing the
current file alone does not remove the credential from Git history.
