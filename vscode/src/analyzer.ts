import { execFile } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";
import * as os from "node:os";
import { constants as fileConstants, promises as fs } from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

import { analysisInvocation, AnalysisResult, createByteRangeMapper, maxAnalysisOutputBytes, maxAnalysisSourceBytes,
  parseAnalysisOutput, serializeAnalysisWorkspace } from "./analysisModel";
import { projectForResource } from "./projectContext";
import { analysisSourcePath, languageDocumentSelector, savedSourcePath, sourceLanguage } from "./languages";
import { expandConfiguredPath } from "./toolchain";
import { configuredSdkPath, nyConfiguration, resolveConfiguredTool, toolNotFoundMessage } from "./vscodeToolchain";
import { definitionAt, SemanticIndex, SemanticSource } from "./semanticModel";

const debounceMilliseconds = 400;
const timeoutMilliseconds = 10_000;

interface AnalysisInput {
  document: vscode.TextDocument;
  version: number;
  text: string;
  source: string;
  identity: string;
}

interface AnalysisJob {
  document: vscode.TextDocument;
  version: number;
  text: string;
  source: string;
  key: string;
  generation: number;
  inputs: AnalysisInput[];
  resolve: (applied: boolean) => void;
}

interface CachedFix {
  job: AnalysisJob;
  diagnostic: vscode.Diagnostic;
  range: vscode.Range;
  replacement: string;
  description: string;
}

/** Compiler analysis is independent of nillsp's deliberately partial parser. */
export class NyAnalyzer implements vscode.Disposable, vscode.CodeActionProvider, vscode.DefinitionProvider {
  private readonly diagnostics = vscode.languages.createDiagnosticCollection("nil-analysis");
  private readonly output = vscode.window.createOutputChannel("NIL Framework Analysis");
  private readonly subscriptions: vscode.Disposable[] = [];
  private readonly timers = new Map<string, ReturnType<typeof setTimeout>>();
  private readonly pending = new Map<string, AnalysisJob>();
  private readonly fixes = new Map<string, CachedFix>();
  private readonly importWatchers = new Map<string, vscode.FileSystemWatcher>();
  private readonly contributions = new Map<string, Map<string, vscode.Diagnostic[]>>();
  private readonly reports = new Map<string, string>();
  private readonly semantics = new Map<string, { job: AnalysisJob; index: SemanticIndex }>();
  private active: { job: AnalysisJob; controller: AbortController } | undefined;
  private disposed = false;
  private generation = 0;
  private tabSources = new Set<string>();

  constructor() {
    this.tabSources = this.openTabSources();
    const tabMembershipChanged = () => {
      const sources = this.openTabSources();
      if (sources.size !== this.tabSources.size || [...sources].some(source => !this.tabSources.has(source))) {
        this.tabSources = sources;
        this.scheduleAll();
      }
    };
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.{ny,matrix,mat,json}");
    const changed = (uri: vscode.Uri) => {
      if (/\.(ny|matrix|mat)$/i.test(uri.fsPath) || path.basename(uri.fsPath) === "nil.project.json") this.scheduleAll();
    };
    this.subscriptions.push(watcher, watcher.onDidChange(changed), watcher.onDidCreate(changed), watcher.onDidDelete(changed),
      vscode.languages.registerCodeActionsProvider(languageDocumentSelector, this, {
        providedCodeActionKinds: [vscode.CodeActionKind.QuickFix]
      }),
      vscode.languages.registerDefinitionProvider(languageDocumentSelector, this),
      vscode.commands.registerCommand("ny.applyAnalysisFix", (token: unknown) => this.applyFix(token)),
      vscode.commands.registerCommand("ny.analyzeCurrentFile", async () => {
        const document = vscode.window.activeTextEditor?.document;
        if (document && this.supported(document)) {
          const applied = await this.analyzeNow(document);
          if (!applied || this.reports.has(document.uri.toString())) this.showOutput();
          return applied;
        }
        void vscode.window.showInformationMessage("Open a Ny or Matrix document to check its current contents.");
        return false;
      }),
      vscode.commands.registerCommand("ny.showAnalysisOutput", () => this.showOutput()),
      vscode.workspace.onDidOpenTextDocument(document => {
        if (this.supported(document) || path.basename(document.uri.fsPath) === "nil.project.json") this.scheduleAll();
      }),
      vscode.workspace.onDidChangeTextDocument(event => {
        if (this.supported(event.document) || path.basename(event.document.uri.fsPath) === "nil.project.json") this.scheduleAll();
      }),
      // A saved import can affect any open entry document.
      vscode.workspace.onDidSaveTextDocument(document => {
        if (this.supported(document) || path.basename(document.uri.fsPath) === "nil.project.json") this.scheduleAll();
      }),
      vscode.workspace.onDidCloseTextDocument(document => {
        if (this.supported(document) || path.basename(document.uri.fsPath) === "nil.project.json") {
          this.invalidate(document.uri); this.scheduleAll();
        }
      }),
      vscode.window.tabGroups.onDidChangeTabs(tabMembershipChanged),
      vscode.window.tabGroups.onDidChangeTabGroups(tabMembershipChanged),
      vscode.workspace.onDidChangeWorkspaceFolders(() => this.scheduleAll()),
      vscode.workspace.onDidRenameFiles(() => this.scheduleAll()),
      vscode.workspace.onDidDeleteFiles(() => this.scheduleAll()),
      vscode.workspace.onDidGrantWorkspaceTrust(() => this.scheduleAll()),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (["ny.analysis", "ny.compiler.path", "ny.compiler.noStdlib", "ny.sdkPath"].some(key => event.affectsConfiguration(key))) {
          this.scheduleAll();
        }
      })
    );
    this.scheduleAll();
  }

  private supported(document: vscode.TextDocument): boolean {
    return (document.languageId === "ny" || document.languageId === "matrix") &&
      (document.uri.scheme === "untitled" || savedSourcePath(document) !== undefined);
  }

  private enabled(document: vscode.TextDocument): boolean {
    return !this.disposed && !document.isClosed && vscode.workspace.isTrusted &&
      (document.isDirty || this.tabSources.has(document.uri.toString())) &&
      this.supported(document) && nyConfiguration(this.contextUri(document)).get<boolean>("analysis.enabled", true);
  }

  usesCompilerDefinitions(document: vscode.TextDocument): boolean { return this.enabled(document); }

  private openTabSources(): Set<string> {
    const sources = new Set<string>();
    const add = (uri: vscode.Uri) => {
      if (uri.scheme === "untitled" || (uri.scheme === "file" && sourceLanguage(uri.fsPath))) sources.add(uri.toString());
    };
    for (const group of vscode.window.tabGroups.all) for (const tab of group.tabs) {
      if (tab.input instanceof vscode.TabInputText) add(tab.input.uri);
      else if (tab.input instanceof vscode.TabInputTextDiff) {
        add(tab.input.original);
        add(tab.input.modified);
      }
    }
    return sources;
  }

  private current(job: AnalysisJob): boolean {
    return this.enabled(job.document) && job.generation === this.generation && job.document.version === job.version &&
      job.inputs.every(input => !input.document.isClosed && input.document.version === input.version);
  }

  private contextUri(document: vscode.TextDocument): vscode.Uri {
    const source = analysisSourcePath(document);
    return source ? vscode.Uri.file(source) : document.uri;
  }

  private workspaceRoot(document: vscode.TextDocument): string | undefined {
    return (vscode.workspace.getWorkspaceFolder(this.contextUri(document)) ?? vscode.workspace.workspaceFolders?.[0])?.uri.fsPath;
  }

  private logicalSource(document: vscode.TextDocument): string {
    const named = analysisSourcePath(document);
    if (named) return named;
    const digest = createHash("sha256").update(document.uri.toString()).digest("hex").slice(0, 20);
    return path.join(this.workspaceRoot(document) ?? os.tmpdir(), `.nil-editor-${digest}.${document.languageId === "matrix" ? "matrix" : "ny"}`);
  }

  private invalidate(uri: vscode.Uri): void {
    const key = uri.toString();
    this.semantics.delete(key);
    const timer = this.timers.get(key);
    if (timer) clearTimeout(timer);
    this.timers.delete(key);
    this.pending.get(key)?.resolve(false);
    this.pending.delete(key);
    if (this.active?.job.key === key) this.active.controller.abort();
    this.contributions.delete(key);
    this.renderDiagnostics();
    for (const [token, fix] of this.fixes) if (fix.job.key === key) this.fixes.delete(token);
    if (this.reports.delete(key)) this.renderOutput();
  }

  private scheduleAll(): void {
    this.generation++;
    for (const document of vscode.workspace.textDocuments) this.schedule(document);
  }

  private schedule(document: vscode.TextDocument): void {
    this.invalidate(document.uri);
    if (!this.enabled(document)) return;
    const key = document.uri.toString();
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      void this.enqueue(document);
    }, debounceMilliseconds));
  }

  async analyzeNow(document: vscode.TextDocument): Promise<boolean> {
    this.invalidate(document.uri);
    if (!this.enabled(document)) {
      this.report(document.uri.toString(), "Analysis requires an enabled, trusted workspace and a Ny or Matrix document.");
      return false;
    }
    return this.enqueue(document);
  }

  private enqueue(document: vscode.TextDocument): Promise<boolean> {
    if (!this.enabled(document)) return Promise.resolve(false);
    const key = document.uri.toString();
    const text = document.getText();
    if (Buffer.byteLength(text, "utf8") > maxAnalysisSourceBytes) {
      this.report(key, "Analysis skipped: this document exceeds the 2 MiB editor analysis limit.");
      return Promise.resolve(false);
    }
    return new Promise(resolve => {
      this.pending.get(key)?.resolve(false);
      this.pending.set(key, { document, key, text, version: document.version, source: this.logicalSource(document),
        generation: this.generation, inputs: [], resolve });
      void this.drain();
    });
  }

  private async sourceIdentity(source: string): Promise<string> {
    let ancestor = path.resolve(source);
    const missing: string[] = [];
    for (;;) {
      try { return path.normalize(path.join(await fs.realpath(ancestor), ...missing)); }
      catch (error) {
        if ((error as NodeJS.ErrnoException).code !== "ENOENT" || path.dirname(ancestor) === ancestor) return path.normalize(source);
        missing.unshift(path.basename(ancestor));
        ancestor = path.dirname(ancestor);
      }
    }
  }

  private async workspaceInput(job: AnalysisJob): Promise<string> {
    // A clean TextDocument can remain cached long after its tab closes. Only
    // open tabs and dirty buffers are editor inputs; never omit dirty imports.
    // Import resolution decides which of these named sources are consumed.
    job.inputs = [job.document, ...vscode.workspace.textDocuments.filter(document => document !== job.document &&
      this.supported(document) && analysisSourcePath(document) !== undefined &&
      (document.isDirty || this.tabSources.has(document.uri.toString())))].map(document => ({
        document, version: document.version, text: document === job.document ? job.text : document.getText(),
        source: document === job.document ? job.source : analysisSourcePath(document)!, identity: ""
      }));
    await Promise.all(job.inputs.map(async input => { input.identity = await this.sourceIdentity(input.source); }));
    const unique = new Map<string, AnalysisInput>();
    for (const input of job.inputs) {
      const previous = unique.get(input.identity);
      if (previous && previous.text !== input.text) {
        throw new Error(`Open aliases of the same source contain different text: ${previous.source} and ${input.source}`);
      }
      if (!previous) unique.set(input.identity, input);
    }
    return serializeAnalysisWorkspace([...unique.values()].map(input => ({ path: input.source, text: input.text })));
  }

  private async workingDirectory(preferred: string): Promise<string> {
    let directory = preferred;
    for (;;) {
      try { if ((await fs.stat(directory)).isDirectory()) return directory; }
      catch (error) { if (!["ENOENT", "ENOTDIR"].includes((error as NodeJS.ErrnoException).code ?? "")) throw error; }
      const parent = path.dirname(directory);
      if (parent === directory) throw new Error(`No existing working directory for ${preferred}`);
      directory = parent;
    }
  }

  // Only one compiler runs at a time, even when a workspace opens many files.
  private async drain(): Promise<void> {
    if (this.active || this.disposed) return;
    const job = this.pending.values().next().value as AnalysisJob | undefined;
    if (!job) return;
    this.pending.delete(job.key);
    const controller = new AbortController();
    this.active = { job, controller };
    try {
      job.resolve(await this.check(job, controller.signal));
    } catch (error) {
      if (!controller.signal.aborted && this.current(job)) this.report(job.key, `Analysis unavailable: ${error instanceof Error ? error.message : String(error)}`);
      job.resolve(false);
    } finally {
      this.active = undefined;
      void this.drain();
    }
  }

  private async check(job: AnalysisJob, signal: AbortSignal): Promise<boolean> {
    if (!this.current(job) || signal.aborted) return false;
    const context = this.contextUri(job.document);
    const project = projectForResource(context);
    if (project && vscode.workspace.textDocuments.some(document => document.uri.scheme === "file" &&
      path.normalize(document.uri.fsPath) === path.normalize(project.projectFile) && document.isDirty)) {
      throw new Error("Save nil.project.json before checking sources with its project settings.");
    }
    const configuredSdk = configuredSdkPath(context);
    const workspaceRoot = this.workspaceRoot(job.document);
    const sdkRoot = project?.sdkPath ?? (configuredSdk ? path.resolve(workspaceRoot ?? process.cwd(), configuredSdk) : undefined);
    const tool = resolveConfiguredTool("nilcompiler", context, sdkRoot);
    if (!tool.executable) throw new Error(toolNotFoundMessage("nilcompiler", tool));
    const configuration = nyConfiguration(context);
    const configuredRoots = configuration.get<unknown>("analysis.includePaths", []);
    const roots = [
      ...(project ? project.includePaths : workspaceRoot ? [workspaceRoot] : []),
      ...(!project && Array.isArray(configuredRoots) ? configuredRoots.filter((root): root is string => typeof root === "string" && root.trim() !== "") : [])
        .map(root => path.resolve(workspaceRoot ?? path.dirname(job.source), expandConfiguredPath(root, workspaceRoot)))
    ];
    this.watchImportRoots([...roots, ...(sdkRoot ? [sdkRoot] : [])]);
    const invocation = analysisInvocation(tool.executable, job.source, sdkRoot,
      !project && configuration.get<boolean>("compiler.noStdlib", false), roots, project?.analysis, !project);
    const input = await this.workspaceInput(job);
    const cwd = await this.workingDirectory(project?.directory ?? path.dirname(job.source));
    if (signal.aborted || !this.current(job)) return false;
    const result = await new Promise<AnalysisResult>((resolve, reject) => {
      const child = execFile(invocation.command, [...invocation.args], {
        cwd, encoding: "utf8", windowsHide: true,
        timeout: timeoutMilliseconds, maxBuffer: maxAnalysisOutputBytes, killSignal: "SIGKILL", signal
      }, (error, stdout, stderr) => {
        if (signal.aborted) { reject(new Error("Analysis cancelled.")); return; }
        if (error && (error.code !== 1 || error.killed || error.signal)) {
          reject(new Error(`${error.message}${stderr.trim() ? `\n${stderr.trim().slice(0, 2000)}` : ""}`));
          return;
        }
        try {
          // Keep a failed compiler's human-readable stderr out of the JSON protocol.
          resolve(parseAnalysisOutput(stdout, job.source, error ? 1 : 0));
        } catch (parseError) {
          reject(new Error(`${parseError instanceof Error ? parseError.message : String(parseError)}${stderr.trim() ? `\n${stderr.trim().slice(0, 2000)}` : ""}`));
        }
      });
      child.stdin?.on("error", () => { /* A cancelled or failed compiler may close stdin early. */ });
      child.stdin?.end(input, "utf8");
    });
    if (signal.aborted || !this.current(job)) return false;
    if (result.language !== job.document.languageId) throw new Error("Compiler analyzed the document as a different language.");
    return this.publish(job, result, signal);
  }

  private watchImportRoots(roots: readonly string[]): void {
    for (const root of roots) {
      const directory = path.resolve(root);
      if (vscode.workspace.getWorkspaceFolder(vscode.Uri.file(directory)) || this.importWatchers.has(directory)) continue;
      const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(directory, "**/*.{ny,matrix,mat}"));
      this.importWatchers.set(directory, watcher);
      this.subscriptions.push(watcher, watcher.onDidCreate(() => this.scheduleAll()),
        watcher.onDidChange(() => this.scheduleAll()), watcher.onDidDelete(() => this.scheduleAll()));
    }
  }

  private watchSemanticSources(index: SemanticIndex): void {
    for (const source of index.sources) {
      const key = `source:${source.path}`;
      if (this.importWatchers.has(key)) continue;
      const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(path.dirname(source.path), path.basename(source.path)));
      this.importWatchers.set(key, watcher);
      this.subscriptions.push(watcher, watcher.onDidCreate(() => this.scheduleAll()),
        watcher.onDidChange(() => this.scheduleAll()), watcher.onDidDelete(() => this.scheduleAll()));
    }
  }

  private async publish(job: AnalysisJob, result: AnalysisResult, signal: AbortSignal): Promise<boolean> {
    const imported = new Map<string, { uris: vscode.Uri[]; text: string; mapRange: ReturnType<typeof createByteRangeMapper> }>();
    for (const snapshot of result.sources ?? []) {
      // VS Code strips a leading BOM from its document model. Keep these
      // findings in Output until the protocol describes that normalization.
      if (snapshot.text.startsWith("\uFEFF")) continue;
      const normalized = path.normalize(snapshot.path);
      const inputs = job.inputs.filter(input => input.identity === normalized);
      if (inputs.length) {
        if (inputs.every(input => input.text === snapshot.text && !input.document.isClosed && input.document.version === input.version)) {
          imported.set(normalized, { uris: inputs.map(input => input.document.uri), text: snapshot.text, mapRange: createByteRangeMapper(snapshot.text) });
        }
        continue;
      }
      const editor = vscode.workspace.textDocuments.find(document => document.uri.scheme === "file" && path.normalize(document.uri.fsPath) === normalized);
      if (editor?.isDirty) continue;
      try {
        const expectedSize = Buffer.byteLength(snapshot.text, "utf8");
        const handle = await fs.open(snapshot.path, fileConstants.O_RDONLY | (fileConstants.O_NONBLOCK ?? 0));
        let disk: string;
        try {
          const stat = await handle.stat();
          if (!stat.isFile() || stat.size !== expectedSize) continue;
          // Bound the read even if another process replaces or grows the file.
          const bytes = Buffer.alloc(expectedSize + 1);
          let count = 0;
          while (count < bytes.length) {
            const result = await handle.read(bytes, count, bytes.length - count, count);
            if (result.bytesRead === 0) break;
            count += result.bytesRead;
          }
          if (count !== expectedSize) continue;
          disk = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes.subarray(0, count));
        } finally { await handle.close(); }
        if (disk !== snapshot.text || (editor && (editor.isDirty || editor.getText() !== snapshot.text))) continue;
        imported.set(normalized, { uris: [vscode.Uri.file(snapshot.path)], text: snapshot.text, mapRange: createByteRangeMapper(snapshot.text) });
      } catch { /* Removed, changed, or unreadable dependencies have no safe location. */ }
    }
    if (signal.aborted || !this.current(job)) return false;
    const mapRange = createByteRangeMapper(job.text);
    const byUri = new Map<string, vscode.Diagnostic[]>();
    byUri.set(job.key, []);
    const messages: string[] = [];
    const editorRange = (offsets: { start: number; end: number } | undefined): vscode.Range | undefined => {
      if (!offsets) return undefined;
      const start = job.document.positionAt(offsets.start);
      const end = job.document.positionAt(offsets.end);
      // Editor positions cannot represent an offset between CR and LF. Do not
      // let positionAt's clamping silently widen or shrink a compiler edit.
      return job.document.offsetAt(start) === offsets.start && job.document.offsetAt(end) === offsets.end
        ? new vscode.Range(start, end) : undefined;
    };
    const severity = { error: vscode.DiagnosticSeverity.Error, warning: vscode.DiagnosticSeverity.Warning,
      note: vscode.DiagnosticSeverity.Information, help: vscode.DiagnosticSeverity.Hint };
    for (const finding of result.diagnostics) {
      const primary = finding.path !== null && path.normalize(finding.path) === path.normalize(job.source);
      const snapshot = finding.path ? imported.get(path.normalize(finding.path)) : undefined;
      const offsets = primary && finding.start !== null && finding.length !== null ? mapRange(finding.start, finding.length) : undefined;
      let range = editorRange(offsets);
      if (!primary && snapshot && finding.start !== null && finding.length !== null) {
        const importedOffsets = snapshot.mapRange(finding.start, finding.length);
        if (importedOffsets) {
          const position = (offset: number): vscode.Position | undefined => {
            if (offset > 0 && snapshot.text[offset - 1] === "\r" && snapshot.text[offset] === "\n") return undefined;
            const lines = snapshot.text.slice(0, offset).split(/\r\n|\r|\n/);
            return new vscode.Position(lines.length - 1, lines[lines.length - 1]!.length);
          };
          const start = position(importedOffsets.start), end = position(importedOffsets.end);
          if (start && end) range = new vscode.Range(start, end);
        }
      }
      if (!range) {
        messages.push(`${finding.severity}[${finding.code}] ${finding.message}\n  ${finding.path ?? "Location unavailable"}${finding.start !== null ? ` (UTF-8 byte ${finding.start})` : ""}${finding.hint ? `\n  ${finding.hint}` : ""}`);
        continue;
      }
      const diagnostic = new vscode.Diagnostic(range,
        `${finding.message}${finding.hint ? `\n${finding.hint}` : ""}`, severity[finding.severity]);
      diagnostic.source = "nil-analysis";
      diagnostic.code = finding.code;
      for (const uri of primary ? [job.key] : snapshot!.uris.map(uri => uri.toString())) {
        if (!byUri.has(uri)) byUri.set(uri, []);
        byUri.get(uri)!.push(diagnostic);
      }
      const fixOffsets = primary && finding.fix ? mapRange(finding.fix.start, finding.fix.length) : undefined;
      const fixRange = editorRange(fixOffsets);
      if (finding.fix && fixRange) this.fixes.set(randomUUID(), {
        job, diagnostic, description: finding.fix.description, replacement: finding.fix.replacement,
        range: fixRange
      });
    }
    this.contributions.set(job.key, byUri);
    if (result.semantic) {
      this.watchSemanticSources(result.semantic);
      this.semantics.set(job.key, { job, index: result.semantic });
    }
    this.renderDiagnostics();
    if (messages.length) this.report(job.key, messages.join("\n\n"));
    else if (this.reports.delete(job.key)) this.renderOutput();
    return true;
  }

  private async matchesDiskSnapshot(snapshot: SemanticSource): Promise<boolean> {
    const size = Buffer.byteLength(snapshot.text, "utf8");
    try {
      const handle = await fs.open(snapshot.path, fileConstants.O_RDONLY | (fileConstants.O_NONBLOCK ?? 0));
      try {
        const stat = await handle.stat();
        if (!stat.isFile() || stat.size !== size) return false;
        const bytes = Buffer.alloc(size + 1);
        let count = 0;
        while (count < bytes.length) {
          const read = await handle.read(bytes, count, bytes.length - count, count);
          if (!read.bytesRead) break;
          count += read.bytesRead;
        }
        return count === size && new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes.subarray(0, count)) === snapshot.text;
      } finally { await handle.close(); }
    } catch { return false; }
  }

  async provideDefinition(document: vscode.TextDocument, position: vscode.Position, token: vscode.CancellationToken): Promise<vscode.LocationLink[] | undefined> {
    if (!this.enabled(document) || token.isCancellationRequested) return undefined;
    const version = document.version;
    const key = document.uri.toString();
    let cached = this.semantics.get(key);
    if (!cached || !this.current(cached.job)) {
      if (!await this.analyzeNow(document)) return undefined;
      cached = this.semantics.get(key);
    }
    if (!cached || token.isCancellationRequested || document.version !== version || !this.current(cached.job)) return undefined;
    const { job, index } = cached;
    const primary = job.inputs.find(input => input.document === document);
    if (!primary) return undefined;
    const match = definitionAt(index, primary.identity, document.offsetAt(position));
    if (!match) return undefined;
    // Textual includes can have other extensions and cannot be overlaid yet.
    // Observe their open models too, including aliases and edits during awaits.
    const localDocuments = (): vscode.TextDocument[] => vscode.workspace.textDocuments.filter(item =>
      (item.uri.scheme === "file" || item.uri.scheme === "untitled") && path.isAbsolute(item.uri.fsPath));
    const observed = localDocuments().map(item => ({ document: item, version: item.version, dirty: item.isDirty, identity: "" }));
    await Promise.all(observed.map(async item => { item.identity = await this.sourceIdentity(item.document.uri.fsPath); }));
    // Validate the complete exported input set, not only the destination. An
    // unchanged target can resolve differently after another dependency edit.
    for (const input of job.inputs) {
      if (await this.sourceIdentity(input.source) !== input.identity) {
        this.invalidate(document.uri);
        return undefined;
      }
    }
    for (const source of index.sources) {
      if (token.isCancellationRequested || !this.current(job) || source.text.startsWith("\uFEFF")) return undefined;
      if (await this.sourceIdentity(source.path) !== path.normalize(source.path)) {
        this.invalidate(document.uri);
        return undefined;
      }
      const inputs = job.inputs.filter(input => input.identity === path.normalize(source.path));
      const models = observed.filter(item => item.identity === path.normalize(source.path));
      if (!inputs.length && models.some(item => item.document.isDirty || item.document.getText() !== source.text)) return undefined;
      if (inputs.length) {
        if (inputs.some(input => input.text !== source.text)) return undefined;
      } else {
        if (!await this.matchesDiskSnapshot(source)) {
          this.invalidate(document.uri);
          return undefined;
        }
      }
    }
    if (token.isCancellationRequested || !this.current(job) || document.version !== version ||
        localDocuments().length !== observed.length || observed.some(item => item.document.isClosed ||
          item.document.version !== item.version || item.document.isDirty !== item.dirty)) return undefined;
    const range = (text: string, start: number, end: number): vscode.Range | undefined => {
      const point = (offset: number): vscode.Position | undefined => {
        if (offset > 0 && text[offset - 1] === "\r" && text[offset] === "\n") return undefined;
        const lines = text.slice(0, offset).split(/\r\n|\r|\n/);
        return new vscode.Position(lines.length - 1, lines[lines.length - 1]!.length);
      };
      const first = point(start), last = point(end);
      return first && last ? new vscode.Range(first, last) : undefined;
    };
    const originSelectionRange = range(job.text, match.origin.start, match.origin.end);
    const targetRange = range(match.targetText, match.targetOffsets.start, match.targetOffsets.end);
    if (!originSelectionRange || !targetRange) return undefined;
    const targetInput = job.inputs.find(input => input.identity === path.normalize(match.target.path));
    return [{ originSelectionRange, targetUri: targetInput?.document.uri ?? vscode.Uri.file(match.target.path), targetRange, targetSelectionRange: targetRange }];
  }

  private renderDiagnostics(): void {
    const combined = new Map<string, Map<string, vscode.Diagnostic>>();
    for (const report of this.contributions.values()) for (const [uri, items] of report) {
      if (!combined.has(uri)) combined.set(uri, new Map());
      for (const item of items) {
        const identity = JSON.stringify([item.code, item.message, item.severity, item.range.start, item.range.end]);
        combined.get(uri)!.set(identity, item);
      }
    }
    this.diagnostics.clear();
    for (const [uri, items] of combined) this.diagnostics.set(vscode.Uri.parse(uri), [...items.values()]);
  }

  provideCodeActions(document: vscode.TextDocument, range: vscode.Range, context: vscode.CodeActionContext): vscode.CodeAction[] {
    if (context.only && !vscode.CodeActionKind.QuickFix.contains(context.only) && !context.only.contains(vscode.CodeActionKind.QuickFix)) return [];
    const actions: vscode.CodeAction[] = [];
    for (const [token, fix] of this.fixes) {
      if (fix.job.key !== document.uri.toString() || !this.current(fix.job) || !fix.diagnostic.range.intersection(range)) continue;
      const action = new vscode.CodeAction(fix.description, vscode.CodeActionKind.QuickFix);
      action.diagnostics = [fix.diagnostic];
      // A command rechecks the version when invoked; a previously returned edit
      // could otherwise be applied after the user has changed this document.
      action.command = { title: fix.description, command: "ny.applyAnalysisFix", arguments: [token] };
      action.isPreferred = true;
      actions.push(action);
    }
    return actions;
  }

  private async applyFix(token: unknown): Promise<boolean> {
    if (typeof token !== "string") return false;
    const fix = this.fixes.get(token);
    if (!fix || !this.current(fix.job) || fix.job.document.getText() !== fix.job.text) return false;
    const edit = new vscode.WorkspaceEdit();
    edit.replace(fix.job.document.uri, fix.range, fix.replacement);
    return vscode.workspace.applyEdit(edit);
  }

  private report(key: string, message: string): void {
    this.reports.set(key, message);
    this.renderOutput();
  }

  private renderOutput(): void {
    this.output.clear();
    for (const [key, message] of this.reports) this.output.appendLine(`${key}\n${message}\n`);
  }

  showOutput(): void { this.output.show(true); }

  dispose(): void {
    this.disposed = true;
    this.active?.controller.abort();
    for (const timer of this.timers.values()) clearTimeout(timer);
    for (const job of this.pending.values()) job.resolve(false);
    this.timers.clear();
    this.pending.clear();
    this.fixes.clear();
    this.contributions.clear();
    this.semantics.clear();
    this.importWatchers.clear();
    this.subscriptions.forEach(subscription => subscription.dispose());
    this.diagnostics.dispose();
    this.output.dispose();
  }
}
