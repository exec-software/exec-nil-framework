import assert from "node:assert/strict";
import test from "node:test";

import {
  compilerProblemMatchers,
  debugProgramKind,
  languageDocumentSelector,
  savedSourcePath,
  sourceLanguage,
  taskSourcePath
} from "../languages";

const document = (fsPath: string, languageId = "matrix", scheme = "file") => ({
  languageId, uri: { fsPath, scheme }
});

test("the language client covers both file and untitled documents for both frontends", () => {
  assert.deepEqual(languageDocumentSelector, [
    { language: "ny", scheme: "file" },
    { language: "ny", scheme: "untitled" },
    { language: "matrix", scheme: "file" },
    { language: "matrix", scheme: "untitled" }
  ]);
});

test("frontend detection handles both Matrix extensions and uppercase paths", () => {
  for (const source of ["main.matrix", "main.mat", "main.MATRIX", "C:\\work\\main.MAT"]) {
    assert.equal(sourceLanguage(source), "matrix");
  }
  assert.equal(sourceLanguage("main.NY"), "ny");
  for (const source of ["main.nil", "main.mat.json", "matrix", "main.matrix/README"]) {
    assert.equal(sourceLanguage(source), undefined);
  }
});

test("commands require saved source in the frontend selected by its extension", () => {
  assert.equal(savedSourcePath(document("/work/main.matrix")), "/work/main.matrix");
  assert.equal(savedSourcePath(document("/work/main.mat")), "/work/main.mat");
  assert.equal(savedSourcePath(document("/work/main.ny", "ny")), "/work/main.ny");
  assert.equal(savedSourcePath(document("/work/main.ny", "matrix")), undefined);
  assert.equal(savedSourcePath(document("/work/main.mat", "plaintext")), undefined);
  assert.equal(savedSourcePath(document("/work/main.mat", "matrix", "untitled")), undefined);
  assert.equal(savedSourcePath(document("/work/main.mat", "matrix", "vscode-remote")), undefined);
  assert.equal(savedSourcePath(undefined), undefined);
});

test("task source resolution rejects unrelated active files and preserves explicit sources", () => {
  assert.equal(taskSourcePath(undefined, document("/work/main.mat")), "/work/main.mat");
  assert.equal(taskSourcePath("${file}", document("/work/main.matrix")), "/work/main.matrix");
  assert.equal(taskSourcePath("${file}", document("/work/README.md", "markdown")), undefined);
  assert.equal(taskSourcePath("/work/main.mat", undefined), "/work/main.mat");
  assert.equal(taskSourcePath("${workspaceFolder}/main.matrix", undefined), "${workspaceFolder}/main.matrix");
  assert.equal(taskSourcePath("/work/native.exe", document("/work/main.mat")), undefined);
});

test("debug launches compile both source frontends but accept precompiled NIL directly", () => {
  for (const source of ["main.ny", "main.matrix", "main.mat", "main.MAT"]) {
    assert.equal(debugProgramKind(source), "source");
  }
  assert.equal(debugProgramKind("main.nil"), "nil");
  assert.equal(debugProgramKind("main.NIL"), "nil");
  assert.equal(debugProgramKind("main.exe"), undefined);
  assert.equal(debugProgramKind("main.mat.backup"), undefined);
  assert.deepEqual(compilerProblemMatchers, ["$nilcompiler", "$matrixcompiler"]);
});
