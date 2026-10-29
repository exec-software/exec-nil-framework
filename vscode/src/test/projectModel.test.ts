import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import * as path from "node:path";
import * as fs from "node:fs";
import * as os from "node:os";
import test from "node:test";
import { findProjectFile, parseProject, parseProjectJson, projectInvocation, readProject } from "../projectModel";
import { resolveNyTool } from "../toolchain";

const root = path.resolve("project-tests");
const file = path.join(root, "nil.project.json");
const manifest = { schema: "nil.project.v1", name: "hello", entry: "src/main.matrix" };

test("project defaults resolve relative to its manifest independently of process cwd", () => {
  assert.deepEqual(parseProject(JSON.stringify(manifest), file), {
    projectFile: file, directory: root, name: "hello", entry: path.join(root, "src/main.matrix"),
    includePaths: [root], outputDirectory: path.join(root, "build"), analysis: { disabledRules: [], warningsAsErrors: false }, projectReferences: []
  });
  const project = parseProject(JSON.stringify({ ...manifest, includePaths: ["lib", "lib", "."], sdkPath: "../sdk",
    outputDirectory: "artifacts", analysis: { disabledRules: ["NIL1001", "NIL2002"], warningsAsErrors: true } }), file);
  assert.deepEqual(project.includePaths, [root, path.join(root, "lib")]);
  assert.equal(project.sdkPath, path.resolve(root, "../sdk"));
  assert.equal(project.outputDirectory, path.join(root, "artifacts"));
  assert.equal(project.analysis.warningsAsErrors, true);
});

test("project parsing rejects ambiguous JSON, unsupported fields and wrong policy types", () => {
  for (const text of ['{"schema":"nil.project.v1","schema":"nil.project.v1"}',
    '{"schema":"nil.project.v1","schem\\u0061":"nil.project.v1"}', '{"a":[1,]}', '{"a":true,}',
    '{"a":01}', '{"a":NaN}', '{"a":"\\uZZZZ"}', '{"a":"unterminated}', 'null trailing']) {
    assert.throws(() => parseProjectJson(text));
  }
  for (const extra of [{ schema: "nil.project.v2" }, { typo: true }, { name: "../outside" }, { name: "source.matrix" },
    { name: "x".repeat(129) }, { entry: "not-a-source.txt" }, { entry: "bad\0.matrix" }, { includePaths: [7] },
    { sdkPath: "" }, { outputDirectory: null }, { analysis: { ignoredRules: [] } },
    { analysis: { warningsAsErrors: "true" } }, { analysis: { disabledRules: ["NIL1001", "NIL1001"] } },
    { analysis: { disabledRules: ["unknown"] } }]) {
    assert.throws(() => parseProject(JSON.stringify({ ...manifest, ...extra }), file), JSON.stringify(extra));
  }
  for (const text of ['{"entry":"\\ud800.ny"}', '{"\\udfff":"value"}']) assert.throws(() => parseProjectJson(text));
  assert.deepEqual(parseProjectJson('"\\ud83d\\ude00"'), "😀");
  assert.throws(() => parseProjectJson(" ".repeat(1024 * 1024 + 1)), /1 MiB/);
  assert.throws(() => parseProjectJson("[".repeat(34) + "0" + "]".repeat(34)), /nesting/);
});

test("project discovery selects the nearest ancestor and stays within the workspace", () => {
  const nested = path.join(root, "apps", "demo", "nil.project.json");
  const source = path.join(root, "apps", "demo", "src", "main.ny");
  const available = new Set([file, nested, path.resolve(root, "../nil.project.json")]);
  assert.equal(findProjectFile(source, root, candidate => available.has(candidate)), nested);
  available.delete(nested);
  assert.equal(findProjectFile(source, root, candidate => available.has(candidate)), file);
  available.delete(file);
  assert.equal(findProjectFile(source, root, candidate => available.has(candidate)), undefined);
  assert.equal(findProjectFile(path.resolve(root, "../outside.ny"), root, () => true), undefined);
});

test("project invocation passes filenames and program arguments literally", () => {
  const invocation = projectInvocation({ builder: "/sdk tools/nilbuild", compiler: "/sdk tools/nilcompiler",
    interpreter: "/sdk tools/nilinterpreter", projectFile: "/workspace/a;$(no)/nil.project.json",
    sdkPath: "/sdk tools", action: "run", target: "native", programArgs: ["two words", "$(no)", "--flag"] });
  assert.deepEqual(invocation, { command: "/sdk tools/nilbuild", args: ["run", "--project",
    "/workspace/a;$(no)/nil.project.json", "--target", "native", "--sdk", "/sdk tools",
    "--compiler", "/sdk tools/nilcompiler", "--interpreter", "/sdk tools/nilinterpreter", "--", "two words", "$(no)", "--flag"] });
  assert.equal(projectInvocation({ builder: "nilbuild", compiler: "nilcompiler", projectFile: file,
    action: "check", target: "vm", programArgs: ["ignored"] }).args.includes("--"), false);
});

test("nilbuild resolves from canonical copied SDK layout", () => {
  const executable = path.join(root, "sdk", "nilbuild");
  assert.equal(resolveNyTool({ tool: "nilbuild", sdkPath: path.join(root, "sdk"), platform: "linux",
    environment: {}, isExecutable: candidate => candidate === executable }).executable, executable);
});


test("project files reject malformed UTF-8 and BOM consistently with the driver", () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "nil-project-model-"));
  const projectFile = path.join(directory, "nil.project.json");
  try {
    fs.writeFileSync(projectFile, Buffer.from([0xff, 0xfe]));
    assert.throws(() => readProject(projectFile));
    fs.writeFileSync(projectFile, "\uFEFF" + JSON.stringify(manifest));
    assert.throws(() => readProject(projectFile));
    fs.writeFileSync(projectFile, JSON.stringify(manifest));
    assert.equal(readProject(projectFile).name, "hello");
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});


test("project files reject FIFOs without blocking the editor", { skip: process.platform === "win32" }, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "nil-project-fifo-"));
  const projectFile = path.join(directory, "nil.project.json");
  try {
    execFileSync("mkfifo", [projectFile]);
    const script = `try { require(process.argv[1]).readProject(process.argv[2]); process.exitCode = 1; }
      catch (error) { if (!/regular file/.test(error.message)) throw error; }`;
    execFileSync(process.execPath, ["-e", script, path.resolve("out-test/projectModel.js"), projectFile], { timeout: 2000 });
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});
