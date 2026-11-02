// Run against an installed VS Code. No downloaded test runner or extension is
// needed; the development extension and all tool settings live in isolation.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { randomUUID } = require("node:crypto");
const { spawn } = require("node:child_process");
const { resolveEditor, hostTimeout, writeIsolatedProfileSettings, validateReport } = require("./host-support.cjs");

const extensionRoot = path.resolve(__dirname, "..");
const repository = path.resolve(extensionRoot, "..");
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "nil vscode host café 中文-"));
const workspace = path.join(temporary, "workspace");
const profile = path.join(temporary, "profile");
const reportDirectory = path.join(repository, "build", "vscode-host-smoke");
fs.mkdirSync(path.join(workspace, ".vscode"), { recursive: true });
fs.mkdirSync(reportDirectory, { recursive: true });
const runId = randomUUID();
const report = path.join(reportDirectory, `${runId}.json`);
const tools = path.resolve(process.env.NIL_VSCODE_TOOL_DIRECTORY || process.env.NY_VSCODE_TOOL_DIRECTORY || path.join(repository, "build", "cmake", "bin"));
const executable = resolveEditor(process.env.VSCODE_EXECUTABLE);
const timeout = hostTimeout(process.env.NIL_VSCODE_HOST_TIMEOUT_MS);
const runner = path.resolve(process.env.NIL_VSCODE_HOST_RUNNER || path.join(tools, "nil_vscode_host_runner" + (process.platform === "win32" ? ".exe" : "")));
let useRunner = false;
try { fs.accessSync(runner, fs.constants.X_OK); useRunner = fs.statSync(runner).isFile(); } catch {}
if (!useRunner && (process.platform === "win32" || process.env.NIL_VSCODE_HOST_RUNNER)) {
  throw new Error(`Missing test-only host runner: ${runner}. Build target nil_vscode_host_runner and set NIL_VSCODE_HOST_RUNNER; it is intentionally not part of the SDK.`);
}
const settings = {
  "ny.sdkPath": process.env.NIL_VSCODE_SDK_PATH || repository,
  "ny.languageServer.enabled": true,
  "ny.languageServer.diagnostics.enabled": true,
  "files.autoSave": "off"
};
for (const [setting, tool] of Object.entries({
  server: "nillsp", compiler: "nilcompiler", interpreter: "nilinterpreter", debugger: "nildebugger", projectTool: "nilbuild", testTool: "niltest", aperture: "aperture"
})) {
  const executable = path.join(tools, tool + (process.platform === "win32" ? ".exe" : ""));
  fs.accessSync(executable, fs.constants.X_OK);
  settings[`ny.${setting}.path`] = executable;
}
fs.writeFileSync(path.join(workspace, ".vscode", "settings.json"), JSON.stringify(settings, null, 2));
writeIsolatedProfileSettings(profile);
const fixture = (book) => `book ${book}

Point begin record
    field x as number
    field y as number
end record

main returns number begin paragraph
    value local-count as number is 7
    value sample-point as Point is build Point with x being 3, y being 4
    say local-count
    say sample-point.x
    give 0
end paragraph
`;
fs.writeFileSync(path.join(workspace, "editor.matrix"), fixture("editor.validation"));
fs.writeFileSync(path.join(workspace, "brief.mat"), fixture("brief.validation"));
fs.writeFileSync(path.join(workspace, "invalid.mat"),
  'book diagnostic.editor\nmain returns number begin paragraph\n    value count as number is "wrong"\n    give 0\nend paragraph\n');
fs.writeFileSync(path.join(workspace, "settings.nildata"), `nildata 1
object
  name is "NILData editor validation"
  enabled is true
  code is "book data.example {literal} # text"
  items is an array
    item is -1.6
    item is null
  end array
end object
`);
const env = { ...process.env, NIL_VSCODE_TEST_WORKSPACE: workspace, NIL_VSCODE_TEST_REPORT: report, NIL_VSCODE_TEST_RUN_ID: runId };
delete env.ELECTRON_RUN_AS_NODE;
// Do not accidentally attach to a user's existing desktop/IPC context.
delete env.VSCODE_IPC_HOOK_CLI;
const args = [
  "--new-window", "--wait", "--skip-welcome", "--skip-release-notes",
  "--disable-workspace-trust", "--disable-extensions", "--disable-gpu",
  `--user-data-dir=${profile}`,
  `--extensions-dir=${path.join(temporary, "extensions")}`,
  `--extensionDevelopmentPath=${extensionRoot}`,
  `--extensionTestsPath=${path.join(__dirname, "suite.cjs")}`,
  workspace
];
console.log(`VS Code host workspace: ${workspace}`);
console.log(`VS Code host report: ${report}`);
const child = spawn(useRunner ? runner : executable, useRunner ? ["--timeout-ms", String(timeout), "--", executable, ...args] : args, {
  env, stdio: "inherit", detached: process.platform !== "win32"
});
let timedOut = false;
let escalation;
// The native helper owns its deadline and confirms cleanup itself. The legacy
// POSIX fallback is for developers without the test helper, never Windows.
const timer = useRunner ? undefined : setTimeout(() => {
  timedOut = true;
  console.error(`VS Code extension-host smoke exceeded ${timeout} milliseconds.`);
  try { process.kill(-child.pid, "SIGTERM"); } catch {}
  escalation = setTimeout(() => {
    // Node retains ownership only until exit. Never signal a possibly reused
    // numeric process-group ID after the leader exit event has been collected.
    if (child.exitCode === null && child.signalCode === null) {
      try { process.kill(-child.pid, "SIGKILL"); } catch {}
    }
  }, 1000);
}, timeout);
child.on("error", (error) => { clearTimeout(timer); clearTimeout(escalation); console.error(error); process.exitCode = 1; });
child.on("exit", (code, signal) => {
  clearTimeout(timer);
  clearTimeout(escalation);
  let result;
  try { result = JSON.parse(fs.readFileSync(report, "utf8")); } catch {}
  console.log(`Isolated profile and logs retained: ${temporary}`);
  try {
    validateReport(result, { runId, workspace, code, signal, timedOut });
    // Per-run evidence is authoritative. This is only the most recent result
    // for convenient browsing; concurrent invocations never consume it.
    fs.copyFileSync(report, path.join(reportDirectory, "latest.json"));
    console.log(`PASS: ${result.checks.length} real extension-host checks; ${result.skippedChecks?.length || 0} explicitly recorded exclusions.`);
  } catch (error) {
    if (result?.error) console.error(result.error);
    console.error(error.stack || String(error));
    process.exitCode = 1;
  }
});
