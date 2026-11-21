const assert = require("node:assert/strict");
const { before, after, test } = require("node:test");
const { createRequire } = require("node:module");
const path = require("node:path");
const vm = require("node:vm");
const esbuild = require("esbuild");
const { Minimatch, minimatch } = require("minimatch");

const clientRoot = path.resolve(path.dirname(require.resolve("vscode-languageclient/node")), "../..");
const globConsumer = path.join(clientRoot, "lib/common/utils/globPattern.js");
const fileConsumer = path.join(clientRoot, "lib/common/fileOperations.js");
let adaptLanguageClientGlobGuard;
let languageClientGlobCompatibility;
let consumers;
const entry = `export { matchGlobPattern } from ${JSON.stringify(globConsumer)};
export { DidCreateFilesFeature } from ${JSON.stringify(fileConsumer)};`;
const buildOptions = { bundle: true, write: false, platform: "node", format: "cjs",
  target: "node20", external: ["vscode"], logLevel: "silent" };

function uri(fsPath, scheme = "file", directory = false) {
  return { fsPath, scheme, directory, toString: () => `${scheme}://${fsPath}` };
}
function loadBundle(contents) {
  const vscode = {
    FileType: { File: 1, Directory: 2 },
    workspace: {
      onDidCreateFiles: () => ({ dispose() {} }),
      fs: { stat: async value => ({ type: value.directory ? 2 : 1 }) }
    },
    Uri: { parse(value) {
      const parsed = new URL(value);
      const basePath = decodeURIComponent(parsed.pathname);
      return { path: basePath, with: change => uri(change.path, parsed.protocol.slice(0, -1)) };
    } }
  };
  const module = { exports: {} };
  const wrapper = vm.runInThisContext(`(function(require, module, exports) {\n${contents}\n})`);
  wrapper(name => name === "vscode" ? vscode : require(name), module, module.exports);
  return module.exports;
}
function feature() {
  return new consumers.DidCreateFilesFeature({
    code2ProtocolConverter: { asDidCreateFilesParams: event => event },
    warn() {}, info() {}
  });
}

before(async () => {
  ({ adaptLanguageClientGlobGuard, languageClientGlobCompatibility } =
    await import("../tools/minimatch-client-compat.mjs"));
  const bundle = await esbuild.build({ ...buildOptions, stdin: { contents: entry, resolveDir: clientRoot },
    plugins: [languageClientGlobCompatibility()] });
  consumers = loadBundle(bundle.outputFiles[0].text);
});
after(() => esbuild.stop());

test("every language-client and packaging matcher resolves the patched version", () => {
  const lock = require("../package-lock.json");
  const matchers = Object.entries(lock.packages).filter(([name]) => name.endsWith("/minimatch"));
  assert.equal(matchers.length, 1);
  assert.equal(matchers[0][1].version, "11.0.9");
  for (const parent of ["vscode-languageclient/node", "@vscode/vsce"]) {
    const fromParent = createRequire(require.resolve(parent));
    assert.equal(fromParent("minimatch/package.json").version, "11.0.9");
  }
  assert.equal(Minimatch.prototype.makeRe, undefined, "no legacy RegExp shim is installed");
});

test("unadapted upstream consumers expose the removed API regression", async () => {
  const bundle = await esbuild.build({ ...buildOptions, stdin: { contents: entry, resolveDir: clientRoot } });
  const upstream = loadBundle(bundle.outputFiles[0].text);
  assert.throws(() => upstream.matchGlobPattern("**/*.ny", uri("/workspace/main.ny")), /makeRe/);
});

test("glob validity adaptation fails closed when upstream call sites change", async () => {
  assert.throws(() => adaptLanguageClientGlobGuard("matcher.match(path)"), /changed/);
  assert.throws(() => adaptLanguageClientGlobGuard("if (!matcher.makeRe()) { if (!matcher.makeRe()) {"), /changed/);
  await assert.rejects(esbuild.build({ ...buildOptions,
    stdin: { contents: `export * from ${JSON.stringify(globConsumer)};`, resolveDir: clientRoot },
    plugins: [languageClientGlobCompatibility()] }), /both reviewed/);
});

test("incremental bundling retains both reviewed glob consumers", async () => {
  const context = await esbuild.context({ ...buildOptions,
    stdin: { contents: entry, resolveDir: clientRoot },
    plugins: [languageClientGlobCompatibility()] });
  try {
    for (let build = 0; build < 2; build++) {
      const result = await context.rebuild();
      assert.equal(loadBundle(result.outputFiles[0].text)
        .matchGlobPattern("**/*.ny", uri("/workspace/main.ny")), true);
    }
  } finally { await context.dispose(); }
});

test("bundled language-client globs retain ordinary, escaped and Unicode matching", () => {
  for (const [pattern, filename, expected] of [
    ["**/*.{ny,matrix,mat}", "/workspace/main.ny", true],
    ["**/*.{ny,matrix,mat}", "/workspace/main.cpp", false],
    ["**/file?.ny", "/workspace/file1.ny", true],
    ["**/file[0-9].ny", "/workspace/filea.ny", false],
    ["**/*.ny", "/workspace/.hidden.ny", false],
    ["**/.hidden.ny", "/workspace/.hidden.ny", true],
    ["**/café 中文.matrix", "/workspace/café 中文.matrix", true],
    ["**/[[]name].ny", "/workspace/[name].ny", true],
    ["**/@(first|second).ny", "/workspace/first.ny", false],
    ["**/@(first|second).ny", "/workspace/@(first|second).ny", true],
    ["**/unfinished[.ny", "/workspace/unfinished[.ny", true],
    ["!**/*.ny", "/workspace/main.matrix", true],
    ["!**/*.ny", "/workspace/main.ny", false],
    ["", "", false], ["#comment", "#comment", false]
  ]) assert.equal(consumers.matchGlobPattern(pattern, uri(filename)), expected, `${pattern}: ${filename}`);
  assert.equal(consumers.matchGlobPattern({ baseUri: "file:///workspace/caf%C3%A9", pattern: "*.ny" },
    uri("/workspace/café/main.ny")), true);
  assert.equal(consumers.matchGlobPattern({ baseUri: "invalid URI", pattern: "*.ny" }, uri("main.ny")), false);
});

test("bundled file operations retain dotfiles, case options, schemes and file kinds", async () => {
  const registered = feature();
  try {
    registered.register({ id: "source", registerOptions: { filters: [
      { scheme: "file", pattern: { glob: "**/*.{ny,matrix}", matches: "file", options: { ignoreCase: true } } }
    ] } });
    const files = [uri("/workspace/.hidden.ny"), uri("/workspace/MAIN.NY"),
      uri("/workspace/data.matrix"), uri("/workspace/other.txt"), uri("/workspace/main.ny", "untitled"),
      uri("/workspace/folder.ny", "file", true), uri("C:\\workspace\\other.ny")];
    assert.deepEqual((await registered.filter({ files }, value => value)).files,
      [files[0], files[1], files[2], files[6]]);
    registered.unregister("source");
    assert.deepEqual((await registered.filter({ files }, value => value)).files, []);
  } finally { registered.clear(); }
});

test("bundled file operations keep invalid registrations out of the filter set", () => {
  const registered = feature();
  try {
    for (const glob of ["", "#comment"]) {
      assert.throws(() => registered.register({ id: "invalid", registerOptions: { filters: [{ pattern: { glob } }] } }), /Invalid pattern/);
      assert.equal(registered.filterSize(), 0);
    }
  } finally { registered.clear(); }
});

test("patched matcher retains Windows drive, UNC and POSIX glob semantics", () => {
  for (const [filename, pattern, options, expected] of [
    ["C:\\work\\main.ny", "C:/work/*.ny", { platform: "win32" }, true],
    ["C:\\work\\MAIN.NY", "C:/work/*.ny", { platform: "win32" }, false],
    ["C:\\work\\MAIN.NY", "c:/work/*.ny", { platform: "win32", nocase: true }, true],
    ["\\\\server\\share\\main.ny", "//server/share/*.ny", { platform: "win32" }, true],
    ["//?/C:/work/main.ny", "C:/work/*.ny", { platform: "win32" }, true],
    ["/workspace/.config/a.ny", "**/*.ny", { platform: "linux", dot: true }, true],
    ["/workspace/.config/a.ny", "**/*.ny", { platform: "darwin", dot: false }, false]
  ]) assert.equal(minimatch(filename, pattern, options), expected, `${pattern}: ${filename}`);
});

test("bounded optimization-level-2 fixtures enforce the patched expansion limit", () => {
  // Tiny explicit limits make these regression inputs bounded even on failure.
  assert.throws(() => new Minimatch("**/../a/b/".repeat(8) + "file.ny",
    { optimizationLevel: 2, braceExpandMax: 4 }),
    { name: "RangeError", message: "optimizationLevel 2: too many patterns from '**/..'" });
  assert.equal(new Minimatch("**/../a/b/file.ny", { optimizationLevel: 2, braceExpandMax: 4 }).match("a/b/file.ny"), true);
});
