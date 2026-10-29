// Run against an installed VS Code. No downloaded test runner or extension is
// needed; the development extension and all tool settings live in isolation.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawn } = require("node:child_process");

const extensionRoot = path.resolve(__dirname, "..");
const repository = path.resolve(extensionRoot, "..");
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "ny-vscode-host-"));
const workspace = path.join(temporary, "workspace");
const reportDirectory = path.join(repository, "build", "vscode-host-smoke");
fs.mkdirSync(path.join(workspace, ".vscode"), { recursive: true });
fs.mkdirSync(reportDirectory, { recursive: true });
const report = path.join(reportDirectory, "latest.json");
fs.rmSync(report, { force: true });
const tools = process.env.NIL_VSCODE_TOOL_DIRECTORY || process.env.NY_VSCODE_TOOL_DIRECTORY || path.join(repository, "build", "cmake", "bin");
const settings = {
  "ny.sdkPath": process.env.NIL_VSCODE_SDK_PATH || repository,
  "ny.languageServer.enabled": true,
  "ny.languageServer.diagnostics.enabled": true,
  "security.workspace.trust.enabled": false,
  "files.autoSave": "off",
  "extensions.autoUpdate": false,
  "extensions.autoCheckUpdates": false,
  "update.mode": "none",
  "telemetry.telemetryLevel": "off"
};
for (const [setting, tool] of Object.entries({
  server: "nillsp", compiler: "nilcompiler", interpreter: "nilinterpreter", debugger: "nildebugger", projectTool: "nilbuild", testTool: "niltest", aperture: "aperture"
})) {
  const executable = path.join(tools, tool + (process.platform === "win32" ? ".exe" : ""));
  fs.accessSync(executable, fs.constants.X_OK);
  settings[`ny.${setting}.path`] = executable;
}
fs.writeFileSync(path.join(workspace, ".vscode", "settings.json"), JSON.stringify(settings, null, 2));
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
const env = { ...process.env, NIL_VSCODE_TEST_WORKSPACE: workspace, NIL_VSCODE_TEST_REPORT: report };
delete env.ELECTRON_RUN_AS_NODE;
const args = [
  "--new-window", "--wait", "--skip-welcome", "--skip-release-notes",
  "--disable-workspace-trust", "--disable-extensions", "--disable-gpu",
  `--user-data-dir=${path.join(temporary, "profile")}`,
  `--extensions-dir=${path.join(temporary, "extensions")}`,
  `--extensionDevelopmentPath=${extensionRoot}`,
  `--extensionTestsPath=${path.join(__dirname, "suite.cjs")}`,
  workspace
];
console.log(`VS Code host workspace: ${workspace}`);
const child = spawn(process.env.VSCODE_EXECUTABLE || "code", args, {
  env, stdio: "inherit", detached: process.platform !== "win32"
});
const timer = setTimeout(() => {
  console.error("VS Code extension-host smoke exceeded 180 seconds.");
  if (process.platform === "win32") child.kill();
  else { try { process.kill(-child.pid, "SIGTERM"); } catch {} }
}, 180_000);
child.on("error", (error) => { clearTimeout(timer); console.error(error); process.exitCode = 1; });
child.on("exit", (code) => {
  clearTimeout(timer);
  let result;
  try { result = JSON.parse(fs.readFileSync(report, "utf8")); } catch {}
  console.log(`VS Code host report: ${report}`);
  console.log(`Isolated profile and logs retained: ${temporary}`);
  if (code !== 0 || result?.status !== "PASS") {
    console.error(result?.error || `Host exited ${code} without a passing report.`);
    process.exitCode = 1;
  } else {
    console.log(`PASS: ${result.checks.length} real extension-host checks.`);
  }
});
