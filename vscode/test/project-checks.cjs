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
const findings = uri => vscode.languages.getDiagnostics(uri).filter(item => item.source === "nil-analysis");
async function replace(document, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
  assert.equal(await vscode.workspace.applyEdit(edit), true);
}
exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) {
    const directory = path.join(workspace, `project ${language}`);
    await fs.mkdir(path.join(directory, "src"), { recursive: true });
    await fs.mkdir(path.join(directory, "lib"), { recursive: true });
    const manifestFile = path.join(directory, language === "matrix" ? "nil.project.nildata" : "nil.project.json");
    const manifest = { schema: "nil.project.v1", name: "hello", entry: `src/main.${language}`, includePaths: ["lib"] };
    const saveManifest = () => fs.writeFile(manifestFile, language === "ny" ? JSON.stringify(manifest) :
      `nildata 1\nobject\n  schema is "nil.project.v1"\n  name is "hello"\n  entry is "${manifest.entry}"\n` +
      `  includePaths is an array\n    item is "lib"\n  end array\n` +
      (manifest.analysis ? `  analysis is an object\n    warningsAsErrors is ${manifest.analysis.warningsAsErrors}\n` +
        `    disabledRules is an array\n${manifest.analysis.disabledRules.map(rule => `      item is "${rule}"\n`).join("")}` +
        `    end array\n  end object\n` : "") + `end object\n`);
    await saveManifest();
    await fs.writeFile(path.join(directory, "lib", "answer.ny"), 'module answer\npub fn answer() -> i32\n return 42\nend fn\n');
    // Distinguish nilbuild's project cwd and explicit import-root ordering from
    // the entry directory. Choosing either shadow makes compiler checks fail.
    await fs.writeFile(path.join(directory, "rooted.ny"), 'module rooted\npub fn rooted() -> i32\n return 0\nend fn\n');
    await fs.writeFile(path.join(directory, "src", "rooted.ny"), 'module rooted\npub fn rooted() -> i32\n return "wrong root"\nend fn\n');
    await fs.writeFile(path.join(directory, "src", "answer.ny"), 'module answer\npub fn answer() -> i32\n return "wrong include precedence"\nend fn\n');
    const text = language === "ny" ? 'import answer.*\nimport rooted.*\nfn main() -> i32\n println(answer())\n return rooted()\nend fn\n' :
      'book project.example\nread everything from answer\nread everything from rooted\nmain returns override integer begin paragraph\n say (answer with nothing)\n give (rooted with nothing)\nend paragraph\n';
    await fs.writeFile(path.join(directory, manifest.entry), text);
    const document = await vscode.workspace.openTextDocument(path.join(directory, manifest.entry));
    await vscode.window.showTextDocument(document);
    await check(`${language}: project cwd and explicit import roots outrank source-directory shadows`, async () => {
      await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
      const projectFindings = [document.uri, ...["rooted.ny", "src/rooted.ny", "src/answer.ny", "lib/answer.ny"]
        .map(relative => vscode.Uri.file(path.join(directory, relative)))].flatMap(findings);
      assert.deepEqual(projectFindings, [], "Editor checks must select the same root/include modules as nilbuild, not invalid source-directory shadows.");
      const tasks = await vscode.tasks.fetchTasks({ type: "ny" });
      const projectTasks = tasks.filter(task => task.definition.project === manifestFile);
      assert.equal(projectTasks.length, 6);
      assert.ok(projectTasks.every(task => task.execution instanceof vscode.ProcessExecution));
    });
    for (const [target, checkCommand, buildCommand, runCommand] of [
      ["vm", "ny.checkProject", "ny.buildProject", "ny.runProject"],
      ["native", "ny.checkNativeProject", "ny.buildNativeProject", "ny.runNativeProject"]
    ]) {
      await check(`${language}: ${target} project check, build and run tasks`, async () => {
        assert.equal(await vscode.commands.executeCommand(checkCommand), 0);
        assert.equal(await vscode.commands.executeCommand(buildCommand), 0);
        assert.equal(await vscode.commands.executeCommand(runCommand), 0);
        const output = path.join(directory, "build", "hello" + (target === "vm" ? ".nil" : process.platform === "win32" ? ".exe" : ""));
        assert.ok((await fs.stat(output)).size > 0);
        return { target, output };
      });
    }
    await check(`${language}: semantic rules and project warning policy agree with project checks`, async () => {
      const warning = language === "ny" ? 'import nil.std.text\nfn main() -> i32\n try_char_at("abc", 0)\n return 0\n println("unreachable")\nend fn\n' :
        'book project.warnings\nread everything from nil.std.text\nmain returns override integer begin paragraph\n evaluate try-char-at with s being "abc", index being 0\n give 0\n say "unreachable"\nend paragraph\n';
      await replace(document, warning);
      await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
      assert.deepEqual(findings(document.uri).map(item => item.code).sort(), ["NIL2001", "NIL2002"]);
      manifest.analysis = { warningsAsErrors: true, disabledRules: [] };
      await saveManifest();
      await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
      assert.ok(findings(document.uri).every(item => item.severity === vscode.DiagnosticSeverity.Error));
      assert.equal(await vscode.commands.executeCommand("ny.checkProject"), 1);
      assert.equal(await vscode.commands.executeCommand("ny.checkNativeProject"), 1);
      manifest.analysis.disabledRules = ["NIL2001", "NIL2002"];
      await saveManifest();
      await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
      assert.deepEqual(findings(document.uri), []);
      assert.equal(await vscode.commands.executeCommand("ny.checkProject"), 0);
      assert.equal(await vscode.commands.executeCommand("ny.checkNativeProject"), 0);
      return { codes: ["NIL2001", "NIL2002"], promoted: true, suppressed: true };
    });
  }

  await check("imported Problems use exact snapshots, aggregate entry reports, and refresh on edits", async () => {
    const directory = path.join(workspace, "imports");
    await fs.mkdir(directory, { recursive: true });
    const dependency = path.join(directory, "dep.ny");
    const broken = 'module dep\r\n# é😀\r\npub fn broken() -> i32\r\n return "bad"\r\nend fn\r\n';
    await fs.writeFile(dependency, broken);
    const depUri = vscode.Uri.file(dependency);
    const entries = [];
    for (const name of ["first", "second"]) {
      const file = path.join(directory, `${name}.ny`);
      await fs.writeFile(file, 'import dep.*\nfn main() -> i32\n return broken()\nend fn\n');
      const document = await vscode.workspace.openTextDocument(file);
      entries.push(document);
      await vscode.window.showTextDocument(document);
      await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
    }
    const errors = findings(depUri).filter(item => item.severity === vscode.DiagnosticSeverity.Error);
    assert.equal(errors.length, 1, "Two entry reports should not duplicate a dependency finding.");
    assert.equal(errors[0].range.start.line, 3, "UTF-8 and CRLF offsets must map the imported source exactly.");
    const depDocument = await vscode.workspace.openTextDocument(depUri);
    await vscode.window.showTextDocument(depDocument);
    await replace(depDocument, broken.replace('"bad"', "1"));
    await vscode.window.showTextDocument(entries[0]);
    await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "current compiler check");
    await eventually(() => findings(depUri).length === 0, "dirty import diagnostic removal");
    assert.equal(await fs.readFile(dependency, "utf8"), broken, "Analysis must not save imported buffers.");
    assert.equal(await depDocument.save(), true);
    await eventually(() => entries.every(document => findings(document.uri).length === 0) && findings(depUri).length === 0, "saved import diagnostics refreshed");
    // A disk-only edit must invalidate entries even when the import is closed.
    await vscode.window.showTextDocument(depDocument);
    await vscode.commands.executeCommand("workbench.action.closeActiveEditor");
    await fs.writeFile(dependency, broken);
    await eventually(() => findings(depUri).some(item => item.range.start.line === 3), "external import change refreshed Problems");
    await fs.writeFile(dependency, broken.replace('"bad"', "1"));
    await eventually(() => findings(depUri).length === 0, "external import repair cleared Problems");
    return { importedLine: 4, duplicateCount: errors.length };
  });
};
