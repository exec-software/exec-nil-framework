// Small, dependency-free helpers shared by the real host gate and its unit tests.
const fs = require("node:fs");
const path = require("node:path");

function resolveEditor(requested, { platform = process.platform, env = process.env,
  isFile = file => { try { return fs.statSync(file).isFile(); } catch { return false; } }
} = {}) {
  if (platform !== "win32") return requested || "code";
  const editor = requested || "code";
  const windows = path.win32;
  const searchPath = Object.entries(env).find(([key]) => key.toLowerCase() === "path")?.[1] || "";
  const qualified = windows.isAbsolute(editor) || /[\\/]/.test(editor);
  const names = windows.extname(editor) ? [editor] : [`${editor}.exe`, `${editor}.cmd`];
  const candidates = qualified ? names : searchPath.split(";").filter(Boolean)
    .flatMap(directory => names.map(name => windows.join(directory.replace(/^"|"$/g, ""), name)));
  const selected = candidates.find(isFile);
  if (!selected) throw new Error(`Cannot find VS Code: ${editor}. Set VSCODE_EXECUTABLE to the installed Code.exe.`);
  if (/\.exe$/i.test(selected)) return selected;
  // Never run a command shell or interpolate test paths into a batch command.
  // The official bin/code.cmd shim points at the sibling desktop executable.
  const shim = windows.basename(selected).toLowerCase();
  const executable = shim === "code.cmd" ? "Code.exe" : shim === "code-insiders.cmd" ? "Code - Insiders.exe" : undefined;
  if (executable && windows.basename(windows.dirname(selected)).toLowerCase() === "bin") {
    const desktop = windows.join(windows.dirname(selected), "..", executable);
    if (isFile(desktop)) return desktop;
  }
  throw new Error(`Unsupported VS Code launcher: ${selected}. Set VSCODE_EXECUTABLE to the installed Code.exe, not a shell script.`);
}

function hostTimeout(value) {
  if (value === undefined) return 600_000;
  if (!/^[0-9]+$/.test(value)) throw new Error("NIL_VSCODE_HOST_TIMEOUT_MS must be an integer from 1000 to 1800000.");
  const timeout = Number(value);
  if (!Number.isSafeInteger(timeout) || timeout < 1000 || timeout > 1_800_000)
    throw new Error("NIL_VSCODE_HOST_TIMEOUT_MS must be an integer from 1000 to 1800000.");
  return timeout;
}

function writeIsolatedProfileSettings(profile) {
  if (!path.isAbsolute(profile)) throw new Error("The isolated editor profile must be an absolute path.");
  const user = path.join(profile, "User");
  fs.mkdirSync(user, { recursive: true });
  const filename = path.join(user, "settings.json");
  // Application-scoped settings are ignored in workspace settings. Write the
  // disposable --user-data-dir before the editor starts, never a user profile.
  fs.writeFileSync(filename, JSON.stringify({
    "security.workspace.trust.enabled": false,
    "extensions.autoUpdate": false,
    "extensions.autoCheckUpdates": false,
    "update.mode": "none",
    "telemetry.telemetryLevel": "off"
  }, null, 2), { flag: "wx" });
  return filename;
}

function validateReport(result, { runId, workspace, code, signal, timedOut }) {
  if (timedOut || code !== 0 || signal)
    throw new Error(`Host did not complete successfully (exit ${code}, signal ${signal || "none"}, timeout ${Boolean(timedOut)}).`);
  if (result?.runId !== runId || result?.workspace !== workspace)
    throw new Error("Host report does not belong to this isolated run.");
  if (result.status !== "PASS") throw new Error(result.error || "Host report did not pass.");
  if (!Array.isArray(result.checks) || result.checks.length === 0 ||
      result.checks.some(check => typeof check.name !== "string" || check.status !== "PASS"))
    throw new Error("Host report must contain nonempty passing checks.");
  return result;
}

async function sameFile(left, right) {
  // Source producers may normalize slashes or the drive letter differently.
  // Compare real file identity, not lowercase spelling (Windows supports
  // case-sensitive directories too). Never mask a genuinely different file.
  const [a, b] = await Promise.all([fs.promises.stat(left, { bigint: true }), fs.promises.stat(right, { bigint: true })]);
  if (a.ino !== 0n && b.ino !== 0n) return a.dev === b.dev && a.ino === b.ino;
  const [aPath, bPath] = await Promise.all([fs.promises.realpath(left), fs.promises.realpath(right)]);
  return aPath === bPath;
}

module.exports = { resolveEditor, hostTimeout, writeIsolatedProfileSettings, validateReport, sameFile };
