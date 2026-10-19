const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function eventually(operation, description) {
  const until = Date.now() + 15_000;
  do { const result = await operation(); if (result) return result; await pause(100); } while (Date.now() < until);
  throw new Error(`Timed out: ${description}`);
}
const findings = document => vscode.languages.getDiagnostics(document.uri).filter(item => item.source === "nil-analysis");
const errors = document => findings(document).filter(item => item.severity === vscode.DiagnosticSeverity.Error);
const hasTab = document => vscode.window.tabGroups.all.some(group => group.tabs.some(tab => tab.input?.uri?.toString() === document.uri.toString()));
async function replace(document, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
  assert.equal(await vscode.workspace.applyEdit(edit), true);
}
async function analyze(document) {
  await vscode.window.showTextDocument(document);
  await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current workspace compiler check");
}
exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) await check(`${language}: unsaved cross-language imports change caller types and preserve disk`, async () => {
    const directory = path.join(workspace, `overlay ${language}`);
    await fs.mkdir(directory, { recursive: true });
    const dependencyLanguage = language === "ny" ? "matrix" : "ny";
    const dependency = path.join(directory, `dep.${dependencyLanguage}`);
    const source = path.join(directory, `main.${language}`);
    const imported = (type, value) => dependencyLanguage === "ny" ?
      `module dep\r\n# é😀\r\npub fn answer() -> ${type === "integer" ? "i32" : "text"}\r\n return ${value}\r\nend fn\r\n` :
      `book dep\r\n!!! é😀\r\npublic answer returns ${type === "integer" ? "override integer" : "text"} begin paragraph\r\n give ${value}\r\nend paragraph\r\n`;
    const original = imported("integer", "42");
    await fs.writeFile(dependency, original);
    await fs.writeFile(source, language === "ny" ? 'import dep.*\nfn main() -> i32\n return answer()\nend fn\n' :
      'book overlay.entry\nread everything from dep\nmain returns override integer begin paragraph\n give (answer with nothing)\nend paragraph\n');
    const entry = await vscode.workspace.openTextDocument(source);
    const dep = await vscode.workspace.openTextDocument(dependency);
    await analyze(entry);
    assert.deepEqual(errors(entry), []);
    await replace(dep, imported("text", '"changed"'));
    await analyze(entry);
    assert.ok(errors(entry).some(item => /type mismatch/i.test(item.message)), "The caller must use the dirty imported function's return type.");
    await replace(dep, imported("integer", '"wrong"'));
    await analyze(entry);
    assert.ok(errors(dep).some(item => item.range.start.line === 3), "A dirty imported error must retain its exact Unicode/CRLF location.");
    await replace(dep, original);
    await analyze(entry);
    await eventually(() => errors(entry).length === 0 && errors(dep).length === 0, "unsaved repair clears dependent errors");
    const leafPath = path.join(directory, "leaf.ny");
    const leafText = 'module leaf\npub fn leaf_answer() -> i32\n return 42\nend fn\n';
    await fs.writeFile(leafPath, leafText);
    const leaf = await vscode.workspace.openTextDocument(leafPath);
    await replace(dep, dependencyLanguage === "ny" ?
      'module dep\nimport leaf.*\npub fn answer() -> i32\n return leaf_answer()\nend fn\n' :
      'book dep\nread everything from leaf\npublic answer returns override integer begin paragraph\n give (leaf-answer with nothing)\nend paragraph\n');
    await analyze(entry);
    assert.deepEqual(errors(entry), []);
    await replace(leaf, leafText.replace("-> i32", "-> text").replace("return 42", 'return "changed"'));
    await analyze(entry);
    assert.ok(errors(dep).some(item => /type mismatch/i.test(item.message)), "An unsaved transitive import must change its direct caller's types.");
    await replace(leaf, leafText);
    await analyze(entry);
    await eventually(() => errors(entry).length === 0 && errors(dep).length === 0, "transitive overlay repair clears errors");
    assert.equal(await fs.readFile(dependency, "utf8"), original);
    assert.equal(await fs.readFile(leafPath, "utf8"), leafText);
    assert.deepEqual((await fs.readdir(directory)).sort(), [`dep.${dependencyLanguage}`, `main.${language}`, "leaf.ny"].sort());
    return { dependencyLanguage, saved: false };
  });

  await check("named untitled imports resolve before saving and disappear when closed", async () => {
    const directory = path.join(workspace, "new overlay");
    await fs.mkdir(directory, { recursive: true });
    const freshPath = path.join(directory, "fresh.matrix");
    const entryPath = path.join(directory, "main.ny");
    await fs.writeFile(entryPath, 'import fresh.*\nfn main() -> i32\n return answer()\nend fn\n');
    let fresh = await vscode.workspace.openTextDocument(vscode.Uri.file(freshPath).with({ scheme: "untitled" }));
    fresh = await vscode.languages.setTextDocumentLanguage(fresh, "matrix");
    await replace(fresh, 'book fresh\npublic answer returns override integer begin paragraph\n give 42\nend paragraph\n');
    const entry = await vscode.workspace.openTextDocument(entryPath);
    await analyze(entry);
    assert.deepEqual(errors(entry), []);
    await analyze(fresh);
    assert.deepEqual(errors(fresh), [], "Named untitled primary checks must retain their filename.");
    await assert.rejects(fs.stat(freshPath), { code: "ENOENT" });
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
    await eventually(() => fresh.isClosed, "named untitled document closes");
    await analyze(entry);
    assert.ok(errors(entry).length, "Closing the unsaved import must invalidate dependent analysis.");
    await assert.rejects(fs.stat(freshPath), { code: "ENOENT" });
  });

  await check("named untitled primary uses its nested project settings and supports missing parent directories", async () => {
    const directory = path.join(workspace, "named project");
    await fs.mkdir(path.join(directory, "lib"), { recursive: true });
    await fs.writeFile(path.join(directory, "nil.project.json"), JSON.stringify({ schema: "nil.project.v1", name: "new-project",
      entry: "src/new.matrix", includePaths: ["lib"], analysis: { disabledRules: ["NIL1002"], warningsAsErrors: true } }));
    await fs.writeFile(path.join(directory, "lib", "projecthelper.ny"), 'module projecthelper\npub fn answer() -> i32\n return 0\nend fn\n');
    const filename = path.join(directory, "src", "new.matrix");
    let primary = await vscode.workspace.openTextDocument(vscode.Uri.file(filename).with({ scheme: "untitled" }));
    primary = await vscode.languages.setTextDocumentLanguage(primary, "matrix");
    const text = 'book fresh.project\nread everything from projecthelper\nmain returns override integer begin paragraph\n give (answer with nothing)\nend paragraph';
    await replace(primary, text);
    await analyze(primary);
    assert.deepEqual(findings(primary), [], "Named untitled checks must find project include paths and suppress the configured missing-newline rule.");
    await replace(primary, text.replace("with nothing)", "with nothing)  "));
    await analyze(primary);
    assert.deepEqual(findings(primary).map(item => [item.code, item.severity]), [["NIL1001", vscode.DiagnosticSeverity.Error]]);
    await assert.rejects(fs.stat(path.dirname(filename)), { code: "ENOENT" });
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
    const standalonePath = path.join(workspace, "not-created", "nested", "new.ny");
    let standalone = await vscode.workspace.openTextDocument(vscode.Uri.file(standalonePath).with({ scheme: "untitled" }));
    standalone = await vscode.languages.setTextDocumentLanguage(standalone, "ny");
    await replace(standalone, 'fn main() -> i32\n return 0\nend fn\n');
    await analyze(standalone);
    assert.deepEqual(findings(standalone), []);
    await assert.rejects(fs.stat(path.dirname(standalonePath)), { code: "ENOENT" });
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
  });

  if (process.platform !== "win32") await check("dependency edits and closes cancel compiler snapshots and imported fixes remain unavailable", async () => {
    const configuration = vscode.workspace.getConfiguration("ny");
    const originalCompiler = configuration.get("compiler.path");
    const fake = path.join(workspace, "workspace-probe.cjs");
    const log = path.join(workspace, "workspace-probe.jsonl");
    const entryPath = path.join(workspace, "workspace-probe.matrix");
    const dependencyPath = path.join(workspace, "workspace-probe-dep.ny");
    await fs.writeFile(entryPath, "book probe\n");
    await fs.writeFile(dependencyPath, "DEP_DISK\r\n");
    await fs.writeFile(fake, `#!/usr/bin/env node
const fs = require('node:fs');
const primary = process.argv[process.argv.indexOf('--workspace-stdin') + 1];
let input = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => input += chunk);
process.stdin.on('end', () => {
  const request = JSON.parse(input);
  const dependencies = request.sources.filter(item => { try { return fs.realpathSync(item.path) === ${JSON.stringify(dependencyPath)}; } catch { return false; } });
  const dependency = dependencies[0];
  const ours = primary === ${JSON.stringify(entryPath)};
  fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({pid:process.pid,primary,text:dependency?.text,count:dependencies.length}) + '\\n');
  const stale = dependency?.text.includes('WAIT');
  const diagnostics = ours && dependency ? [{code:stale?'STALE':'IMPORT_CURRENT',severity:'warning',message:'Imported finding',
    path:${JSON.stringify(dependencyPath)},start:6,length:2,hint:'',fix:{start:6,length:2,replacement:'',description:'Do not offer imported edits'}}] : [];
  setTimeout(() => process.stdout.write(JSON.stringify({schema:'nil.analysis.v1',source:primary,
    language:primary.endsWith('.ny')?'ny':'matrix',success:true,reflection:{},diagnostics,
    sources:ours && dependency ? [{path:${JSON.stringify(dependencyPath)},text:dependency.text}] : []})), ours && stale ? 5000 : 0);
});
`, { mode: 0o755 });
    try {
      await configuration.update("compiler.path", fake, vscode.ConfigurationTarget.Workspace);
      const dependency = await vscode.workspace.openTextDocument(dependencyPath);
      const entry = await vscode.workspace.openTextDocument(entryPath);
      const waitForRequest = marker => eventually(async () => {
        let lines; try { lines = await fs.readFile(log, "utf8"); } catch { return false; }
        return lines.trim().split("\n").map(line => JSON.parse(line)).find(item => item.primary === entryPath && item.text?.includes(marker));
      }, "delayed dependency snapshot begins");
      const exited = pid => eventually(() => {
        try { process.kill(pid, 0); return false; } catch (error) { return error.code === "ESRCH"; }
      }, "cancelled workspace compiler exits");
      await replace(dependency, "é😀  \r\nWAIT_EDIT\r\n");
      await vscode.window.showTextDocument(entry);
      const stale = vscode.commands.executeCommand("ny.analyzeCurrentFile");
      const started = await waitForRequest("WAIT_EDIT");
      await replace(dependency, "é😀  \r\nCURRENT\r\n");
      assert.equal(await stale, false, "An imported edit must cancel unchanged primary analysis.");
      await exited(started.pid);
      await analyze(entry);
      const imported = findings(dependency).find(item => item.code === "IMPORT_CURRENT");
      assert.ok(imported, "A response matching a dirty captured import must publish.");
      assert.equal(dependency.getText(imported.range), "  ", "Imported byte ranges must match the exact captured buffer.");
      const actions = await vscode.commands.executeCommand("vscode.executeCodeActionProvider", dependency.uri, imported.range, vscode.CodeActionKind.QuickFix.value);
      assert.deepEqual(actions.filter(action => action.command?.command === "ny.applyAnalysisFix"), []);
      assert.equal(await fs.readFile(dependencyPath, "utf8"), "DEP_DISK\r\n");
      const aliasPath = path.join(workspace, "workspace-probe-alias.ny");
      await fs.symlink(dependencyPath, aliasPath);
      const alias = await vscode.workspace.openTextDocument(aliasPath);
      await replace(alias, dependency.getText());
      await analyze(entry);
      assert.ok(findings(alias).some(item => item.code === "IMPORT_CURRENT"), "Canonical imported paths must map back to an open alias URI.");
      const logEntries = (await fs.readFile(log, "utf8")).trim().split("\n").map(line => JSON.parse(line));
      assert.equal(logEntries.filter(item => item.primary === entryPath).at(-1).count, 1, "Identical canonical aliases must be coalesced before sending.");
      await replace(alias, "different unsaved alias\n");
      assert.equal(await vscode.commands.executeCommand("ny.analyzeCurrentFile"), false, "Conflicting open aliases must be reported rather than choosing one buffer.");
      await replace(alias, dependency.getText());
      await vscode.window.showTextDocument(alias);
      await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
      await eventually(() => !hasTab(alias), "alias tab closes");
      await analyze(entry);
      assert.ok(findings(dependency).some(item => item.code === "IMPORT_CURRENT"), "A retained clean model from the closed alias tab must not block the dirty source.");
      await replace(dependency, "é😀  \r\nWAIT_CLOSE\r\n");
      await vscode.window.showTextDocument(entry);
      const closing = vscode.commands.executeCommand("ny.analyzeCurrentFile");
      const beforeClose = await waitForRequest("WAIT_CLOSE");
      await vscode.window.showTextDocument(dependency);
      await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
      assert.equal(await closing, false);
      await exited(beforeClose.pid);
      await eventually(() => !hasTab(dependency), "dependency tab closes");
      await analyze(entry);
      assert.equal(findings(entry).some(item => item.code === "STALE"), false);
      assert.equal(findings(dependency).some(item => item.code === "STALE"), false);
    } finally {
      await configuration.update("compiler.path", originalCompiler, vscode.ConfigurationTarget.Workspace);
    }
  });
};
