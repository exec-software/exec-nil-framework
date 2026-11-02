// Real-host test infrastructure only; never installed in the SDK/extension.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

function sameExistingFile(left, right) {
  try {
    const a = fs.statSync(left, { bigint: true }), b = fs.statSync(right, { bigint: true });
    assert.notEqual(a.ino, 0n, "Fixture filesystem must expose stable file identity.");
    return a.dev === b.dev && a.ino === b.ino;
  } catch (error) { if (error.code === "ENOENT") return false; throw error; }
}

function canonicalSnapshot(snapshot) {
  // Real compiler semantic/completion/signature snapshots use canonical disk
  // identities, not the editor's URI spelling (notably c: versus C: on Windows).
  // Keep the captured buffer byte-for-byte and never read replacement text.
  // The non-native sync implementation can preserve the caller's drive-letter
  // case. Match fs.promises.realpath, used by the editor's source identities.
  return { ...snapshot, path: fs.realpathSync.native(snapshot.path) };
}

async function createCompilerFixture(script, body, options = {}) {
  assert.ok(path.isAbsolute(script) && script.endsWith(".cjs"));
  const platform = options.platform || process.platform;
  let executable = script;
  if (platform === "win32") {
    const runner = options.runner || process.env.NIL_VSCODE_FIXTURE_RUNNER ||
      (process.env.NIL_VSCODE_HOST_RUNNER && path.join(path.dirname(process.env.NIL_VSCODE_HOST_RUNNER), "nil_vscode_compiler_fixture.exe"));
    const node = options.node || process.env.NIL_VSCODE_FIXTURE_NODE;
    for (const [name, file] of [["NIL_VSCODE_FIXTURE_RUNNER", runner], ["NIL_VSCODE_FIXTURE_NODE", node]]) {
      assert.ok(file && path.isAbsolute(file) && fs.statSync(file).isFile(),
        `${name} must name an existing absolute test helper/Node executable; no shell or PATH fallback.`);
    }
    executable = script.slice(0, -4) + ".exe";
    await fs.promises.copyFile(runner, executable, fs.constants.COPYFILE_EXCL);
  }
  const prelude = `const { sameExistingFile, canonicalSnapshot } = require(${JSON.stringify(__filename)});\n`;
  await fs.promises.writeFile(script, (platform === "win32" ? "" : "#!/usr/bin/env node\n") + prelude + body,
    { mode: 0o755, flag: "wx" });
  return executable;
}

async function createFileAlias(target, directory) {
  assert.ok(path.isAbsolute(target) && path.isAbsolute(directory));
  const kind = process.platform === "win32" ? "junction" : "symlink";
  let alias, link;
  if (kind === "junction") {
    // Directory junctions need no symlink privilege. The editor still sees two
    // distinct file URIs resolving to the exact same canonical source object.
    link = directory;
    await fs.promises.symlink(path.dirname(target), link, "junction");
    alias = path.join(link, path.basename(target));
  } else {
    await fs.promises.mkdir(directory);
    link = alias = path.join(directory, path.basename(target));
    await fs.promises.symlink(target, alias, "file");
  }
  assert.equal((await fs.promises.lstat(link)).isSymbolicLink(), true, "Alias must be a real reparse/symbolic link.");
  assert.notEqual(path.resolve(alias), path.resolve(target));
  assert.equal(sameExistingFile(alias, target), true);
  assert.equal(await fs.promises.realpath(alias), await fs.promises.realpath(target));
  return { path: alias, kind };
}

module.exports = { createCompilerFixture, createFileAlias, sameExistingFile, canonicalSnapshot };
