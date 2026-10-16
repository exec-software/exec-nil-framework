const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function eventually(operation, description, timeout = 15_000) {
  const end = Date.now() + timeout;
  do { const result = await operation(); if (result) return result; await pause(100); } while (Date.now() < end);
  throw new Error(`Timed out: ${description}`);
}
const findings = document => vscode.languages.getDiagnostics(document.uri).filter(item => item.source === "nil-analysis");
async function replace(document, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
  assert.equal(await vscode.workspace.applyEdit(edit), true);
}
async function fixes(document, diagnostic) {
  const result = await vscode.commands.executeCommand("vscode.executeCodeActionProvider", document.uri, diagnostic.range,
    vscode.CodeActionKind.QuickFix.value);
  return result.filter(action => action.command?.command === "ny.applyAnalysisFix");
}
async function apply(action) {
  return vscode.commands.executeCommand(action.command.command, ...(action.command.arguments || []));
}

exports.run = async ({ check: runCheck, workspace }) => {
  const check = (name, operation) => process.env.NIL_VSCODE_ANALYSIS_PROBE_ONLY && !name.startsWith("cancelled compiler")
    ? Promise.resolve() : runCheck(name, operation);
  for (const [language, suffix, text] of [
    ["ny", "ny", 'fn main() -> i32\r\n    println("é😀")  \r\n    return 0\r\nend fn'],
    ["matrix", "mat", 'book editor.analysis\nmain returns number begin paragraph\n    say "é😀"  \n    give 0\nend paragraph']
  ]) {
    await check(`${language}: live buffer diagnostics and guarded quick fixes`, async () => {
      const filename = path.join(workspace, `analysis.${suffix}`);
      const onDisk = text.replace(/  (\r?\n)/g, "$1") + (text.includes("\r\n") ? "\r\n" : "\n");
      await fs.writeFile(filename, onDisk);
      const document = await vscode.workspace.openTextDocument(filename);
      await vscode.window.showTextDocument(document);
      assert.equal(document.languageId, language);
      await replace(document, text);
      const diagnostics = await eventually(() => {
        const result = findings(document);
        return result.length >= 2 && result;
      }, `${language} debounced compiler analysis`);
      assert.equal(diagnostics.filter(item => item.severity === vscode.DiagnosticSeverity.Error).length, 0,
        diagnostics.map(item => item.message).join("\n"));
      const trailing = diagnostics.find(item => /trailing whitespace/i.test(item.message));
      const newline = diagnostics.find(item => /final newline/i.test(item.message));
      assert.ok(trailing && newline, diagnostics.map(item => item.message).join("\n"));
      assert.equal(document.getText(trailing.range), "  ", "Unicode byte spans must select only the trailing spaces.");
      assert.equal(document.offsetAt(newline.range.start), document.getText().length);
      const staleAction = (await fixes(document, trailing))[0];
      assert.ok(staleAction, "The compiler fix must be available as a quick fix.");
      await replace(document, text.replace(language === "ny" ? "return 0" : "give 0", language === "ny" ? "return 1" : "give 1"));
      const changedText = document.getText();
      assert.equal(await apply(staleAction), false, "A previously returned action must reject a newer buffer version.");
      assert.equal(document.getText(), changedText);
      assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
      const freshTrailing = findings(document).find(item => /trailing whitespace/i.test(item.message));
      assert.ok(freshTrailing);
      assert.equal(await apply((await fixes(document, freshTrailing))[0]), true);
      assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
      const freshNewline = findings(document).find(item => /final newline/i.test(item.message));
      assert.ok(freshNewline);
      assert.equal(await apply((await fixes(document, freshNewline))[0]), true);
      assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
      assert.deepEqual(findings(document), []);
      assert.equal(await fs.readFile(filename, "utf8"), onDisk, "Checking/fixing an unsaved buffer must not save it.");
      const basename = filename.replace(/\.[^.]+$/, "");
      for (const artifact of [`${basename}.nil`, `${basename}.exe`, basename]) await assert.rejects(fs.stat(artifact), { code: "ENOENT" });
      return { codes: diagnostics.map(item => item.code), version: document.version };
    });
  }

  await check("untitled Matrix compiler diagnostics and fix", async () => {
    const text = "book editor.scratch\nmain returns number begin paragraph\n    give 0\nend paragraph";
    const document = await vscode.workspace.openTextDocument({ language: "matrix", content: text });
    await vscode.window.showTextDocument(document);
    assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
    const newline = findings(document).find(item => /final newline/i.test(item.message));
    assert.ok(newline);
    assert.equal(await apply((await fixes(document, newline))[0]), true);
    assert.equal(document.getText(), `${text}\n`);
    assert.equal((await fs.readdir(workspace)).some(name => name.startsWith(".nil-editor-")), false);
  });

  await check("unsaved Matrix compiler semantic error", async () => {
    const document = await vscode.workspace.openTextDocument({ language: "matrix", content:
      'book editor.semantic\nmain returns number begin paragraph\n    value count as number is "wrong"\n    give 0\nend paragraph\n' });
    await vscode.window.showTextDocument(document);
    assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
    const errors = findings(document).filter(item => item.severity === vscode.DiagnosticSeverity.Error);
    assert.ok(errors.some(item => /type mismatch/i.test(item.message)), JSON.stringify(errors));
    assert.ok(errors.some(item => item.range.start.line === 2));
    await replace(document, document.getText().replace('"wrong"', "1"));
    assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
    assert.deepEqual(findings(document), []);
    return { codes: errors.map(item => item.code) };
  });

  // A controlled executable makes cancellation and malformed-location checks
  // deterministic without timing assumptions about the real compiler.
  if (process.platform !== "win32") await check("cancelled compiler cannot publish stale diagnostics or unsafe locations", async () => {
    const configuration = vscode.workspace.getConfiguration("ny");
    const original = configuration.get("compiler.path");
    const fake = path.join(workspace, "analysis-probe.cjs");
    const log = path.join(workspace, "analysis-probe.jsonl");
    await fs.writeFile(fake, `#!/usr/bin/env node
const fs = require('node:fs');
const source = process.argv[process.argv.indexOf('--source-stdin') + 1];
let text = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => text += chunk);
process.stdin.on('end', () => {
  fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({pid: process.pid, source, text}) + '\\n');
  const finding = (code, extra = {}) => ({code, severity:'warning', message:code, path:source, start:0, length:1, hint:'', fix:null, ...extra});
  setTimeout(() => process.stdout.write(JSON.stringify({schema:'nil.analysis.v1',source,language:'matrix',success:true,reflection:{},diagnostics:[
    finding(text.includes('WAIT_OLD') ? 'STALE' : 'CURRENT'),
    finding('UNLOCATED', {path:null,start:null,length:null}),
    finding('IMPORTED', {path:source+'.imported.matrix'}),
    finding('CRLF_INTERIOR', {start:2,length:1}),
    finding('UNREPRESENTABLE_FIX', {fix:{start:2,length:1,replacement:'',description:'Unsafe line-ending edit'}})
  ]})), text.includes('WAIT_OLD') ? 5000 : 0);
});
`, { mode: 0o755 });
    let document;
    try {
      await configuration.update("compiler.path", fake, vscode.ConfigurationTarget.Workspace);
      document = await vscode.workspace.openTextDocument({ language: "matrix", content: "A\r\nWAIT_OLD\r\n" });
      await vscode.window.showTextDocument(document);
      const oldResult = vscode.commands.executeCommand("ny.analyzeCurrentFile");
      const started = await eventually(async () => {
        let data; try { data = await fs.readFile(log, "utf8"); } catch { return false; }
        return data.trim().split("\n").map(line => JSON.parse(line)).find(item => item.text.includes("WAIT_OLD"));
      }, "controlled compiler started");
      await replace(document, "A\r\nWAIT_NEW\r\n");
      assert.equal(await oldResult, false);
      await eventually(() => {
        try { process.kill(started.pid, 0); return false; } catch (error) { return error.code === "ESRCH"; }
      }, "cancelled compiler process exited");
      assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), true);
      assert.deepEqual(findings(document).map(item => item.code), ["CURRENT", "UNREPRESENTABLE_FIX"]);
      assert.deepEqual(await fixes(document, findings(document)[1]), [], "An edit inside CRLF cannot be represented safely.");
      await configuration.update("analysis.enabled", false, vscode.ConfigurationTarget.Workspace);
      await eventually(() => findings(document).length === 0, "disabled analyzer cleared diagnostics");
      const before = await fs.readFile(log, "utf8");
      await replace(document, "A\r\nDISABLED\r\n");
      await pause(600);
      assert.equal(await fs.readFile(log, "utf8"), before, "Disabled analysis must not launch a compiler.");
    } finally {
      await configuration.update("compiler.path", original, vscode.ConfigurationTarget.Workspace);
      await configuration.update("analysis.enabled", undefined, vscode.ConfigurationTarget.Workspace);
    }
  });
};
