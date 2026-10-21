const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const label = item => typeof item.label === "string" ? item.label : item.label.label;
async function eventually(operation, description) {
  const until = Date.now() + 15_000;
  let latest;
  do {
    try { const value = await operation(); if (value) return value; }
    catch (error) { latest = error; }
    await pause(100);
  } while (Date.now() < until);
  throw new Error(`Timed out: ${description}${latest ? ` (${latest})` : ""}`);
}
async function create(workspace, name, text) {
  const filename = path.join(workspace, "language values", name);
  await fs.mkdir(path.dirname(filename), { recursive: true });
  await fs.writeFile(filename, text);
  const document = await vscode.workspace.openTextDocument(filename);
  await vscode.window.showTextDocument(document, { preview: false });
  return document;
}
function range(document, context, token) {
  const contextStart = document.getText().indexOf(context);
  const start = contextStart + context.indexOf(token);
  assert.ok(contextStart >= 0 && context.includes(token), `Missing ${context}/${token}`);
  return new vscode.Range(document.positionAt(start), document.positionAt(start + token.length));
}
async function analyze(document) {
  await vscode.window.showTextDocument(document, { preview: false });
  await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"),
    "analysis publishes after the editor generation settles");
  return vscode.languages.getDiagnostics(document.uri).filter(item =>
    item.source === "nil-analysis" && item.severity === vscode.DiagnosticSeverity.Error);
}
async function complete(document, context, token, prefix, expectedType) {
  const selected = range(document, context, token);
  const position = document.positionAt(document.offsetAt(selected.start) + prefix.length);
  const candidates = await eventually(async () => {
    const result = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, position);
    const items = result?.items.filter(item => /^variable:/.test(item.detail || "")) || [];
    return items.length && items;
  }, `compiler completion within ${context}`);
  const item = candidates.find(candidate => label(candidate) === token);
  assert.ok(item, `Expected ${token}, got ${candidates.map(label)}`);
  assert.equal(item.detail, `variable: ${expectedType}`);
  assert.ok((item.range?.replacing || item.range).isEqual(selected));
  return selected;
}
function fixture(language) {
  return (language === "ny" ?
    '# é😀\nfn scale(input: f64, factor: f64) -> f64\n return input * factor\nend fn\nfn main() -> i32\n let height: f64 = -1.6\n let label = $"é\\n😀 {{height}} {height} {scale(height, 2.0)}"\n println(label)\n return 0\nend fn\n' :
    'book editor.values\n!!! é😀\nscale receives input as floating, factor as floating returns floating begin paragraph\n give input times factor\nend paragraph\nmain returns override integer begin paragraph\n value height as floating is negative 1.6\n value label is formatted "é😀 {{height}} {height} {scale with factor being 2.0, input being height}"\n say label\n give 0\nend paragraph\n').replaceAll('\n', '\r\n');
}

exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) {
    await check(`${language}: interpolation exposes compiler completion and call signatures`, async () => {
      const document = await create(workspace, `completion.${language}`, fixture(language));
      assert.deepEqual(await analyze(document), []);
      await complete(document, " {height} ", "height", "hei", language === "ny" ? "f64" : "floating");
      const context = language === "ny" ? "scale(height, 2.0)" : "scale with factor being 2.0, input being height";
      const beforeCursor = language === "ny" ? "scale(height, " : "scale with factor being ";
      const cursor = document.positionAt(document.getText().indexOf(context) + beforeCursor.length);
      const help = await eventually(async () => {
        const value = await vscode.commands.executeCommand("vscode.executeSignatureHelpProvider", document.uri, cursor);
        return value?.signatures?.length && value;
      }, "signature inside interpolation");
      assert.equal(help.signatures.length, 1);
      assert.equal(help.activeParameter, 1);
      assert.equal(help.signatures[0].parameters.length, 2);
      assert.ok(help.signatures[0].label.includes("input") && help.signatures[0].label.includes("factor"));
      return { language, utf8: true, lineEndings: "CRLF", activeParameter: 1 };
    });

    await check(`${language}: interpolation rename edits expression identifiers and preserves literal braces`, async () => {
      const text = fixture(language);
      const document = await create(workspace, `rename.${language}`, text);
      assert.deepEqual(await analyze(document), []);
      const target = range(document, " {height} ", "height");
      const replacement = language === "ny" ? "updated_height" : "updated-height";
      const edit = await eventually(async () => {
        const value = await vscode.commands.executeCommand("vscode.executeDocumentRenameProvider", document.uri,
          document.positionAt(document.offsetAt(target.start) + 2), replacement);
        return value?.size && value;
      }, "rename inside interpolation");
      assert.equal(edit.size, 1);
      const changes = edit.entries()[0][1];
      assert.equal(changes.length, 3);
      assert.ok(changes.every(item => document.getText(item.range) === "height" && item.newText === replacement));
      const expected = language === "ny" ? text.replace("let height:", `let ${replacement}:`)
        .replace(" {height} ", ` {${replacement}} `).replace("scale(height,", `scale(${replacement},`) :
        text.replace("value height as", `value ${replacement} as`)
          .replace(" {height} ", ` {${replacement}} `).replace("input being height", `input being ${replacement}`);
      assert.equal(await vscode.workspace.applyEdit(edit), true);
      // The doubled-brace literal contains the same spelling and must survive.
      assert.ok(document.getText().includes("{{height}}"));
      assert.equal(document.getText(), expected);
      assert.deepEqual(await analyze(document), []);
      await vscode.commands.executeCommand("undo");
      await eventually(() => document.getText() === text, "single undo restores interpolation rename");
      return { language, occurrences: 3, literalPreserved: true };
    });

    await check(`${language}: destructured tuple locals retain types and source definitions`, async () => {
      const text = language === "ny" ?
        'fn main() -> i32\n let coordinates: (f64, f64) = (1.0, -1.6)\n let (width, height) = coordinates\n println(height)\n return 0\nend fn\n' :
        'book editor.coordinates\nmain returns override integer begin paragraph\n value coordinates as tuple of (floating, floating) is (1.0, -1.6)\n value (width, height) is coordinates\n say height\n give 0\nend paragraph\n';
      const document = await create(workspace, `tuple.${language}`, text);
      assert.deepEqual(await analyze(document), []);
      const occurrence = language === "ny" ? "println(height)" : "say height";
      const selected = await complete(document, occurrence, "height", "hei", language === "ny" ? "f64" : "floating");
      const definitions = await vscode.commands.executeCommand("vscode.executeDefinitionProvider", document.uri, selected.start);
      assert.equal(definitions.length, 1);
      const expected = range(document, "(width, height)", "height");
      assert.ok((definitions[0].targetSelectionRange || definitions[0].range).isEqual(expected));
      return { language, tupleArity: 2 };
    });

    await check(`${language}: malformed interpolation reports an editor error and recovers after correction`, async () => {
      const good = fixture(language);
      const broken = good.replace(" {height} ", " {height plus} ");
      const document = await create(workspace, `diagnostics.${language}`, broken);
      const errors = await analyze(document);
      assert.ok(errors.length > 0);
      const edit = new vscode.WorkspaceEdit();
      edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), good);
      assert.equal(await vscode.workspace.applyEdit(edit), true);
      assert.deepEqual(await analyze(document), []);
      return { language, diagnosticCodes: errors.map(item => item.code) };
    });
  }
};
