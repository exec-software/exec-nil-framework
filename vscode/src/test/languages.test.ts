import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";

import {
  analysisSourcePath,
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

test("analysis preserves named untitled source identities without treating scratch buffers as imports", () => {
  const source = path.resolve("new module.matrix");
  assert.equal(analysisSourcePath(document(source)), source);
  assert.equal(analysisSourcePath(document(source, "matrix", "untitled")), source);
  assert.equal(analysisSourcePath(document("Untitled-1", "matrix", "untitled")), undefined);
  assert.equal(analysisSourcePath(document("new.matrix", "matrix", "untitled")), undefined);
  assert.equal(analysisSourcePath(document(source, "ny", "untitled")), undefined);
  assert.equal(analysisSourcePath(document(source, "matrix", "vscode-remote")), undefined);
  assert.equal(analysisSourcePath(undefined), undefined);
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

test("NILData stays outside analysis, compiler tasks, and source debugging", () => {
  for (const source of ["settings.nildata", "settings.NILDATA"]) {
    const absolute = path.resolve(source);
    assert.equal(sourceLanguage(source), undefined);
    assert.equal(debugProgramKind(source), undefined);
    assert.equal(taskSourcePath(absolute, undefined), undefined);
    for (const language of ["nildata", "ny", "matrix"]) {
      for (const scheme of ["file", "untitled"]) {
        const data = document(absolute, language, scheme);
        assert.equal(savedSourcePath(data), undefined);
        assert.equal(analysisSourcePath(data), undefined);
        assert.equal(taskSourcePath("${file}", data), undefined);
      }
    }
  }
  assert.equal(languageDocumentSelector.some(item => item.language === "nildata"), false);
});
