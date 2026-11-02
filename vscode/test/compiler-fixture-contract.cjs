// Windows-only, no editor dependency:
// node test/compiler-fixture-contract.cjs ABSOLUTE_NIL_VSCODE_COMPILER_FIXTURE_EXE
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const syncFs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFile } = require("node:child_process");
const { createCompilerFixture } = require("./compiler-fixture-support.cjs");
assert.equal(process.platform, "win32", "This contract exercises the native Windows fixture adapter.");
assert.ok(process.argv[2] && path.isAbsolute(process.argv[2]), "Pass an absolute fixture adapter executable.");
const runner = process.argv[2];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function eventually(operation, description) {
  const deadline = Date.now() + 5000;
  do { if (await operation()) return; await pause(20); } while (Date.now() < deadline);
  throw new Error(`Timed out: ${description}`);
}
async function gone(pid) {
  await eventually(() => {
    try { process.kill(pid, 0); return false; }
    catch (error) { if (error.code === "ESRCH") return true; throw error; }
  }, `owned fixture child ${pid} exited`);
}

(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil fixture contract café 中文-"));
  console.log(`Fixture contract workspace: ${directory}`);
  let passed = false;
  const env = { ...process.env, NIL_VSCODE_FIXTURE_NODE: process.execPath };
  delete env.NIL_VSCODE_FIXTURE_TIMEOUT_MS;
  const fixture = (name, body) => createCompilerFixture(path.join(directory, `${name}.cjs`), body,
    { runner, node: process.execPath });
  const invoke = (program, args = [], input = Buffer.alloc(0), options = {}) => new Promise((resolve, reject) => {
    const child = execFile(program, args, { env, encoding: "buffer", timeout: 7000,
      maxBuffer: 1024 * 1024, windowsHide: true, ...options }, (error, stdout, stderr) => {
      if (error) { error.stdout = stdout; error.stderr = stderr; reject(error); }
      else resolve({ stdout, stderr });
    });
    child.stdin.on("error", () => {});
    child.stdin.end(input);
  });
  try {
    const echo = await fixture("echo", `
const chunks = [];
process.stdin.on('data', chunk => chunks.push(chunk));
process.stdin.on('end', () => {
  process.stdout.write(JSON.stringify({args:process.argv.slice(2),input:Buffer.concat(chunks).toString('base64')}));
  process.stderr.write(Buffer.from([0,13,10,195,169,255]));
});`);
    const arguments_ = ["", "café 中文 spaces", 'a"b', "trailing\\", "& | %PATH% ! ^", "--analyze", "--workspace-stdin"];
    const input = Buffer.concat([Buffer.from('{"sources":["é😀\\r\\n"]}\r\n'), Buffer.alloc(256 * 1024, 0xab)]);
    const result = await invoke(echo, arguments_, input);
    assert.deepEqual(JSON.parse(result.stdout), { args: arguments_, input: input.toString("base64") });
    assert.deepEqual(result.stderr, Buffer.from([0,13,10,195,169,255]));
    const exit = await fixture("exit", "process.exit(19);");
    await assert.rejects(invoke(exit), error => error.code === 19);
    for (const invalid of ["", "node.exe", path.join(directory, "missing-node.exe")])
      await assert.rejects(invoke(exit, [], undefined, { env: { ...env, NIL_VSCODE_FIXTURE_NODE: invalid } }),
        error => error.code === 125 && /NIL_VSCODE_FIXTURE_NODE/.test(error.stderr));
    const missing = path.join(directory, "missing-script.exe");
    await fs.copyFile(runner, missing);
    await assert.rejects(invoke(missing), error => error.code === 125 && /Adjacent/.test(error.stderr));
    for (const invalid of ["0", "49", "15001", "1e3", "NaN", "1000x"])
      await assert.rejects(invoke(exit, [], undefined, { env: { ...env, NIL_VSCODE_FIXTURE_TIMEOUT_MS: invalid } }),
        error => error.code === 125);

    const pidFile = path.join(directory, "running.json");
    const waiting = await fixture("waiting", `
require('node:fs').writeFileSync(${JSON.stringify(pidFile)}, JSON.stringify({pid:process.pid}));
setInterval(() => {}, 1000);`);
    const start = Date.now();
    await assert.rejects(invoke(waiting, [], undefined, { env: { ...env, NIL_VSCODE_FIXTURE_TIMEOUT_MS: "500" } }),
      error => error.code === 124 && /timed out/.test(error.stderr));
    assert.ok(Date.now() - start < 6500);
    await gone(JSON.parse(await fs.readFile(pidFile, "utf8")).pid);

    await fs.unlink(pidFile);
    const controller = new AbortController();
    let callbackError;
    const cancelled = execFile(waiting, [], { env, signal: controller.signal, timeout: 7000, windowsHide: true },
      error => { callbackError = error; });
    cancelled.stdin.end();
    const closed = new Promise(resolve => cancelled.once("close", resolve));
    try {
      await eventually(async () => {
        try { JSON.parse(await fs.readFile(pidFile, "utf8")); return true; }
        catch (error) { if (error.code === "ENOENT" || error instanceof SyntaxError) return false; throw error; }
      }, "cancellation child starts");
      const cancelledPid = JSON.parse(await fs.readFile(pidFile, "utf8")).pid;
      controller.abort(); // Retained ChildProcess ownership; never signal a PID.
      await closed;
      assert.equal(callbackError.code, "ABORT_ERR");
      await gone(cancelledPid);
    } finally { controller.abort(); await closed; }

    const descendantFile = path.join(directory, "descendant.json");
    const descendant = await fixture("descendant", `
const child = require('node:child_process').spawn(process.execPath, ['-e','setInterval(() => {}, 1000)'], {stdio:'ignore',detached:true});
require('node:fs').writeFileSync(${JSON.stringify(descendantFile)}, JSON.stringify({pid:child.pid}));
child.unref();`);
    await assert.rejects(invoke(descendant), error => error.code === 125 && /descendants/.test(error.stderr));
    await gone(JSON.parse(await fs.readFile(descendantFile, "utf8")).pid);
    const historical = await fixture("historical-descendant", `
const result = require('node:child_process').spawnSync(process.execPath, ['-e','process.exit(0)'], {stdio:'ignore',timeout:3000});
if (result.error || result.status !== 0) process.exit(99);`);
    await assert.rejects(invoke(historical),
      error => error.code === 125 && /unretained process lifetime/.test(error.stderr));

    const rundown = await fixture("resource-rundown", `
const fs = require('node:fs');
const [cwd, ready, release] = process.argv.slice(2);
const child = require('node:child_process').spawn(process.execPath, ['-e','setInterval(() => {}, 1000)'],
  {cwd,stdio:'ignore',detached:true});
child.unref();
fs.writeFileSync(ready, JSON.stringify({pid:child.pid}));
setInterval(() => { if (fs.existsSync(release)) process.exit(0); }, 10);`);
    for (let index = 0; index < 20; ++index) {
      const resource = path.join(directory, `held cwd ${index}`);
      const ready = path.join(directory, `resource-ready-${index}.json`);
      const release = path.join(directory, `resource-release-${index}.flag`);
      await fs.mkdir(resource);
      const completion = assert.rejects(invoke(rundown, [resource, ready, release]),
        error => error.code === 125 && /descendants/.test(error.stderr));
      // Observe early failures below without an unhandled rejection while the
      // independent readiness/resource-positive control is running.
      completion.catch(() => {});
      try {
        await eventually(async () => {
          try { JSON.parse(await fs.readFile(ready, "utf8")); return true; }
          catch (error) { if (error.code === "ENOENT" || error instanceof SyntaxError) return false; throw error; }
        }, "descendant owns its current directory");
        assert.throws(() => syncFs.rmdirSync(resource),
          error => ["EBUSY", "EPERM", "EACCES"].includes(error.code),
          "Positive control: a running descendant must actually hold this directory open.");
      } finally { await fs.writeFile(release, "parent may exit"); }
      await completion;
      // Deliberately immediate: no sleep, retry, or PID liveness poll may hide
      // Windows process resource rundown after the Job active count hits zero.
      syncFs.rmdirSync(resource);
      await gone(JSON.parse(await fs.readFile(ready, "utf8")).pid);
    }

    const floodFile = path.join(directory, "flood.json");
    const flood = await fixture("flood", `
require('node:fs').writeFileSync(${JSON.stringify(floodFile)}, JSON.stringify({pid:process.pid}));
setInterval(() => process.stdout.write(Buffer.alloc(4096,97)), 1);`);
    await assert.rejects(invoke(flood, [], undefined, { maxBuffer: 1024 }),
      error => error.code === "ERR_CHILD_PROCESS_STDIO_MAXBUFFER" && error.stdout.length <= 1024);
    await gone(JSON.parse(await fs.readFile(floodFile, "utf8")).pid);
    passed = true;
    console.log("PASS: literal argv, binary stdin/stdout/stderr, exit status, invalid configuration, deadline, abort, descendants, historical-process refusal, 20 immediate CWD-release controls, and output-budget cleanup.");
  } finally {
    // Only remove our successful unique workspace. Failures retain their
    // scripts/PID observations for diagnosis, never a reused user directory.
    if (passed) await fs.rm(directory, { recursive: true, force: true });
    else console.error(`Failed fixture evidence retained: ${directory}`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
