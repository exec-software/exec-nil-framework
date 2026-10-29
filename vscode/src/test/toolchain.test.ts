import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import test from "node:test";

import {
  expandConfiguredPath,
  resolveNyTool,
  toolExecutableName
} from "../toolchain";

test("framework SDK discovery uses canonical NIL executable names", () => {
  for (const tool of ["nilcompiler", "nilinterpreter", "nildebugger", "nillsp", "niltest"] as const) {
    const result = resolveNyTool({
      tool, sdkPath: "/sdk", workspaceFolders: [], environment: {}, platform: "linux",
      isExecutable: (candidate) => candidate === `/sdk/${tool}`
    });
    assert.equal(result.executable, `/sdk/${tool}`);
    assert.equal(result.source, "sdk");
    assert.equal(toolExecutableName(tool, "win32"), `${tool}.exe`);
  }
});

test("explicit executable paths have strict precedence", () => {
  const usable = new Set(["/configured/nillsp", "/workspace/sdk/nillsp"]);
  const result = resolveNyTool({
    tool: "nillsp",
    explicitPath: "/configured/nillsp",
    workspaceFolders: ["/workspace"],
    environment: { PATH: "/usr/bin" },
    platform: "linux",
    isExecutable: (candidate) => usable.has(candidate)
  });
  assert.equal(result.executable, "/configured/nillsp");
  assert.equal(result.source, "explicit");
});

test("a broken explicit setting is reported instead of silently ignored", () => {
  const result = resolveNyTool({
    tool: "nilcompiler",
    explicitPath: "/missing/nilcompiler",
    workspaceFolders: ["/workspace"],
    environment: { PATH: "/usr/bin" },
    platform: "linux",
    isExecutable: (candidate) => candidate === "/workspace/sdk/nilcompiler"
  });
  assert.equal(result.executable, undefined);
  assert.match(result.error ?? "", /Configured nilcompiler path/);
  assert.deepEqual(result.attempted, ["/missing/nilcompiler"]);
});

test("an SDK setting accepts canonical SDK and repository roots", () => {
  const installed = resolveNyTool({
    tool: "nilinterpreter",
    sdkPath: "/opt/ny",
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => candidate === "/opt/ny/nilinterpreter"
  });
  assert.equal(installed.executable, "/opt/ny/nilinterpreter");
  assert.equal(installed.source, "sdk");

  const repository = resolveNyTool({
    tool: "nilinterpreter",
    sdkPath: "/src/ny",
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => candidate === "/src/ny/sdk/nilinterpreter"
  });
  assert.equal(repository.executable, "/src/ny/sdk/nilinterpreter");
});

test("canonical SDK roots precede compatibility bin directories", () => {
  const usable = new Set([
    "/opt/ny/nilcompiler",
    "/opt/ny/bin/nilcompiler"
  ]);
  const result = resolveNyTool({
    tool: "nilcompiler",
    sdkPath: "/opt/ny",
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => usable.has(candidate)
  });
  assert.equal(result.executable, "/opt/ny/nilcompiler");
  assert.deepEqual(result.attempted, [
    "/opt/ny/nilcompiler",
    "/opt/ny/sdk/nilcompiler",
    "/opt/ny/build/cmake/bin/nilcompiler",
    "/opt/ny/build/sdk/nilcompiler",
    "/opt/ny/bin/nilcompiler",
    "/opt/ny/sdk/bin/nilcompiler",
    "/opt/ny/build/sdk/bin/nilcompiler"
  ]);
});

test("legacy SDK bin directories remain a last-resort compatibility fallback", () => {
  const result = resolveNyTool({
    tool: "nilcompiler",
    sdkPath: "/opt/legacy-ny",
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => candidate === "/opt/legacy-ny/bin/nilcompiler"
  });
  assert.equal(result.executable, "/opt/legacy-ny/bin/nilcompiler");
  assert.equal(result.source, "sdk");
});

test("workspace SDK outputs precede developer builds and PATH", () => {
  const usable = new Set([
    "/work/sdk/nilcompiler",
    "/work/build/sdk/nilcompiler",
    "/work/build/cmake/bin/nilcompiler",
    "/usr/local/bin/nilcompiler"
  ]);
  const result = resolveNyTool({
    tool: "nilcompiler",
    workspaceFolders: ["/work"],
    platform: "linux",
    environment: { PATH: "/usr/local/bin:/usr/bin" },
    isExecutable: (candidate) => usable.has(candidate)
  });
  assert.equal(result.executable, "/work/sdk/nilcompiler");
  assert.equal(result.source, "workspace");
});

test("configured checkout prefers root sdk and retains build/sdk compatibility", () => {
  for (const published of [true, false]) {
    const result = resolveNyTool({
      tool: "nillsp", sdkPath: "/framework", environment: {}, platform: "linux",
      isExecutable: (candidate) => candidate === "/framework/build/sdk/nillsp" ||
        (published && candidate === "/framework/sdk/nillsp")
    });
    assert.equal(result.executable, published ? "/framework/sdk/nillsp" : "/framework/build/sdk/nillsp");
  }
});

test("flat source component directories cannot shadow SDK executables", () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "nil-extension-discovery-"));
  try {
    fs.mkdirSync(path.join(root, "nilcompiler"));
    fs.mkdirSync(path.join(root, "sdk"));
    const executable = path.join(root, "sdk", toolExecutableName("nilcompiler", process.platform));
    fs.writeFileSync(executable, "#!/bin/sh\nexit 0\n", { mode: 0o700 });
    const result = resolveNyTool({ tool: "nilcompiler", sdkPath: root, environment: {} });
    assert.equal(result.executable, executable);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
});

test("a project-local SDK at workspace/sdk is discovered", () => {
  const result = resolveNyTool({
    tool: "nillsp",
    workspaceFolders: ["/work/example"],
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => candidate === "/work/example/sdk/nillsp"
  });
  assert.equal(result.executable, "/work/example/sdk/nillsp");
  assert.equal(result.source, "workspace");
});

test("PATH lookup uses platform conventions", () => {
  const result = resolveNyTool({
    tool: "nillsp",
    platform: "win32",
    environment: { Path: "C:\\Ny\\bin;C:\\Tools" },
    isExecutable: (candidate) => candidate === "C:\\Ny\\bin\\nillsp.exe"
  });
  assert.equal(toolExecutableName("nillsp", "win32"), "nillsp.exe");
  assert.equal(result.executable, "C:\\Ny\\bin\\nillsp.exe");
  assert.equal(result.source, "path");
});

test("an explicit Windows command name follows PATHEXT", () => {
  const result = resolveNyTool({
    tool: "nilcompiler",
    explicitPath: "nilcompiler",
    platform: "win32",
    environment: {
      Path: "C:\\Ny\\bin;C:\\Tools",
      PATHEXT: ".EXE;.CMD"
    },
    isExecutable: (candidate) => candidate === "C:\\Ny\\bin\\nilcompiler.EXE"
  });
  assert.equal(result.executable, "C:\\Ny\\bin\\nilcompiler.EXE");
  assert.equal(result.source, "explicit");
});

test("configured paths expand workspace, environment, and home variables", () => {
  assert.equal(
    expandConfiguredPath("${workspaceFolder}/build/${env:PROFILE}", "/work/ny", { PROFILE: "debug" }, "/home/a"),
    "/work/ny/build/debug"
  );
  assert.equal(expandConfiguredPath("~/sdk", undefined, {}, "/home/a"), "/home/a/sdk");
});

test("the source debugger is resolved at the canonical SDK root", () => {
  const result = resolveNyTool({
    tool: "nildebugger",
    sdkPath: "/sdk",
    platform: "linux",
    environment: {},
    isExecutable: (candidate) => candidate === "/sdk/nildebugger"
  });
  assert.equal(result.executable, "/sdk/nildebugger");
  assert.equal(result.source, "sdk");
});
