import assert from "node:assert/strict";
import test from "node:test";

import {
  compileInvocation,
  defaultArtifactPath,
  interpreterInvocation,
  sdkBuildInvocation
} from "../taskModel";

test("Windows SDK build selects PowerShell with literal Unicode paths and a configured build", () => {
  const root = "C:\\work café 中文 & %PATH%\\NIL [main]";
  const build = "C:\\build café 中文 & %PATH%\\configured [Release]";
  const powershell = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
  const script = root + "\\scripts\\build-sdk.ps1";
  assert.deepEqual(sdkBuildInvocation(root, build, "win32", powershell), {
    command: powershell,
    args: ["-NoLogo", "-NoProfile", "-NonInteractive", "-File", script, "-BuildDirectory", build],
    script,
    buildDirectory: build
  });
});

test("Windows SDK builds resolve workspace-relative and UNC build directories without a shell", () => {
  const relative = sdkBuildInvocation("C:\\repo", "build\\cmake", "win32");
  assert.equal(relative.buildDirectory, "C:\\repo\\build\\cmake");
  assert.equal(relative.command, "powershell.exe");
  const unc = "\\\\builder\\shared SDK café\\configured";
  assert.equal(sdkBuildInvocation("C:\\repo", unc, "win32").buildDirectory, unc);
  for (const forbidden of ["-Command", "-EncodedCommand", "-ExecutionPolicy", "-AllowDirtySource"]) {
    assert.equal(relative.args.includes(forbidden), false);
  }
  assert.equal(relative.args.some(arg => arg.includes("build-sdk.sh")), false);
});

test("POSIX SDK builds retain the existing environment-driven install workflow", () => {
  for (const platform of ["linux", "darwin"] as const) {
    const script = "/work/café 中文 & $literal/scripts/build-sdk.sh";
    assert.deepEqual(sdkBuildInvocation("/work/café 中文 & $literal", "custom/cmake", platform), {
      command: script, args: [], script
    });
  }
});

test("NIL compilation is deterministic and preserves configured arguments", () => {
  const invocation = compileInvocation({
    compiler: "/sdk/nilcompiler",
    source: "/work/main.ny",
    output: "/work/main.nil",
    mode: "nil",
    sdkPath: "/sdk",
    noStdlib: true,
    extraArgs: ["--quiet"]
  });
  assert.equal(invocation.command, "/sdk/nilcompiler");
  assert.deepEqual(invocation.args, [
    "--quiet",
    "--no-bundle",
    "--no-stdlib",
    "--sdk-path",
    "/sdk",
    "--output",
    "/work/main.nil",
    "/work/main.ny"
  ]);
});

test("native compilation selects LLVM without NIL bundling flags", () => {
  const invocation = compileInvocation({
    compiler: "nilcompiler",
    source: "main.ny",
    output: "main",
    mode: "native"
  });
  assert.deepEqual(invocation.args, ["--native", "--output", "main", "main.ny"]);
  assert.equal(invocation.args.includes("--no-bundle"), false);
});

test("Windows source and interpreter arguments retain Unicode, spaces, and shell metacharacters literally", () => {
  const source = "C:\\work café 中文 & %PATH%\\page.mat";
  const output = "C:\\work café 中文 & %PATH%\\page.nil";
  const compiler = "C:\\SDK tools\\nilcompiler.exe";
  assert.deepEqual(compileInvocation({ compiler, source, output, mode: "nil" }), {
    command: compiler, args: ["--no-bundle", "--output", output, source]
  });
  assert.deepEqual(interpreterInvocation("C:\\SDK tools\\nilinterpreter.exe", output, [], ["", "café 中文", "& | %PATH%"]), {
    command: "C:\\SDK tools\\nilinterpreter.exe", args: [output, "--", "", "café 中文", "& | %PATH%"]
  });
});

test("interpreter invocation separates host options from Ny program arguments", () => {
  assert.deepEqual(
    interpreterInvocation(
      "nilinterpreter",
      "main.nil",
      ["--memory-mode=safe"],
      ["--region", "west"]
    ),
    {
    command: "nilinterpreter",
      args: ["--memory-mode=safe", "main.nil", "--", "--region", "west"]
    }
  );
});

test("default artifacts are derived beside source files", () => {
  assert.equal(defaultArtifactPath("/work/main.ny", "nil", undefined, "linux"), "/work/main.nil");
  assert.equal(defaultArtifactPath("C:\\work\\main.ny", "native", undefined, "win32"), "C:\\work\\main.exe");
  assert.equal(
    defaultArtifactPath("${file}", "nil", undefined, "linux"),
    "${fileDirname}/${fileBasenameNoExtension}.nil"
  );
});

test("source debugging opts into maps without shell interpretation", () => {
  const invocation = compileInvocation({
    compiler: "/sdk/nilcompiler",
    source: "/work/with spaces;$(echo nope).ny",
    output: "/tmp/ny-debug-owned/program.nil",
    mode: "nil",
    extraArgs: ["--debug-info"]
  });
  assert.deepEqual(invocation.args, [
    "--debug-info", "--no-bundle", "--output",
    "/tmp/ny-debug-owned/program.nil", "/work/with spaces;$(echo nope).ny"
  ]);
});

test("Matrix source uses the same compiler for NIL, native, and debugger artifacts", () => {
  for (const source of ["/work/page.matrix", "/work/page.mat", "/work/with spaces;$(echo nope).MAT"]) {
    assert.deepEqual(compileInvocation({
      compiler: "/sdk/nilcompiler", source, output: "/work/page.nil", mode: "nil", debugInfo: true
    }).args, ["--no-bundle", "--debug-info", "--output", "/work/page.nil", source]);
    assert.deepEqual(compileInvocation({
      compiler: "/sdk/nilcompiler", source, output: "/work/page", mode: "native"
    }).args, ["--native", "--output", "/work/page", source]);
    assert.equal(defaultArtifactPath(source, "nil", undefined, "linux"), source.replace(/\.[^.]+$/, ".nil"));
  }
});
