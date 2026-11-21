import assert from "node:assert/strict";
import { buildSync } from "esbuild";
import * as fs from "node:fs/promises";
import * as os from "node:os";
import * as path from "node:path";
import test from "node:test";
import { pathToFileURL } from "node:url";
import { Script } from "node:vm";

// Exercise both production providers and their actual registrations. Only the
// editor API and external compiler/LSP responses are controlled by this fixture.
const bundled = buildSync({
  stdin: { contents: 'export { NyAnalyzer } from "./analyzer"; export { NyLanguageClientManager } from "./languageClient";',
    resolveDir: path.resolve(__dirname, "../../src"), loader: "ts" },
  bundle: true, write: false, platform: "node", format: "cjs",
  external: ["vscode", "vscode-languageclient/node"], logLevel: "silent"
}).outputFiles[0]!.text;
const disposable = () => ({ dispose() {} });
class Position { constructor(readonly line: number, readonly character: number) {} }
class Range {
  constructor(readonly start: Position, readonly end: Position) {}
  contains(position: Position) {
    const compare = (a: Position, b: Position) => a.line - b.line || a.character - b.character;
    return compare(this.start, position) <= 0 && compare(position, this.end) <= 0;
  }
}
const kinds = { Function: 2, Method: 1, Variable: 5, Field: 4, Keyword: 13, Snippet: 14 };
class CompletionItem {
  detail?: string; insertText?: string; range?: Range; filterText?: string;
  constructor(readonly label: string, readonly kind: number) {}
}
class CompletionList { constructor(readonly items: CompletionItem[], readonly isIncomplete = false) {} }
class TabInputText { constructor(readonly uri: ReturnType<typeof fileUri>) {} }
class TabInputTextDiff {}
function fileUri(filename: string) { return { scheme: "file", fsPath: filename, toString: () => pathToFileURL(filename).href }; }
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(done => { resolve = done; });
  return { promise, resolve };
}
function document(filename: string, text: string) {
  return { uri: fileUri(filename), languageId: "ny", version: 1, isClosed: false, isDirty: false, text,
    positionAt(offset: number) {
      const lines = this.text.slice(0, offset).split(/\r\n|\r|\n/);
      return new Position(lines.length - 1, lines.at(-1)!.length);
    },
    offsetAt(position: Position) {
      let offset = 0;
      const breaks = this.text.matchAll(/\r\n|\r|\n/g);
      for (let line = 0; line < position.line; line++) { const found = breaks.next().value!; offset = found.index! + found[0].length; }
      return offset + position.character;
    },
    getText(range?: Range): string { return range ? this.text.slice(this.offsetAt(range.start), this.offsetAt(range.end)) : this.text; }
  };
}
type Document = ReturnType<typeof document>;

async function fixture(operation: (value: any) => Promise<void>) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "nil completion owner-"));
  let analyzer: any, manager: any;
  const registrations: { provider: any; triggers: string[] }[] = [], documents: Document[] = [], tabs: any[] = [];
  const state: any = { analysis: true, compilerCalls: 0, compilerFailures: 0, lspCalls: 0, conversions: 0,
    compilerGate: undefined, lspGate: undefined, conversionGate: undefined, compilerEmpty: false, extraSource: undefined };
  const fallbackItems = [new CompletionItem("cafe_value", kinds.Variable), new CompletionItem("return", kinds.Keyword),
    new CompletionItem("template", kinds.Snippet), new CompletionItem("guessed_other", kinds.Variable)];
  const fallback = new CompletionList(fallbackItems);
  class Client {
    static instances: Client[] = [];
    running = false;
    initializeResult = { capabilities: { completionProvider: { resolveProvider: false, allCommitCharacters: [";"] } } };
    code2ProtocolConverter = { asCompletionParams: (doc: Document, position: Position, context: unknown) => ({
      textDocument: { uri: doc.uri.toString() }, position, context
    }) };
    protocol2CodeConverter = { asCompletionResult: async (value: unknown, characters: string[]) => {
      state.conversions++; assert.deepEqual(characters, [";"]);
      if (state.conversionGate) await state.conversionGate.promise;
      return value;
    } };
    constructor(_id: string, _name: string, _server: unknown, readonly options: any) { Client.instances.push(this); }
    async start() { this.running = true; }
    async stop() { this.running = false; }
    isRunning() { return this.running; }
    async sendRequest(type: any, params: any) {
      assert.equal(type.method, "textDocument/completion");
      assert.equal(params.textDocument.uri, documents[0]!.uri.toString());
      state.lspCalls++;
      state.lspStarted?.resolve(undefined);
      if (state.lspGate) await state.lspGate.promise;
      if (state.lspError) throw new Error("LSP unavailable");
      state.lspProduced?.resolve(undefined);
      return state.fallbackArray ? fallback.items : fallback;
    }
  }
  const api: any = {
    workspace: { textDocuments: documents, isTrusted: true, workspaceFolders: [], getWorkspaceFolder: () => undefined,
      getConfiguration: () => ({ get: (key: string, value: unknown) => key === "server.path" ? process.execPath : key === "analysis.enabled" ? state.analysis : value }),
      createFileSystemWatcher: () => ({ dispose() {}, onDidChange: disposable, onDidCreate: disposable, onDidDelete: disposable }) },
    window: { createOutputChannel: () => ({ clear() {}, appendLine() {}, dispose() {}, show() {} }),
      showInformationMessage() {}, tabGroups: { all: [{ tabs }], onDidChangeTabs: disposable, onDidChangeTabGroups: disposable } },
    languages: { createDiagnosticCollection: () => ({ clear() {}, set() {}, dispose() {} }),
      registerCompletionItemProvider: (_selector: unknown, provider: any, ...triggers: string[]) => {
        registrations.push({ provider, triggers }); return disposable();
      } }, commands: { registerCommand: disposable }, Uri: { file: fileUri },
    Position, Range, CompletionItem, CompletionList, CompletionItemKind: kinds, TabInputText, TabInputTextDiff,
    RelativePattern: class { constructor(readonly base: string, readonly pattern: string) {} },
    CodeActionKind: { QuickFix: "quickfix" }
  };
  for (const name of ["onDidOpenTextDocument", "onDidChangeTextDocument", "onDidSaveTextDocument", "onDidCloseTextDocument",
    "onDidChangeWorkspaceFolders", "onDidRenameFiles", "onDidDeleteFiles", "onDidGrantWorkspaceTrust", "onDidChangeConfiguration"]) api.workspace[name] = disposable;
  for (const name of ["registerCodeActionsProvider", "registerDefinitionProvider", "registerReferenceProvider", "registerRenameProvider", "registerSignatureHelpProvider"]) api.languages[name] = disposable;
  const module = { exports: {} as any };
  new Script(bundled, { filename: "completion-owner-production.cjs" }).runInNewContext({
    module, exports: module.exports, require: (name: string) => name === "vscode" ? api : name === "vscode-languageclient/node" ?
      { LanguageClient: Client, RevealOutputChannelOn: { Error: 1 }, CompletionRequest: { type: { method: "textDocument/completion" } } } : require(name),
    process, Buffer, TextDecoder, AbortController, setTimeout, clearTimeout
  });
  try {
    analyzer = new module.exports.NyAnalyzer((...args: unknown[]) => manager.provideCompletionItems(...args));
    manager = new module.exports.NyLanguageClientManager((doc: Document) => analyzer.usesCompilerDefinitions(doc));
    await manager.start();
    const text = '# é😀\r\nfn main() -> i32\r\n let cafe_value = 7\r\n return cafe_unfinished\r\nend fn\r\n';
    const filename = path.join(directory, "main.ny"); await fs.writeFile(filename, text);
    const entry = document(await fs.realpath(filename), text); documents.push(entry); tabs.push({ input: new TabInputText(entry.uri) });
    analyzer.tabSources = analyzer.openTabSources();
    analyzer.analyzeSnapshot = async (job: any, _signal: unknown, _input: unknown, query: any) => {
      state.compilerCalls++;
      await analyzer.workspaceInput(job);
      state.compilerStarted?.resolve(undefined);
      if (state.compilerGate) await state.compilerGate.promise;
      if (state.compilerFailures-- > 0) throw new Error("compiler unavailable");
      if (state.compilerUnsupported) return {};
      const sources = job.inputs.map((input: any) => ({ path: input.identity, text: input.text }));
      if (state.extraSource) sources.push(state.extraSource);
      state.compilerProduced?.resolve(undefined);
      return { completion: { schema: "nil.completion.v1", partial: true, path: job.inputs[0].identity, cursor: query.offset,
        start: Buffer.byteLength(job.text.slice(0, job.text.indexOf("cafe_unfinished"))), length: "cafe_unfinished".length,
        sources, items: state.compilerEmpty ? [] : [{ name: "cafe_value", kind: "variable", type: "i32", insert_text: "cafe_value" }] } };
    };
    const position = entry.positionAt(text.indexOf("cafe_unfinished") + "cafe_".length);
    const token = { isCancellationRequested: false, onCancellationRequested: disposable };
    const context = { triggerKind: 1 };
    const query = () => registrations[0]!.provider.provideCompletionItems(entry, position, token, context);
    await operation({ analyzer, manager, entry, documents, tabs, position, token, context, query, state, fallback, registrations,
      client: Client.instances[0], clients: Client.instances, directory });
  } finally {
    state.compilerGate?.resolve(undefined); state.lspGate?.resolve(undefined); state.conversionGate?.resolve(undefined);
    await manager?.stop(); analyzer?.dispose();
    await fs.rm(directory, { recursive: true, force: true });
  }
}

test("one production completion registration publishes compiler and tolerant candidates without a second LSP publisher", async () => {
  await fixture(async ({ registrations, client, query, entry, position, token, context, state }) => {
    assert.equal(registrations.length, 1);
    assert.deepEqual(registrations[0].triggers, [".", ":", "_", "-"]);
    let nextCalled = false;
    const legacy = await client.options.middleware.provideCompletionItem(entry, position, context, token, () => { nextCalled = true; return []; });
    assert.equal(legacy, undefined); assert.equal(nextCalled, false);
    const result = await query();
    assert.deepEqual(Array.from(result.items, (item: CompletionItem) => item.label), ["cafe_value", "return", "template"]);
    assert.equal(result.items[0].detail, "variable: i32");
    assert.equal(result.items[0].insertText, "cafe_value");
    assert.equal(entry.getText(result.items[0].range), "cafe_unfinished");
    assert.equal(state.compilerCalls, 1); assert.equal(state.lspCalls, 1); assert.equal(state.conversions, 1);
  });
});

for (const first of ["compiler", "lsp"]) test(`completion arbitration is unique when ${first} finishes first`, async () => {
  await fixture(async ({ query, state }) => {
    state.compilerGate = deferred<void>(); state.lspGate = deferred<void>();
    state.compilerStarted = deferred<void>(); state.lspStarted = deferred<void>();
    state.compilerProduced = deferred<void>(); state.lspProduced = deferred<void>();
    const pending = query(); await Promise.all([state.compilerStarted.promise, state.lspStarted.promise]);
    state[first + "Gate"].resolve(undefined);
    await state[first + "Produced"].promise;
    state[first === "compiler" ? "lspGate" : "compilerGate"].resolve(undefined);
    const result = await pending;
    assert.equal(result.items.filter((item: CompletionItem) => item.label === "cafe_value").length, 1);
  });
});

test("a supported empty compiler answer suppresses guessed symbols while keeping language items", async () => {
  await fixture(async ({ query, state }) => {
    state.compilerEmpty = true;
    assert.deepEqual(Array.from((await query()).items, (item: CompletionItem) => item.label), ["return", "template"]);
  });
});

test("an unavailable compiler preserves full fallback and recovers at the same cursor without source edits", async () => {
  await fixture(async ({ query, state, fallback, entry }) => {
    state.compilerFailures = 1;
    assert.equal(await query(), fallback);
    assert.equal((await query()).items[0].detail, "variable: i32");
    assert.equal(state.compilerCalls, 2); assert.equal(entry.version, 1);
  });
});

for (const array of [false, true]) test(`an older compiler preserves the complete tolerant ${array ? "array" : "list"} result`, async () => {
  await fixture(async ({ query, state, fallback }) => {
    state.compilerUnsupported = true; state.fallbackArray = array;
    assert.equal(await query(), array ? fallback.items : fallback);
    state.compilerUnsupported = false;
    assert.equal((await query()).items[0].detail, "variable: i32", "Unsupported results must not be cached indefinitely.");
  });
});

for (const mode of ["analysis-disabled", "closed-tab", "unsupported-context"])
  test(`${mode} preserves ordinary tolerant completion`, async () => {
    await fixture(async ({ query, state, fallback, analyzer, entry, token, context }) => {
      if (mode === "analysis-disabled") state.analysis = false;
      if (mode === "closed-tab") analyzer.tabSources.clear();
      const result = mode === "unsupported-context" ? await analyzer.provideCompletionItems(entry, new Position(0, 0), token, context) : await query();
      assert.equal(result, fallback); assert.equal(state.compilerCalls, 0);
    });
  });

for (const mode of ["disabled", "failed", "throwing-callback"])
  test(`compiler completion remains available with ${mode} LSP`, async () => {
    await fixture(async ({ query, state, manager, analyzer }) => {
      if (mode === "disabled") await manager.stop();
      else if (mode === "failed") state.lspError = true;
      else analyzer.tolerantCompletions = () => { throw new Error("synchronous LSP boundary failure"); };
      const result = await query();
      assert.deepEqual(Array.from(result.items, (item: CompletionItem) => item.label), ["cafe_value"]);
    });
  });

for (const unavailable of [false, true]) for (const change of ["version", "generation", "cancel", "other-model", "local-include"])
  test(`${unavailable ? "fallback" : "compiler"} completion rejects ${change} changes while LSP is pending`, async () => {
    await fixture(async ({ query, state, entry, analyzer, token, documents, directory }) => {
      state.compilerFailures = unavailable ? 1 : 0;
      state.lspGate = deferred<void>(); state.lspStarted = deferred<void>();
      const include = document(path.join(directory, "helper.inc"), "original");
      documents.push(include);
      const pending = query(); await state.lspStarted.promise;
      if (change === "version") entry.version++;
      else if (change === "generation") analyzer.generation++;
      else if (change === "cancel") token.isCancellationRequested = true;
      else if (change === "other-model") documents.push(document(path.join(directory, "new.inc"), "changed"));
      else { include.version++; include.isDirty = true; include.text = "edited"; }
      state.lspGate.resolve(undefined);
      assert.equal(await pending, undefined);
    });
  });

test("unrelated virtual output changes do not discard compiler completion", async () => {
  await fixture(async ({ query, state, documents, directory }) => {
    const output = document(path.join(directory, "analysis.log"), "before"); output.uri.scheme = "output";
    documents.push(output);
    state.lspGate = deferred<void>(); state.lspStarted = deferred<void>();
    const pending = query(); await state.lspStarted.promise;
    output.version++; output.text = "more output";
    const virtual = document(path.join(directory, "another.log"), "new output"); virtual.uri.scheme = "output";
    documents.push(virtual);
    state.lspGate.resolve(undefined);
    assert.equal((await pending).items[0].detail, "variable: i32");
  });
});

test("compiler source bytes are revalidated after a delayed tolerant response", async () => {
  await fixture(async ({ query, state, directory }) => {
    const header = path.join(directory, "helper.inc"); await fs.writeFile(header, "original");
    state.extraSource = { path: await fs.realpath(header), text: "original" };
    state.lspGate = deferred<void>(); state.lspStarted = deferred<void>();
    state.compilerStarted = deferred<void>();
    const pending = query(); await Promise.all([state.lspStarted.promise, state.compilerStarted.promise]);
    await fs.writeFile(header, "modified");
    state.lspGate.resolve(undefined);
    assert.equal(await pending, undefined, "A stale compiler answer must not become full tolerant fallback.");
  });
});

for (const phase of ["request", "conversion"]) test(`LSP lifecycle changes discard old ${phase} results`, async () => {
  await fixture(async ({ manager, entry, position, token, context, state }) => {
    const gate = deferred<void>(); state[phase === "request" ? "lspGate" : "conversionGate"] = gate;
    state.lspStarted = deferred<void>();
    const pending = manager.provideCompletionItems(entry, position, token, context);
    await state.lspStarted.promise;
    if (phase === "conversion") while (!state.conversions) await new Promise(resolve => setImmediate(resolve));
    await manager.restart(); gate.resolve(undefined);
    assert.equal(await pending, undefined);
  });
});
