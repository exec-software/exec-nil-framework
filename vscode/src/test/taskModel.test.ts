import assert from "node:assert/strict";
import test from "node:test";

import {
  compileInvocation,
  defaultArtifactPath,
  interpreterInvocation
} from "../taskModel";

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
