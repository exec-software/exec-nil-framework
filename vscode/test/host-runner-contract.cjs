// Run manually against the CMake test-only helper, with no editor dependency:
// node test/host-runner-contract.cjs /absolute/path/nil_vscode_host_runner[.exe]
const assert = require("node:assert/strict");
const path = require("node:path");
const { execFile } = require("node:child_process");
const { promisify } = require("node:util");
const run = promisify(execFile);
const runner = path.resolve(process.argv[2] || "");
if (!process.argv[2]) throw new Error("Pass the test-only nil_vscode_host_runner path.");

(async () => {
  const invoke = (milliseconds, program, ...args) => run(runner,
    ["--timeout-ms", String(milliseconds), "--", program, ...args], { timeout: milliseconds + 5000, maxBuffer: 65536 });
  const expected = ["", "café 中文 spaces", 'a"b', "trailing\\", "& | %PATH% ! ^"];
  const argumentsResult = await invoke(5000, process.execPath, "-e", "process.stdout.write(JSON.stringify(process.argv.slice(1)))", ...expected);
  assert.deepEqual(JSON.parse(argumentsResult.stdout), expected);
  await assert.rejects(invoke(5000, process.execPath, "-e", "process.exit(19)"), error => error.code === 19);
  const start = Date.now();
  await assert.rejects(invoke(150, process.execPath, "-e", "setInterval(() => {}, 1000)"), error => error.code === 124 && /timed out/.test(error.stderr));
  assert.ok(Date.now() - start < 5000, "Timeout and verified cleanup must remain bounded.");
  await assert.rejects(invoke(5000, path.join(path.dirname(runner), "does-not-exist")), error => error.code === 125);
  if (process.platform === "win32") {
    // The launcher may exit, but a surviving ordinary descendant is not PASS.
    await assert.rejects(invoke(5000, process.execPath, "-e",
      'require("node:child_process").spawn(process.execPath, ["-e", "setInterval(() => {}, 1000)"], {stdio:"ignore",detached:true}).unref()'),
    error => error.code === 125 && /descendant/.test(error.stderr));
  }
  console.log("PASS: host runner preserves argv/exit status and bounds timeout, launch failure, and owned cleanup.");
})().catch(error => { console.error(error); process.exitCode = 1; });
