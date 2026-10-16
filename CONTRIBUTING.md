# Contributing to NIL Framework

Ny and Matrix are separate frontends over one compiler framework. Changes to
shared behavior belong in the common semantic/runtime layer where practical;
changes to surface syntax belong in the corresponding frontend.

## Local workflow

1. Build and run the checks described in [README.md](README.md#build-the-sdk).
2. Add a focused reproducer for a language/runtime change. Exercise both
   frontends when the shared behavior is affected, and the relevant interpreter,
   bundle, JIT, AOT, assembler/linker, or debugger routes.
3. Keep generated artifacts under ignored `build` directories. Do not commit
   SDK generations, binaries, local editor state, credentials, or test databases.
4. Update the public documentation when behavior changes. Record confirmed
   language/library friction in the [findings document](matrix/docs/next-version-findings.md).
5. Run `git diff --check` and the affected tests before submitting a change.

For editor work, use `npm ci` and `npm test` under `vscode`. `npm run test:host`
requires a local VS Code installation and exercises the actual extension host.
SDK packaging is a separate validation step; its source identity must match the
commit being qualified.

## Decisions and scope

Use the [design index](docs/design/README.md) for new architectural records:
`NIL-` covers shared infrastructure, `NY-` covers Ny-only behavior, and `MAT-`
covers Matrix-only behavior. Preserve existing decision numbers and link
superseding decisions rather than silently changing their meaning.

Experimental code belongs in clearly marked experimental areas. Compilation
alone does not establish runtime support. Document platform and API boundaries
with the evidence actually available, and avoid widening a release claim merely
because a new declaration or test exists.

## Licensing and attribution

First-party contributions are made under the repository's [MIT License](LICENSE).
Retain existing copyright and third-party notices. Document the origin and
license of any added dependency or copied third-party source.

Original and community contributors are recognized in
[CONTRIBUTORS.md](CONTRIBUTORS.md). You may add your preferred name to the
community section when submitting a contribution.

Stable version tags and distribution publication require the
[release qualification process](docs/release/v1-profile.md#qualification-and-publication).
Ordinary source contributions do not change the product version or publish a
release.
