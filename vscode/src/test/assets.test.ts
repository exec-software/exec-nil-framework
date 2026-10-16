import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import test from "node:test";

function readJson(relativePath: string): unknown {
  return JSON.parse(fs.readFileSync(path.join(process.cwd(), relativePath), "utf8"));
}

test("manifest contributes both frontends, shared tasks, and source debugger integration", () => {
  const manifest = readJson("package.json") as {
    activationEvents: string[];
    contributes: {
      breakpoints: Array<{ language: string }>;
      languages: Array<{ id: string; extensions: string[] }>;
      configuration: { properties: Record<string, unknown> };
      taskDefinitions: Array<{ properties: { task: { enum: string[] } } }>;
      debuggers: Array<{ type: string; languages: string[]; configurationAttributes: { launch: { required: string[] } } }>;
    };
  };
  assert.deepEqual(manifest.contributes.languages[0], {
    id: "ny",
    aliases: ["Ny", "ny"],
    extensions: [".ny"],
    configuration: "./language-configuration.json"
  });
  assert.deepEqual(manifest.contributes.languages[1], {
    id: "matrix",
    aliases: ["Matrix", "matrix"],
    extensions: [".matrix", ".mat"],
    configuration: "./language-configuration.matrix.json"
  });
  for (const language of ["ny", "matrix"]) {
    assert.ok(manifest.activationEvents.includes(`onLanguage:${language}`));
    assert.ok(manifest.contributes.breakpoints.some((item) => item.language === language));
  }
  for (const extension of ["ny", "matrix", "mat"]) {
    assert.ok(manifest.activationEvents.includes(`workspaceContains:**/*.${extension}`));
  }
  assert.ok(manifest.contributes.configuration.properties["ny.interpreter.path"]);
  const retiredInterpreterName = ["ny", "vm"].join("");
  const retiredInterpreterSetting = ["ny", "vm.path"].join(".");
  assert.equal(
    manifest.contributes.configuration.properties[retiredInterpreterSetting],
    undefined
  );
  assert.deepEqual(manifest.contributes.taskDefinitions[0]?.properties.task.enum, [
    "compile",
    "run",
    "native",
    "build-sdk",
    "ctest"
  ]);
  assert.equal(manifest.contributes.debuggers[0]?.type, "ny");
  assert.deepEqual(manifest.contributes.debuggers[0]?.languages, ["ny", "matrix"]);
  assert.deepEqual(manifest.contributes.debuggers[0]?.configurationAttributes.launch.required, ["program"]);
  assert.ok(manifest.contributes.configuration.properties["ny.debugger.path"]);
  assert.equal(JSON.stringify(manifest).includes(retiredInterpreterName), false);
});

test("compiler problem matchers recognize Matrix parser and typechecker locations", () => {
  const manifest = readJson("package.json") as {
    contributes: { problemMatchers: Array<{
      name: string;
      pattern: { regexp: string } | Array<{ regexp: string }>;
    }>; configurationDefaults?: Record<string, unknown> };
  };
  const matchers = manifest.contributes.problemMatchers;
  const matrixPattern = matchers.find((matcher) => matcher.name === "matrixcompiler")?.pattern;
  assert.ok(Array.isArray(matrixPattern));
  const headline = new RegExp(matrixPattern[0]!.regexp);
  const location = new RegExp(matrixPattern[1]!.regexp);
  assert.deepEqual(headline.exec("error[M1001]: Expected a page name.")?.slice(1), [
    "error", "M1001", "Expected a page name."
  ]);
  assert.deepEqual(location.exec("  --> /work/my page.matrix:12:8")?.slice(1), [
    "/work/my page.matrix", "12", "8"
  ]);
  // Shared typechecking reports a line without inventing a column.
  assert.deepEqual(location.exec("  --> C:\\work\\MAIN.MAT:9")?.slice(1), [
    "C:\\work\\MAIN.MAT", "9", undefined
  ]);
  assert.deepEqual(location.exec("  --> /work/imported.ny:7")?.slice(1), [
    "/work/imported.ny", "7", undefined
  ]);
  assert.equal(location.test("  --> /work/main.mat.backup:9:1"), false);
  const nyPattern = matchers.find((matcher) => matcher.name === "nilcompiler")?.pattern;
  assert.ok(nyPattern && !Array.isArray(nyPattern));
  assert.deepEqual(new RegExp(nyPattern.regexp).exec(" /work/MAIN.NY:3: missing end")?.slice(1), [
    "/work/MAIN.NY", "3", "missing end"
  ]);
  assert.equal(manifest.contributes.configurationDefaults?.["editor.tokenColorCustomizations"], undefined);
});

test("framework branding preserves installed extension and workspace identities", () => {
  const manifest = readJson("package.json") as {
    name: string; publisher: string; displayName: string;
    repository: { url: string }; bugs: { url: string };
    contributes: {
      commands: Array<{ command: string; title: string }>;
      configuration: { title: string; properties: Record<string, unknown> };
      problemMatchers: Array<{ name: string; source: string; pattern: unknown }>;
    };
  };
  assert.equal(`${manifest.publisher}.${manifest.name}`, "exec-software.ny-language-support");
  assert.equal(manifest.displayName, "NIL Framework (Ny + Matrix)");
  assert.equal(manifest.repository.url, "https://github.com/exec-software/exec-nil-framework.git");
  assert.equal(manifest.bugs.url, "https://github.com/exec-software/exec-nil-framework/issues");
  for (const command of manifest.contributes.commands) {
    assert.ok(command.command.startsWith("ny."));
    assert.ok(command.title.startsWith("NIL Framework:"));
  }
  for (const setting of Object.keys(manifest.contributes.configuration.properties)) {
    assert.ok(setting.startsWith("ny."));
  }
  const canonical = manifest.contributes.problemMatchers.find((matcher) => matcher.name === "nilcompiler");
  const legacy = manifest.contributes.problemMatchers.find((matcher) => matcher.name === "nycompiler");
  assert.ok(canonical && legacy);
  assert.equal(canonical.source, "nilcompiler");
  assert.equal(legacy.source, "nilcompiler");
  assert.deepEqual(legacy.pattern, canonical.pattern);
});

test("language configuration uses canonical edit actions", () => {
  const configuration = readJson("language-configuration.json") as {
    comments: { lineComment: string; blockComment: string[] };
    brackets: string[][];
    indentationRules: Record<string, string>;
  };
  assert.equal(configuration.comments.lineComment, "//");
  assert.deepEqual(configuration.comments.blockComment, ["/*", "*/"]);
  assert.deepEqual(configuration.brackets, [["{", "}"], ["[", "]"], ["(", ")"]]);
  assert.ok(configuration.indentationRules.increaseIndentPattern);
  assert.ok(configuration.indentationRules.decreaseIndentPattern);
});

test("snippet catalog covers core, testing, fault, interop, and AI constructs", () => {
  const snippets = readJson("snippets/ny.code-snippets") as Record<
    string,
    { prefix: string | string[]; body: string | string[]; description: string }
  >;
  const prefixes = new Set(
    Object.values(snippets).flatMap((snippet) =>
      Array.isArray(snippet.prefix) ? snippet.prefix : [snippet.prefix]
    )
  );
  for (const prefix of [
    "module",
    "fn",
    "main",
    "struct",
    "enum",
    "trait",
    "impl",
    "if",
    "while",
    "for",
    "match",
    "attempt",
    "test",
    "example",
    "property",
    "@interop",
    "@intrinsic",
    "@doc",
    "@ai_context"
  ]) {
    assert.ok(prefixes.has(prefix), `missing ${prefix} snippet`);
  }
  const serialized = JSON.stringify(snippets);
  assert.equal(serialized.includes("end function"), false);
  assert.equal(serialized.includes(["ny", "vm"].join("")), false);
  assert.match(serialized, /end fn/);
  assert.match(serialized, /end attempt/);
});
