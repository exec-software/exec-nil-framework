import { spawn } from "node:child_process";
import * as path from "node:path";
import * as vscode from "vscode";
import { projectForResource } from "./projectContext";
import { isDirtySavedSource, savedSourcePath } from "./languages";
import { configuredSdkPath, nyConfiguration, resolveConfiguredTool, toolNotFoundMessage } from "./vscodeToolchain";
import { NilTestCase, parseTestDiscovery, parseTestResults } from "./testingModel";

interface TestTarget {
  root: vscode.TestItem;
  source: vscode.Uri;
  entry: string;
  cwd: string;
  items: Map<string, vscode.TestItem>;
}

// The runner owns test child processes. Give cancellation time to propagate,
// then kill its process group if a broken runner fails to clean up descendants.
function execute(command: string, args: string[], cwd: string, token: vscode.CancellationToken,
  timeout: number): Promise<{ code: number | null; stdout: string; stderr: string }> {
  return new Promise((resolve, reject) => {
    if (token.isCancellationRequested) { reject(new Error("Test operation cancelled.")); return; }
    const child = spawn(command, args, { cwd, shell: false, windowsHide: true,
      detached: process.platform !== "win32", stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "", stderr = "", bytes = 0, failure: Error | undefined;
    let killTimer: NodeJS.Timeout | undefined;
    const signal = (value: NodeJS.Signals): void => {
      try {
        if (process.platform !== "win32" && child.pid) process.kill(-child.pid, value);
        else child.kill(value);
      } catch { /* Process already exited. */ }
    };
    const stop = (message: string): void => {
      if (failure) return;
      failure = new Error(message);
      signal("SIGTERM");
      killTimer = setTimeout(() => signal("SIGKILL"), 2000);
    };
    const subscription = token.onCancellationRequested(() => stop("Test operation cancelled."));
    const deadline = setTimeout(() => stop("niltest exceeded the editor operation deadline."), timeout);
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    const collect = (chunk: string, error: boolean): void => {
      bytes += Buffer.byteLength(chunk);
      if (bytes > 32 * 1024 * 1024) { stop("niltest output exceeded 32 MiB."); return; }
      if (error) stderr += chunk; else stdout += chunk;
    };
    child.stdout.on("data", (chunk: string) => collect(chunk, false));
    child.stderr.on("data", (chunk: string) => collect(chunk, true));
    const cleanup = (): void => {
      clearTimeout(deadline);
      if (killTimer) clearTimeout(killTimer);
      subscription.dispose();
    };
    child.on("error", error => { cleanup(); reject(error); });
    child.on("close", code => {
      cleanup();
      if (failure) reject(failure); else resolve({ code, stdout, stderr });
    });
  });
}

export class NilTestIntegration implements vscode.Disposable {
  private readonly controller = vscode.tests.createTestController("niltest", "NIL tests");
  private readonly output = vscode.window.createOutputChannel("NIL tests");
  private readonly targets = new Map<string, TestTarget>();
  private readonly operations = new Set<vscode.CancellationTokenSource>();
  private readonly subscriptions: vscode.Disposable[] = [];
  private disposed = false;

  constructor() {
    this.controller.refreshHandler = token => this.refresh(token);
    for (const route of ["vm", "native"] as const) {
      this.subscriptions.push(this.controller.createRunProfile(
        route === "vm" ? "NIL interpreter" : "NIL native", vscode.TestRunProfileKind.Run,
        (request, token) => this.run(request, token, route), route === "vm"));
    }
    this.subscriptions.push(vscode.commands.registerCommand("ny.discoverTests", async () => {
      const cancellation = new vscode.CancellationTokenSource();
      this.operations.add(cancellation);
      try { return await this.discover(vscode.window.activeTextEditor?.document.uri, cancellation.token); }
      catch (error) { void vscode.window.showErrorMessage(String(error)); }
      finally { this.operations.delete(cancellation); cancellation.dispose(); }
    }));
  }

  dispose(): void {
    this.disposed = true;
    for (const operation of this.operations) { operation.cancel(); operation.dispose(); }
    this.operations.clear();
    for (const subscription of this.subscriptions) subscription.dispose();
    this.controller.dispose();
    this.output.dispose();
  }

  private trusted(): void {
    if (!vscode.workspace.isTrusted) throw new Error("Trust this workspace before discovering or running NIL tests.");
  }

  private invocation(source: vscode.Uri, entry: string, action: "list" | "run", route: "vm" | "native") {
    const project = projectForResource(source);
    const sdk = project?.sdkPath ?? configuredSdkPath(source);
    const tool = resolveConfiguredTool("niltest", source, sdk);
    const compiler = resolveConfiguredTool("nilcompiler", source, sdk);
    const interpreter = resolveConfiguredTool("nilinterpreter", source, sdk);
    if (!tool.executable) throw new Error(toolNotFoundMessage("niltest", tool));
    if (!compiler.executable) throw new Error(toolNotFoundMessage("nilcompiler", compiler));
    if (action === "run" && route === "vm" && !interpreter.executable) {
      throw new Error(toolNotFoundMessage("nilinterpreter", interpreter));
    }
    const args = [action, "--format", "json", "--compiler", compiler.executable];
    if (interpreter.executable) args.push("--interpreter", interpreter.executable);
    if (sdk) args.push("--sdk-path", sdk);
    for (const include of project?.includePaths ?? []) args.push("-I", include);
    if (nyConfiguration(source).get<boolean>("compiler.noStdlib", false)) args.push("--no-stdlib");
    if (action === "run") args.push("--target", route);
    args.push(entry);
    return { command: tool.executable, args };
  }

  private async discover(resource: vscode.Uri | undefined, token: vscode.CancellationToken): Promise<NilTestCase[]> {
    if (this.disposed) return [];
    this.trusted();
    const project = projectForResource(resource);
    const document = resource && vscode.workspace.textDocuments.find(item => item.uri.toString() === resource.toString());
    const entry = project?.entry ?? savedSourcePath(document);
    if (!entry) throw new Error("Open a saved Ny or Matrix source, or a file in a NIL project, to discover tests.");
    if (vscode.workspace.textDocuments.some(isDirtySavedSource)) {
      throw new Error("Save source changes before discovering tests; niltest reads the saved import graph.");
    }
    const source = vscode.Uri.file(entry);
    const key = source.toString();
    const root = this.targets.get(key)?.root ?? this.controller.createTestItem(key, project?.name ?? path.basename(entry), source);
    root.busy = true;
    root.error = undefined;
    this.controller.items.add(root);
    const target = { root, source, entry, cwd: project?.directory ?? path.dirname(entry), items: new Map<string, vscode.TestItem>() };
    this.targets.set(key, target);
    try {
      const invocation = this.invocation(source, entry, "list", "vm");
      const result = await execute(invocation.command, invocation.args, target.cwd, token, 120_000);
      // A newer discovery owns this root. Its model and visible items must
      // remain paired even if older compiler requests complete out of order.
      if (this.disposed || this.targets.get(key) !== target) return [];
      if (result.code !== 0) throw new Error(result.stderr || result.stdout || "Test discovery failed.");
      const cases = parseTestDiscovery(result.stdout);
      root.children.replace([]);
      const suites = new Map<string, vscode.TestItem>();
      for (const test of cases) {
        const uri = test.file ? vscode.Uri.file(test.file) : source;
        const item = this.controller.createTestItem(JSON.stringify([key, test.id]), test.name, uri);
        if (test.line > 0) item.range = new vscode.Range(test.line - 1, 0, test.line - 1, 0);
        let parent = root;
        if (test.suite) {
          let suite = suites.get(test.suite);
          if (!suite) {
            suite = this.controller.createTestItem(JSON.stringify([key, "suite", test.suite]), test.suite);
            suites.set(test.suite, suite);
            root.children.add(suite);
          }
          parent = suite;
        }
        parent.children.add(item);
        target.items.set(test.id, item);
      }
      return cases;
    } catch (error) {
      if (!this.disposed && this.targets.get(key) === target) {
        root.children.replace([]);
        root.error = String(error);
      }
      throw error;
    } finally { if (!this.disposed && this.targets.get(key) === target) root.busy = false; }
  }

  private async refresh(token: vscode.CancellationToken): Promise<void> {
    const cancellation = new vscode.CancellationTokenSource();
    const forward = token.onCancellationRequested(() => cancellation.cancel());
    if (token.isCancellationRequested) cancellation.cancel();
    this.operations.add(cancellation);
    token = cancellation.token;
    try {
      if (!this.targets.size) { await this.discover(vscode.window.activeTextEditor?.document.uri, token); return; }
      for (const target of [...this.targets.values()]) {
        if (token.isCancellationRequested) return;
        try { await this.discover(target.source, token); }
        catch (error) { if (!this.disposed) this.output.appendLine(String(error)); }
      }
    } finally { forward.dispose(); this.operations.delete(cancellation); cancellation.dispose(); }
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken, route: "vm" | "native"): Promise<void> {
    const cancellation = new vscode.CancellationTokenSource();
    const forwardCancellation = token.onCancellationRequested(() => cancellation.cancel());
    if (token.isCancellationRequested) cancellation.cancel();
    this.operations.add(cancellation);
    token = cancellation.token;
    const run = this.controller.createTestRun(request);
    const selected = (item: vscode.TestItem, roots: readonly vscode.TestItem[] | undefined): boolean => {
      if (!roots) return true;
      for (let candidate: vscode.TestItem | undefined = item; candidate; candidate = candidate.parent) {
        if (roots.some(root => root.id === candidate?.id)) return true;
      }
      return false;
    };
    try {
      this.trusted();
      if (!(await vscode.workspace.saveAll(false))) throw new Error("Save sources before running NIL tests.");
      for (const target of this.targets.values()) {
        const cases = [...target.items].filter(([, item]) => selected(item, request.include) &&
          (!request.exclude || !selected(item, request.exclude)));
        if (!cases.length) continue;
        for (const [, item] of cases) run.enqueued(item);
        if (token.isCancellationRequested) { for (const [, item] of cases) run.skipped(item); continue; }
        try {
          const invocation = this.invocation(target.source, target.entry, "run", route);
          const configured = nyConfiguration(target.source).get<number>("testing.timeoutSeconds", 30);
          const timeout = Number.isFinite(configured) ? Math.max(1, Math.min(300, Math.floor(configured))) : 30;
          invocation.args.push("--timeout", String(timeout));
          for (const [id, item] of cases) { invocation.args.push("--test-id", id); run.started(item); }
          const result = await execute(invocation.command, invocation.args, target.cwd, token,
            Math.min(3_600_000, 240_000 + cases.length * timeout * 1000));
          const report = parseTestResults(result.stdout);
          const expected = new Map(cases);
          const completed = new Set<string>();
          for (const test of report.tests) {
            const item = expected.get(test.id);
            if (!item || test.route !== route) throw new Error("niltest returned an unexpected test result.");
            completed.add(test.id);
            if (test.stdout || test.stderr) run.appendOutput((test.stdout + test.stderr).replace(/\r?\n/g, "\r\n"), undefined, item);
            if (test.status === "passed") run.passed(item, test.durationMs);
            else if (test.status === "cancelled") run.skipped(item);
            else {
              const message = new vscode.TestMessage(test.stderr || test.message || `Test ${test.status}.`);
              if (item.uri && item.range) message.location = new vscode.Location(item.uri, item.range);
              if (test.status === "error") run.errored(item, message, test.durationMs);
              else run.failed(item, message, test.durationMs);
            }
          }
          for (const [id, item] of cases) if (!completed.has(id)) {
            run.errored(item, new vscode.TestMessage(report.errors.join("\n") || result.stderr || "niltest returned no result for this test; refresh discovery after source changes."));
          }
          if (report.errors.length) run.appendOutput(report.errors.join("\r\n") + "\r\n");
          if (result.code !== 0 && report.tests.every(item => item.status === "passed") && completed.size === cases.length) {
            throw new Error(report.errors.join("\n") || result.stderr || "niltest failed after reporting test results.");
          }
        } catch (error) {
          for (const [, item] of cases) {
            if (token.isCancellationRequested) run.skipped(item);
            else run.errored(item, new vscode.TestMessage(String(error)));
          }
        }
      }
    } catch (error) { run.appendOutput(String(error) + "\r\n"); }
    finally { run.end(); forwardCancellation.dispose(); this.operations.delete(cancellation); cancellation.dispose(); }
  }
}
