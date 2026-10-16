const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const { execFile } = require("node:child_process");
const { promisify } = require("node:util");
const vscode = require("vscode");
const executeFile = promisify(execFile);

const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function bounded(promise, description, timeout = 25_000) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(`Timed out: ${description}`)), timeout);
    })]);
  } finally { clearTimeout(timer); }
}
async function eventually(operation, description, timeout = 20_000) {
  const end = Date.now() + timeout;
  let latest;
  do {
    try { const value = await operation(); if (value) return value; }
    catch (error) { latest = error; }
    await pause(150);
  } while (Date.now() < end);
  throw new Error(`Timed out: ${description}${latest ? ` (${latest})` : ""}`);
}
function positionIn(document, text, offset = text.length) {
  const index = document.getText().indexOf(text);
  assert.notEqual(index, -1, `Fixture text missing: ${text}`);
  return document.positionAt(index + offset);
}
function label(item) { return typeof item.label === "string" ? item.label : item.label.label; }
function rangeFor(item) { return item.range?.replacing || item.range; }

exports.run = async function run() {
  const workspace = process.env.NIL_VSCODE_TEST_WORKSPACE || process.env.NY_VSCODE_TEST_WORKSPACE;
  const reportFile = process.env.NIL_VSCODE_TEST_REPORT || process.env.NY_VSCODE_TEST_REPORT;
  assert.ok(workspace && reportFile, "Use test/run-host.cjs to create an isolated workspace.");
  const checks = [];
  const report = { status: "RUNNING", vscode: vscode.version, workspace, checks };
  const check = async (name, operation) => {
    const start = Date.now();
    const detail = await bounded(operation(), name, 45_000);
    checks.push({ name, status: "PASS", milliseconds: Date.now() - start, ...(detail || {}) });
    console.log(`PASS ${name}`);
  };
  const events = [];
  const taskSubscription = vscode.tasks.onDidEndTaskProcess((event) => {
    events.push({ task: event.execution.task.definition.task, source: event.execution.task.definition.source, exitCode: event.exitCode });
  });
  try {
    assert.equal(vscode.workspace.isTrusted, true);
    const extension = vscode.extensions.getExtension("exec-software.ny-language-support");
    assert.ok(extension);
    await bounded(extension.activate(), "extension activation");
    if (process.env.NIL_VSCODE_ANALYSIS_ONLY || process.env.NIL_VSCODE_ANALYSIS_PROBE_ONLY) {
      await require("./analyzer-checks.cjs").run({ check, workspace });
      report.status = "PASS";
      return;
    }
    const documents = [];
    for (const filename of ["editor.matrix", "brief.mat"]) {
      await check(`${filename}: language detection`, async () => {
        const document = await vscode.workspace.openTextDocument(vscode.Uri.file(path.join(workspace, filename)));
        await vscode.window.showTextDocument(document);
        assert.equal(document.languageId, "matrix");
        documents.push(document);
      });
    }
    const document = documents[0];
    await vscode.window.showTextDocument(document);
    await check("hyphenated local completion and replacement range", async () => {
      const position = positionIn(document, "say local-count", "say local-c".length);
      const item = await eventually(async () => {
        const result = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, position);
        return result?.items.find((entry) => label(entry) === "local-count");
      }, "hyphenated local completion");
      const range = rangeFor(item);
      assert.ok(range, "Server must supply a replacement range for hyphenated identifiers.");
      assert.equal(document.getText(range), "local-count");
      return { label: label(item), range: [range.start.line, range.start.character, range.end.line, range.end.character] };
    });
    await check("typed receiver member completion", async () => {
      const position = positionIn(document, "say sample-point.x", "say sample-point.".length);
      const result = await eventually(async () => {
        const completion = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, position, ".");
        return completion?.items.some((entry) => label(entry) === "x") && completion;
      }, "Point member completion");
      const labels = result.items.map(label);
      assert.ok(labels.includes("x") && labels.includes("y"), JSON.stringify(labels));
      assert.equal(labels.includes("local-count"), false, "Member completion must not leak ordinary locals.");
      return { labels };
    });
    await check("hover, definition, and document symbols", async () => {
      const position = positionIn(document, "say local-count", "say local".length);
      const hover = await vscode.commands.executeCommand("vscode.executeHoverProvider", document.uri, position);
      assert.ok(hover?.length);
      const hoverText = hover.flatMap((item) => item.contents).map((content) => typeof content === "string" ? content : content.value).join("\n");
      assert.match(hoverText, /local-count/);
      const definitions = await vscode.commands.executeCommand("vscode.executeDefinitionProvider", document.uri, position);
      assert.ok(definitions?.length);
      const definition = definitions[0];
      assert.equal((definition.targetUri || definition.uri).fsPath, document.uri.fsPath);
      assert.equal((definition.targetSelectionRange || definition.range).start.line, 8);
      const symbols = await vscode.commands.executeCommand("vscode.executeDocumentSymbolProvider", document.uri);
      const names = [];
      const visit = (items) => { for (const item of items) { names.push(item.name); visit(item.children || []); } };
      visit(symbols);
      assert.ok(names.includes("Point") && names.includes("main"), JSON.stringify(names));
      return { symbols: names };
    });
    await check("untitled Matrix document completion", async () => {
      const untitled = await vscode.workspace.openTextDocument({ language: "matrix", content:
        "book scratch\nmain returns number begin paragraph\n    value scratch-name as number is 9\n    say scratch-name\n    give 0\nend paragraph\n" });
      await vscode.window.showTextDocument(untitled);
      const position = positionIn(untitled, "say scratch-name", "say scratch".length);
      await eventually(async () => {
        const completion = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", untitled.uri, position);
        return completion?.items.some((item) => label(item) === "scratch-name");
      }, "untitled Matrix completion");
    });
    await check("compiler diagnostics appear in Problems without a source column", async () => {
      const invalid = await vscode.workspace.openTextDocument(vscode.Uri.file(path.join(workspace, "invalid.mat")));
      await vscode.window.showTextDocument(invalid);
      const start = events.length;
      await vscode.commands.executeCommand("ny.compileCurrentFile");
      await eventually(() => events.length > start, "rejected compiler task");
      assert.equal(events[start].task, "compile");
      assert.notEqual(events[start].exitCode, 0);
      const diagnostics = await eventually(() => {
        const items = vscode.languages.getDiagnostics(invalid.uri).filter((item) => item.source === "nilcompiler");
        return items.length && items;
      }, "Matrix compiler problem matcher");
      assert.ok(diagnostics.some((item) => item.range.start.line === 2 && /Type mismatch/.test(item.message)));
      return { messages: diagnostics.map((item) => item.message) };
    });
    for (const source of documents) {
      await vscode.window.showTextDocument(source);
      const basename = source.uri.fsPath.replace(/\.[^.]+$/, "");
      await check(`${path.basename(source.fileName)}: compile and interpreter tasks`, async () => {
        await fs.rm(`${basename}.nil`, { force: true });
        const start = events.length;
        await vscode.commands.executeCommand("ny.runCurrentFile");
        await eventually(() => events.slice(start).some((event) => event.task === "run"), "interpreter task completion");
        const completed = events.slice(start);
        assert.deepEqual(completed.map(({ task, exitCode }) => ({ task, exitCode })), [
          { task: "compile", exitCode: 0 }, { task: "run", exitCode: 0 }
        ]);
        assert.ok((await fs.stat(`${basename}.nil`)).size > 0);
        const interpreter = vscode.workspace.getConfiguration("ny").get("interpreter.path");
        const result = await executeFile(interpreter, [`${basename}.nil`], { timeout: 10_000 });
        assert.equal(result.stdout.replace(/\r/g, ""), "7\n3\n");
        return { tasks: completed, stdout: result.stdout };
      });
      await check(`${path.basename(source.fileName)}: native compiler task`, async () => {
        const executable = basename + (process.platform === "win32" ? ".exe" : "");
        await fs.rm(executable, { force: true });
        const start = events.length;
        await vscode.commands.executeCommand("ny.compileNativeCurrentFile");
        await eventually(() => events.length > start, "native task completion");
        assert.equal(events[start].task, "native");
        assert.equal(events[start].exitCode, 0);
        assert.ok((await fs.stat(executable)).size > 0);
        const result = await executeFile(executable, [], { timeout: 10_000 });
        assert.equal(result.stdout.replace(/\r/g, ""), "7\n3\n");
        return { stdout: result.stdout };
      });
      await check(`${path.basename(source.fileName)}: debugger entry and cleanup`, async () => {
        let activeSession;
        let stopped;
        let terminated = false;
        const messages = [];
        const tracker = vscode.debug.registerDebugAdapterTrackerFactory("ny", {
          createDebugAdapterTracker(session) {
            activeSession = session;
            return { onDidSendMessage(message) {
              if (message.type === "event") messages.push(message.event);
              if (message.type === "event" && message.event === "stopped") stopped = message.body;
            } };
          }
        });
        const endSubscription = vscode.debug.onDidTerminateDebugSession((session) => {
          if (session.id === activeSession?.id) terminated = true;
        });
        try {
          const started = await vscode.debug.startDebugging(vscode.workspace.workspaceFolders[0], {
            type: "ny", request: "launch", name: "Matrix host smoke", program: source.fileName, stopOnEntry: true
          });
          assert.equal(started, true);
          await eventually(() => stopped, "Matrix debugger entry stop");
          assert.equal(stopped.reason, "entry");
          const stack = await activeSession.customRequest("stackTrace", { threadId: stopped.threadId, startFrame: 0, levels: 10 });
          assert.ok(stack.stackFrames.length);
          assert.equal(stack.stackFrames[0].source.path, source.fileName);
          assert.ok(stack.stackFrames[0].line > 0);
          const directory = activeSession.configuration.__nyDebugDirectory;
          assert.ok(directory, "Source debug launch must own a temporary NIL directory.");
          assert.ok((await fs.stat(path.join(directory, "program.nil"))).size > 0);
          await vscode.debug.stopDebugging(activeSession);
          await eventually(() => terminated, "debug session termination");
          await eventually(async () => {
            try { await fs.stat(directory); return false; }
            catch (error) { if (error.code === "ENOENT") return true; throw error; }
          }, "debug artifact cleanup");
          return { events: messages, sourceLine: stack.stackFrames[0].line };
        } finally {
          if (activeSession && !terminated) await vscode.debug.stopDebugging(activeSession);
          tracker.dispose(); endSubscription.dispose();
        }
      });
    }
    await require("./analyzer-checks.cjs").run({ check, workspace });
    report.status = "PASS";
  } catch (error) {
    report.status = "FAIL";
    report.error = error.stack || String(error);
    throw error;
  } finally {
    taskSubscription.dispose();
    await fs.writeFile(reportFile, JSON.stringify(report, null, 2) + "\n");
  }
};
