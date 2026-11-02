const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { resolveEditor, hostTimeout, validateReport, sameFile } = require("./host-support.cjs");

function windows(files, env = {}) {
  const existing = new Set(files.map(file => path.win32.normalize(file).toLowerCase()));
  return { platform: "win32", env, isFile: file => existing.has(path.win32.normalize(file).toLowerCase()) };
}

test("Windows host resolves official code.cmd to Code.exe without a command shell", () => {
  const root = "C:\\Program Files\\VS Code café 中文";
  const options = windows([`${root}\\bin\\code.cmd`, `${root}\\Code.exe`], { Path: `${root}\\bin` });
  assert.equal(resolveEditor(undefined, options), `${root}\\Code.exe`);
  assert.equal(resolveEditor(`${root}\\bin\\code.cmd`, options), `${root}\\Code.exe`);
  assert.equal(resolveEditor(`${root}\\Code.exe`, options), `${root}\\Code.exe`);
  assert.equal(resolveEditor("code", windows(["D:\\editor\\code.exe"], { PATH: '"D:\\editor"' })), "D:\\editor\\code.exe");
});

test("Windows host resolves Insiders and fails closed for missing/nonstandard scripts", () => {
  assert.equal(resolveEditor("code-insiders", windows([
    "C:\\editor\\bin\\code-insiders.cmd", "C:\\editor\\Code - Insiders.exe"
  ], { PATH: "C:\\editor\\bin" })), "C:\\editor\\Code - Insiders.exe");
  assert.throws(() => resolveEditor("C:\\unsafe & name\\anything.cmd", windows(["C:\\unsafe & name\\anything.cmd"])), /Unsupported/);
  assert.throws(() => resolveEditor("code", windows(["C:\\editor\\bin\\code.cmd"], { Path: "C:\\editor\\bin" })), /Unsupported/);
  assert.throws(() => resolveEditor(undefined, windows([])), /Cannot find/);
  assert.equal(resolveEditor(undefined, { platform: "linux" }), "code");
});

test("host deadline is finite, explicit, and rejects ambiguous values", () => {
  assert.equal(hostTimeout(undefined), 600000);
  assert.equal(hostTimeout("1000"), 1000);
  assert.equal(hostTimeout("1800000"), 1800000);
  for (const value of ["", "0", "999", "1800001", "NaN", "1000x", "1e6", "-1000", "Infinity", " 1000"])
    assert.throws(() => hostTimeout(value), /must be an integer/);
});

test("host report binds the run and workspace, and never accepts a timed-out PASS", () => {
  const report = { status: "PASS", runId: "one", workspace: "isolated", checks: [{ name: "real check", status: "PASS" }] };
  const launch = { runId: "one", workspace: "isolated", code: 0, signal: null, timedOut: false };
  assert.equal(validateReport(report, launch), report);
  for (const override of [{ code: 1 }, { code: null }, { signal: "SIGTERM" }, { timedOut: true },
    { runId: "stale" }, { workspace: "another" }]) assert.throws(() => validateReport(report, { ...launch, ...override }));
  for (const override of [{ status: "FAIL" }, { checks: [] }, { checks: [{}] }, { checks: [{ name: "skip", status: "SKIP" }] }])
    assert.throws(() => validateReport({ ...report, ...override }, launch));
  assert.throws(() => validateReport(undefined, launch));
});

test("source paths compare existing file identity, not superficial spelling or contents", async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "nil host identity café 中文-"));
  try {
    const source = path.join(directory, "source.matrix");
    const alias = path.join(directory, "hardlink.matrix");
    const different = path.join(directory, "different.matrix");
    fs.writeFileSync(source, "identical bytes");
    fs.linkSync(source, alias);
    fs.writeFileSync(different, "identical bytes");
    assert.equal(await sameFile(source, alias), true);
    assert.equal(await sameFile(source, different), false);
    if (process.platform === "win32") assert.equal(await sameFile(source, source.replaceAll("\\", "/")), true);
    await assert.rejects(sameFile(source, path.join(directory, "missing.matrix")), { code: "ENOENT" });
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});
