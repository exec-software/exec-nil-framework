import assert from "node:assert/strict";
import { buildSync } from "esbuild";
import * as fs from "node:fs/promises";
import * as os from "node:os";
import * as path from "node:path";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";
import { Script } from "node:vm";
import type { AnalysisResult } from "../analysisModel";

// Execute the production class and its real helpers, without activating an
// editor, registering watchers, or spawning a compiler. Only the VS Code API
// boundary is fake; publisher/semantic filesystem reads and byte mapping are real.
const bundled = buildSync({
  entryPoints: [path.resolve(__dirname, "../../src/analyzer.ts")],
  bundle: true, write: false, platform: "node", format: "cjs", external: ["vscode"], logLevel: "silent"
}).outputFiles[0]!.text;

class Position {
  constructor(readonly line: number, readonly character: number) {}
}
class Range {
  constructor(readonly start: Position, readonly end: Position) {}
}
class Diagnostic {
  source?: string;
  code?: string;
  constructor(readonly range: Range, readonly message: string, readonly severity: number) {}
}
function uri(filename: string) {
  return { scheme: "file", fsPath: filename, toString: () => pathToFileURL(filename).href };
}
function document(filename: string, text: string) {
  return { uri: uri(filename), languageId: "ny", version: 1, isClosed: false, isDirty: false,
    text, getText() { return this.text; } };
}
type FileUri = ReturnType<typeof uri>;
class TabInputText {
  constructor(readonly uri: FileUri) {}
}
class TabInputTextDiff {
  constructor(readonly original: FileUri, readonly modified: FileUri) {}
}
type Document = ReturnType<typeof document>;
type SourceSnapshot = { path: string; text: string };
interface Job {
  document: Document;
  version: number;
  text: string;
  source: string;
  key: string;
  generation: number;
  inputs: { document: Document; version: number; text: string; source: string; identity: string }[];
}
interface Publisher {
  tabSources: Set<string>;
  generation: number;
  timers: Map<string, ReturnType<typeof setTimeout>>;
  workspaceInput(job: Job): Promise<string>;
  publish(job: Job, result: AnalysisResult, signal: AbortSignal): Promise<boolean>;
  current(job: Job): boolean;
  openTabSources(): Set<string>;
  updateTabSources(): void;
  validateSemantic(job: Job, index: { sources: SourceSnapshot[] }, token: { isCancellationRequested: boolean }): Promise<boolean>;
  matchesDiskSnapshot(source: SourceSnapshot): Promise<boolean>;
}

const broken = 'module dep\r\n# é😀\r\npub fn broken() -> i32\r\n return "bad"\r\nend fn\r\n';
const repaired = broken.replace('"bad"', "1");
async function fixture(operation: (value: {
  publisher: Publisher; job: Job; result: AnalysisResult; dependency: Document; documents: Document[];
  published: Map<string, Diagnostic[]>; output: string[]; depPath: string;
  tabs: { input: TabInputText | TabInputTextDiff }[];
}) => Promise<void>, dependencyName = "dep.ny") {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil publisher café 中文-"));
  try {
    const entryText = (dependencyName === "dep.ny" ? 'import dep.*\n' : `@include "${dependencyName}"\n`)
      + 'fn main() -> i32\n return broken()\nend fn\n';
    await fs.writeFile(path.join(directory, "main.ny"), entryText);
    await fs.writeFile(path.join(directory, dependencyName), broken);
    // Exact canonical spelling makes the cached-document guard observable on
    // Windows too, independent of drive-letter case produced by a real editor.
    const entryPath = await fs.realpath(path.join(directory, "main.ny"));
    const depPath = await fs.realpath(path.join(directory, dependencyName));
    const entry = document(entryPath, entryText), dependency = document(depPath, repaired);
    const documents = [entry, dependency], published = new Map<string, Diagnostic[]>(), output: string[] = [];
    const tabs = [{ input: new TabInputText(entry.uri) }] as { input: TabInputText | TabInputTextDiff }[];
    const api = {
      workspace: { textDocuments: documents, isTrusted: true,
        getConfiguration: () => ({ get: (_key: string, fallback: unknown) => fallback }) },
      Uri: { file: uri, parse: (value: string) => uri(fileURLToPath(value)) },
      window: { tabGroups: { all: [{ tabs }] } }, TabInputText, TabInputTextDiff,
      Position, Range, Diagnostic, DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 }
    };
    const module = { exports: {} as { NyAnalyzer: { prototype: Publisher } } };
    new Script(bundled, { filename: "analyzer-publisher-test.cjs" }).runInNewContext({
      module, exports: module.exports, require: (name: string) => name === "vscode" ? api : require(name),
      process, Buffer, TextDecoder, setTimeout, clearTimeout
    });
    const publisher = Object.assign(Object.create(module.exports.NyAnalyzer.prototype) as Publisher, {
      generation: 1, disposed: false, tabSources: new Set([entry.uri.toString()]),
      contributions: new Map(), fixes: new Map(), reports: new Map(),
      semantics: new Map(), completionQueries: new Map(), signatureQueries: new Map(),
      timers: new Map(), pending: new Map(), renameChecks: new Set(),
      diagnostics: { clear: () => published.clear(), set: (key: ReturnType<typeof uri>, items: Diagnostic[]) => published.set(key.toString(), items) },
      output: { clear: () => { output.length = 0; }, appendLine: (value: string) => output.push(value) }
    });
    const job: Job = { document: entry, version: entry.version, text: entryText, source: entryPath,
      key: entry.uri.toString(), generation: 1, inputs: [] };
    const start = Buffer.byteLength(broken.slice(0, broken.indexOf('"bad"')), "utf8");
    const result: AnalysisResult = { schema: "nil.analysis.v1", source: entryPath, language: "ny", success: false,
      reflection: {}, sources: [{ path: depPath, text: broken }],
      diagnostics: [{ code: "NIL1002", severity: "error", message: "Expected i32", hint: "", path: depPath,
        start, length: Buffer.byteLength('"bad"'), fix: null }] };
    try { await operation({ publisher, job, result, dependency, documents, published, output, depPath, tabs }); }
    finally { for (const timer of publisher.timers.values()) clearTimeout(timer); }
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
}

function located(published: Map<string, Diagnostic[]>, depPath: string): void {
  const items = published.get(uri(depPath).toString()) ?? [];
  assert.equal(items.length, 1, "Current disk import must publish its finding even when a clean closed-tab cache is stale.");
  const finding = items[0]!;
  assert.equal(finding.source, "nil-analysis");
  assert.equal(finding.code, "NIL1002");
  assert.equal(finding.severity, 0);
  assert.deepEqual(finding.range, new Range(new Position(3, 8), new Position(3, 13)));
}

test("publisher excludes a clean closed-tab cache from overlays and publishes the current disk import", async () => {
  await fixture(async ({ publisher, job, result, dependency, published, output, depPath }) => {
    const workspace = JSON.parse(await publisher.workspaceInput(job));
    assert.equal(job.inputs.length, 1);
    assert.equal(job.inputs[0]!.document, job.document);
    assert.deepEqual(workspace.sources, [{ path: job.source, text: job.text }]);
    assert.equal(dependency.isClosed, false, "A closed tab can retain a live clean TextDocument.");
    assert.equal(publisher.current(job), true, "The entry must remain eligible throughout the regression.");
    assert.equal(await publisher.publish(job, result, new AbortController().signal), true);
    located(published, depPath);
    assert.deepEqual(output, []);
    assert.equal(await fs.readFile(depPath, "utf8"), broken);
    assert.equal(dependency.getText(), repaired, "Publishing must neither edit nor save the cached document.");
  });
});

for (const mode of ["absent", "current"] as const) test(`publisher locates a disk import with ${mode} cache`, async () => {
  await fixture(async ({ publisher, job, result, dependency, documents, published, depPath }) => {
    if (mode === "absent") documents.splice(documents.indexOf(dependency), 1);
    else dependency.text = broken;
    await publisher.workspaceInput(job);
    assert.equal(publisher.current(job), true);
    assert.equal(await publisher.publish(job, result, new AbortController().signal), true);
    located(published, depPath);
  });
});

for (const mode of ["dirty", "open-tab", "changed-disk", "became-dirty", "became-open-tab"] as const)
  test(`publisher still rejects a mismatched ${mode} import snapshot`, async () => {
    await fixture(async ({ publisher, job, result, dependency, documents, published, output, depPath }) => {
      if (mode === "dirty") dependency.isDirty = true;
      if (mode === "open-tab") publisher.tabSources.add(dependency.uri.toString());
      if (mode === "changed-disk") documents.splice(documents.indexOf(dependency), 1);
      await publisher.workspaceInput(job);
      assert.equal(job.inputs.length, mode === "dirty" || mode === "open-tab" ? 2 : 1);
      if (mode === "changed-disk") await fs.writeFile(depPath, broken.replace('"bad"', '"new"'));
      if (mode === "became-dirty") dependency.isDirty = true;
      if (mode === "became-open-tab") publisher.tabSources.add(dependency.uri.toString());
      assert.equal(publisher.current(job), true);
      assert.equal(await publisher.publish(job, result, new AbortController().signal), true);
      assert.equal(published.get(uri(depPath).toString())?.length ?? 0, 0);
      assert.ok(output.join("\n").includes("NIL1002"), "Unsafe locations must remain visible in Analysis Output.");
    });
  });

async function includeAlias(dependency: Document, depPath: string): Promise<void> {
  const directory = path.join(path.dirname(depPath), "include alias");
  await fs.symlink(path.dirname(depPath), directory, process.platform === "win32" ? "junction" : "dir");
  dependency.uri = uri(path.join(directory, path.basename(depPath)));
  assert.notEqual(dependency.uri.toString(), uri(depPath).toString());
}

for (const alias of [false, true]) test(`semantic validation follows restored disk include with a stale closed-tab ${alias ? "alias" : "model"}`, async () => {
  await fixture(async ({ publisher, job, dependency, depPath }) => {
    if (alias) await includeAlias(dependency, depPath);
    dependency.text = broken;
    await publisher.workspaceInput(job);
    assert.equal(job.inputs.length, 1, "An arbitrary-extension include is a disk input, not a compiler overlay.");
    const sources = [{ path: job.source, text: job.text }, { path: depPath, text: broken }];
    const token = { isCancellationRequested: false };
    assert.equal(await publisher.validateSemantic(job, { sources }, token), true);
    await fs.writeFile(depPath, repaired);
    sources[1]!.text = repaired;
    assert.equal(dependency.isDirty, false);
    assert.equal(dependency.isClosed, false, "Closing its tab need not dispose VS Code's clean model.");
    assert.equal(await publisher.validateSemantic(job, { sources }, token), true,
      "A retained clean closed-tab include must not veto a fresh compiler disk snapshot.");
    assert.equal(dependency.getText(), broken, "Validation must not refresh, edit or save the cached model.");
    assert.equal(await fs.readFile(depPath, "utf8"), repaired);
  }, "helper.inc");
});

for (const mode of ["dirty", "text-tab", "diff-original", "diff-modified"] as const)
  test(`semantic validation retains ${mode} authority for arbitrary include aliases`, async () => {
    await fixture(async ({ publisher, job, dependency, depPath, tabs }) => {
      await includeAlias(dependency, depPath);
      if (mode === "dirty") dependency.isDirty = true;
      else {
        tabs.push({ input: mode === "text-tab" ? new TabInputText(dependency.uri) :
          mode === "diff-original" ? new TabInputTextDiff(dependency.uri, job.document.uri) :
          new TabInputTextDiff(job.document.uri, dependency.uri) });
        publisher.tabSources = publisher.openTabSources();
        assert.ok(publisher.tabSources.has(dependency.uri.toString()), "Open .inc inputs need the same authority as source tabs.");
      }
      await publisher.workspaceInput(job);
      assert.equal(job.inputs.length, 1, "Tracking an open include must not invent support for include overlays.");
      assert.equal(publisher.current(job), true);
      const sources = [{ path: job.source, text: job.text }, { path: depPath, text: broken }];
      assert.equal(await publisher.validateSemantic(job, { sources }, { isCancellationRequested: false }), false);
      dependency.text = broken;
      assert.equal(await publisher.validateSemantic(job, { sources }, { isCancellationRequested: false }), mode !== "dirty",
        "Matching clean open includes are navigable; dirty includes still have no supported overlay.");
    }, "helper.inc");
  });

for (const mode of ["became-dirty", "changed-version", "opened-tab"] as const)
  test(`semantic validation rejects an include alias that ${mode} during disk validation`, async () => {
    await fixture(async ({ publisher, job, dependency, depPath, tabs }) => {
      await includeAlias(dependency, depPath);
      await publisher.workspaceInput(job);
      const read = publisher.matchesDiskSnapshot.bind(publisher);
      let observed = false;
      publisher.matchesDiskSnapshot = async source => {
        const matches = await read(source);
        if (source.path === depPath) {
          observed = true;
          if (mode === "became-dirty") dependency.isDirty = true;
          else if (mode === "changed-version") dependency.version++;
          else {
            tabs.push({ input: new TabInputText(dependency.uri) });
            publisher.updateTabSources();
            assert.equal(publisher.generation, job.generation + 1, "Opening an include must invalidate the in-flight generation.");
          }
        }
        return matches;
      };
      const sources = [{ path: job.source, text: job.text }, { path: depPath, text: broken }];
      assert.equal(await publisher.validateSemantic(job, { sources }, { isCancellationRequested: false }), false);
      assert.equal(observed, true, "The race control must reach the real disk comparison before changing the model.");
    }, "helper.inc");
  });

test("opening and closing an arbitrary include alias invalidate semantic generations", async () => {
  await fixture(async ({ publisher, job, dependency, depPath, tabs }) => {
    await includeAlias(dependency, depPath);
    await publisher.workspaceInput(job);
    assert.equal(publisher.current(job), true);
    const generation = publisher.generation;
    tabs.push({ input: new TabInputText(dependency.uri) });
    publisher.updateTabSources();
    assert.equal(publisher.generation, generation + 1);
    assert.equal(publisher.current(job), false);
    publisher.updateTabSources();
    assert.equal(publisher.generation, generation + 1, "Unchanged membership must not invalidate a new generation.");
    job.generation = publisher.generation;
    assert.equal(publisher.current(job), true);
    tabs.pop();
    publisher.updateTabSources();
    assert.equal(publisher.generation, generation + 2);
    assert.equal(publisher.current(job), false);
    assert.equal(dependency.isClosed, false, "Tab closure must invalidate even if VS Code retains the model.");
  }, "helper.inc");
});
