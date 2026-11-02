const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { execFile } = require("node:child_process");
const { promisify } = require("node:util");
const { createCompilerFixture, createFileAlias, sameExistingFile, canonicalSnapshot } = require("./compiler-fixture-support.cjs");
const run = promisify(execFile);

test("compiler fixture factory retains portable source and fails closed without explicit Windows prerequisites", async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil fixture unit café 中文-"));
  try {
    const source = "process.stdout.write(JSON.stringify(process.argv.slice(2)));\n";
    const posix = await createCompilerFixture(path.join(directory, "posix.cjs"), source, { platform: "linux" });
    assert.match(await fs.readFile(posix, "utf8"), /^#!\/usr\/bin\/env node\n/);
    const args = ["", "café 中文", 'quotes"', "& | %PATH% !", "trailing\\"];
    const result = await run(process.execPath, [posix, ...args], { timeout: 5000 });
    assert.deepEqual(JSON.parse(result.stdout), args);
    await assert.rejects(createCompilerFixture(path.join(directory, "missing.cjs"), source,
      { platform: "win32", runner: "relative.exe", node: process.execPath }), /NIL_VSCODE_FIXTURE_RUNNER/);
    await assert.rejects(createCompilerFixture(path.join(directory, "node.cjs"), source,
      { platform: "win32", runner: process.execPath, node: "relative.exe" }), /NIL_VSCODE_FIXTURE_NODE/);
    // A tiny existing file stands in for the native adapter for factory-only
    // unit checks; compiler-fixture-contract.cjs executes the genuine adapter.
    const placeholder = path.join(directory, "adapter.exe");
    await fs.writeFile(placeholder, "test-only adapter placeholder");
    const windows = await createCompilerFixture(path.join(directory, "windows.cjs"), source,
      { platform: "win32", runner: placeholder, node: process.execPath });
    assert.equal(path.extname(windows), ".exe");
    assert.equal((await fs.readFile(path.join(directory, "windows.cjs"), "utf8")).startsWith("#!"), false);
    assert.equal((await fs.stat(windows)).size, (await fs.stat(placeholder)).size);
    await assert.rejects(createCompilerFixture(path.join(directory, "windows.cjs"), "overwrite",
      { platform: "win32", runner: placeholder, node: process.execPath }), { code: "EEXIST" });
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test("fixture aliases are real symbolic/reparse links to one file, not duplicate bytes", async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil alias unit café 中文-"));
  try {
    const sources = path.join(directory, "sources");
    await fs.mkdir(sources);
    const source = path.join(sources, "header.inc"), other = path.join(sources, "other.inc");
    await fs.writeFile(source, "same bytes");
    await fs.writeFile(other, "same bytes");
    const alias = await createFileAlias(source, path.join(directory, "alias"));
    assert.equal(alias.kind, process.platform === "win32" ? "junction" : "symlink");
    assert.equal(sameExistingFile(source, alias.path), true);
    assert.equal(sameExistingFile(source, other), false);
    assert.equal(sameExistingFile(source, path.join(directory, "missing")), false);
    if (process.platform === "win32") assert.equal(sameExistingFile(source, source.replaceAll("\\", "/")), true);
    await fs.writeFile(alias.path, "changed through alias");
    assert.equal(await fs.readFile(source, "utf8"), "changed through alias");
    await assert.rejects(createFileAlias(source, path.join(directory, "alias")), { code: "EEXIST" });
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test("compiler fixture response snapshots canonicalize saved paths without changing captured text", async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil snapshot unit café 中文-"));
  try {
    const sources = path.join(directory, "sources");
    await fs.mkdir(sources);
    const source = path.join(sources, "main.ny");
    await fs.writeFile(source, "different saved text\n");
    const alias = await createFileAlias(source, path.join(directory, "alias"));
    const text = "# é😀\r\nunsaved exact snapshot\r\n";
    const original = { path: alias.path, text };
    const result = canonicalSnapshot(original);
    assert.notEqual(result, original);
    assert.deepEqual(result, { path: await fs.realpath(source), text });
    assert.equal(original.path, alias.path, "Do not mutate the request/envelope path.");
    if (process.platform === "win32") {
      const alternate = source.replace(/^[A-Z](?=:)/, drive => drive.toLowerCase()).replaceAll("\\", "/");
      assert.deepEqual(canonicalSnapshot({ path: alternate, text }), result);
    }
    assert.throws(() => canonicalSnapshot({ path: path.join(directory, "missing.ny"), text }), { code: "ENOENT" });
    assert.equal(await fs.readFile(source, "utf8"), "different saved text\n");
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});
