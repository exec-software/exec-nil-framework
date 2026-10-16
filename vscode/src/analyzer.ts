import { execFile } from "node:child_process";
import { createHash, randomUUID } from "node:crypto";
import * as os from "node:os";
import * as path from "node:path";
import * as vscode from "vscode";

import { analysisInvocation, AnalysisResult, createByteRangeMapper, maxAnalysisOutputBytes, parseAnalysisOutput } from "./analysisModel";
import { languageDocumentSelector, savedSourcePath } from "./languages";
import { expandConfiguredPath } from "./toolchain";
import { configuredSdkPath, nyConfiguration, resolveConfiguredTool, toolNotFoundMessage } from "./vscodeToolchain";

const debounceMilliseconds = 400;
const timeoutMilliseconds = 10_000;
const maxSourceBytes = 2 * 1024 * 1024;

interface AnalysisJob {
  document: vscode.TextDocument;
  version: number;
  text: string;
  source: string;
  key: string;
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
export class NyAnalyzer implements vscode.Disposable, vscode.CodeActionProvider {
  private readonly diagnostics = vscode.languages.createDiagnosticCollection("nil-analysis");
  private readonly output = vscode.window.createOutputChannel("NIL Framework Analysis");
  private readonly subscriptions: vscode.Disposable[] = [];
  private readonly timers = new Map<string, ReturnType<typeof setTimeout>>();
  private readonly pending = new Map<string, AnalysisJob>();
  private readonly fixes = new Map<string, CachedFix>();
  private readonly reports = new Map<string, string>();
  private active: { job: AnalysisJob; controller: AbortController } | undefined;
  private disposed = false;

  constructor() {
    this.subscriptions.push(
      vscode.languages.registerCodeActionsProvider(languageDocumentSelector, this, {
        providedCodeActionKinds: [vscode.CodeActionKind.QuickFix]
      }),
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
      vscode.workspace.onDidOpenTextDocument(document => this.schedule(document)),
      vscode.workspace.onDidChangeTextDocument(event => this.schedule(event.document)),
      // A saved import can affect any open entry document.
      vscode.workspace.onDidSaveTextDocument(document => {
        if (this.supported(document)) this.scheduleAll();
      }),
      vscode.workspace.onDidCloseTextDocument(document => this.invalidate(document.uri)),
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
      this.supported(document) && nyConfiguration(document.uri).get<boolean>("analysis.enabled", true);
  }

  private current(job: AnalysisJob): boolean {
    return this.enabled(job.document) && job.document.version === job.version;
  }

  private workspaceRoot(document: vscode.TextDocument): string | undefined {
    return (vscode.workspace.getWorkspaceFolder(document.uri) ?? vscode.workspace.workspaceFolders?.[0])?.uri.fsPath;
  }

  private logicalSource(document: vscode.TextDocument): string {
    if (document.uri.scheme === "file") return document.uri.fsPath;
    const digest = createHash("sha256").update(document.uri.toString()).digest("hex").slice(0, 20);
    return path.join(this.workspaceRoot(document) ?? os.tmpdir(), `.nil-editor-${digest}.${document.languageId === "matrix" ? "matrix" : "ny"}`);
  }

  private invalidate(uri: vscode.Uri): void {
    const key = uri.toString();
    const timer = this.timers.get(key);
    if (timer) clearTimeout(timer);
    this.timers.delete(key);
    this.pending.get(key)?.resolve(false);
    this.pending.delete(key);
    if (this.active?.job.key === key) this.active.controller.abort();
    this.diagnostics.delete(uri);
    for (const [token, fix] of this.fixes) if (fix.job.key === key) this.fixes.delete(token);
    if (this.reports.delete(key)) this.renderOutput();
  }

  private scheduleAll(): void {
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
    if (Buffer.byteLength(text, "utf8") > maxSourceBytes) {
      this.report(key, "Analysis skipped: this document exceeds the 2 MiB editor analysis limit.");
      return Promise.resolve(false);
    }
    return new Promise(resolve => {
      this.pending.get(key)?.resolve(false);
      this.pending.set(key, { document, key, text, version: document.version, source: this.logicalSource(document), resolve });
      void this.drain();
    });
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
    const tool = resolveConfiguredTool("nilcompiler", job.document.uri);
    if (!tool.executable) throw new Error(toolNotFoundMessage("nilcompiler", tool));
    const configuration = nyConfiguration(job.document.uri);
    const workspaceRoot = this.workspaceRoot(job.document);
    const configuredRoots = configuration.get<unknown>("analysis.includePaths", []);
    const roots = [
      ...(workspaceRoot ? [workspaceRoot] : []),
      ...(Array.isArray(configuredRoots) ? configuredRoots.filter((root): root is string => typeof root === "string" && root.trim() !== "") : [])
        .map(root => path.resolve(workspaceRoot ?? path.dirname(job.source), expandConfiguredPath(root, workspaceRoot)))
    ];
    const sdk = configuredSdkPath(job.document.uri);
    const sdkRoot = sdk ? path.resolve(workspaceRoot ?? process.cwd(), sdk) : undefined;
    const invocation = analysisInvocation(tool.executable, job.source, sdkRoot,
      configuration.get<boolean>("compiler.noStdlib", false), roots);
    const result = await new Promise<AnalysisResult>((resolve, reject) => {
      const child = execFile(invocation.command, [...invocation.args], {
        cwd: path.dirname(job.source), encoding: "utf8", windowsHide: true,
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
      child.stdin?.end(job.text, "utf8");
    });
    if (signal.aborted || !this.current(job)) return false;
    if (result.language !== job.document.languageId) throw new Error("Compiler analyzed the document as a different language.");
    this.publish(job, result);
    return true;
  }

  private publish(job: AnalysisJob, result: AnalysisResult): void {
    const mapRange = createByteRangeMapper(job.text);
    const diagnostics: vscode.Diagnostic[] = [];
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
      const offsets = finding.path !== null && path.normalize(finding.path) === path.normalize(job.source) &&
        finding.start !== null && finding.length !== null ? mapRange(finding.start, finding.length) : undefined;
      const range = editorRange(offsets);
      if (!range) {
        messages.push(`${finding.severity}[${finding.code}] ${finding.message}\n  ${finding.path ?? "Location unavailable"}${finding.start !== null ? ` (UTF-8 byte ${finding.start})` : ""}${finding.hint ? `\n  ${finding.hint}` : ""}`);
        continue;
      }
      const diagnostic = new vscode.Diagnostic(range,
        `${finding.message}${finding.hint ? `\n${finding.hint}` : ""}`, severity[finding.severity]);
      diagnostic.source = "nil-analysis";
      diagnostic.code = finding.code;
      diagnostics.push(diagnostic);
      const fixOffsets = finding.fix ? mapRange(finding.fix.start, finding.fix.length) : undefined;
      const fixRange = editorRange(fixOffsets);
      if (finding.fix && fixRange) this.fixes.set(randomUUID(), {
        job, diagnostic, description: finding.fix.description, replacement: finding.fix.replacement,
        range: fixRange
      });
    }
    this.diagnostics.set(job.document.uri, diagnostics);
    if (messages.length) this.report(job.key, messages.join("\n\n"));
    else if (this.reports.delete(job.key)) this.renderOutput();
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
    this.subscriptions.forEach(subscription => subscription.dispose());
    this.diagnostics.dispose();
    this.output.dispose();
  }
}
