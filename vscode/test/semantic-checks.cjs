const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function eventually(operation, description) {
  const until = Date.now() + 15_000;
  let latest;
  do {
    try { const result = await operation(); if (result) return result; }
    catch (error) { latest = error; }
    await pause(100);
  } while (Date.now() < until);
  throw new Error(`Timed out: ${description}${latest ? ` (${latest.message})` : ""}`);
}
async function replace(document, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
  assert.equal(await vscode.workspace.applyEdit(edit), true);
}
async function analyze(document) {
  await vscode.window.showTextDocument(document);
  await eventually(() => vscode.commands.executeCommand("ny.analyzeCurrentFile"), "semantic compiler analysis completes");
}
function rangeIn(document, context, token) {
  const text = document.getText();
  const contextStart = text.indexOf(context);
  assert.notEqual(contextStart, -1, `Missing fixture context: ${context}`);
  const within = context.indexOf(token);
  assert.notEqual(within, -1, `Missing fixture token: ${token}`);
  const start = contextStart + within;
  return new vscode.Range(document.positionAt(start), document.positionAt(start + token.length));
}
async function definitions(document, context, token) {
  const range = rangeIn(document, context, token);
  // Place the cursor inside the token, including for a one-character name.
  const position = document.positionAt(document.offsetAt(range.start) + Math.floor(token.length / 2));
  return await vscode.commands.executeCommand("vscode.executeDefinitionProvider", document.uri, position) || [];
}
async function expectDefinition(document, useContext, useToken, target, declarationContext, declarationToken = useToken) {
  const origin = rangeIn(document, useContext, useToken);
  const selection = rangeIn(target, declarationContext, declarationToken);
  const results = await eventually(async () => {
    const value = await definitions(document, useContext, useToken);
    return value.length && value;
  }, `definition of ${useContext}`);
  // An absent result while a generation changes is safe. Once any destination
  // is returned, validate it immediately: retries must never hide a stale or
  // duplicate location that an actual user would have navigated to.
  assert.equal(results.length, 1, "The compiler must supply exactly one definition; legacy LSP guesses must not be added.");
  const [result] = results;
  assert.ok(result.originSelectionRange && result.targetSelectionRange && result.targetRange,
    "Semantic navigation must return a LocationLink with exact original-source selections.");
  assert.equal(result.targetUri.toString(), target.uri.toString());
  assert.ok(result.originSelectionRange.isEqual(origin), "Origin must select the actual source spelling.");
  assert.ok(result.targetSelectionRange.isEqual(selection), "Target must select the resolved declaration token.");
  assert.ok(result.targetRange.isEqual(selection), "Target range must not widen to an unrelated statement.");
  assert.equal(target.getText(result.targetSelectionRange), declarationToken);
  return result;
}
async function expectNoDefinition(document, context, token) {
  assert.deepEqual(await definitions(document, context, token), [],
    "Unsupported or failed semantic analysis must not fall back to a guessed or stale LSP destination.");
}
async function create(workspace, filename, text) {
  const filenamePath = path.join(workspace, filename);
  await fs.mkdir(path.dirname(filenamePath), { recursive: true });
  await fs.writeFile(filenamePath, text);
  return vscode.workspace.openTextDocument(filenamePath);
}

exports.run = async ({ check, workspace }) => {
  await check("Ny semantic definitions preserve lexical shadowing, parameters, loops and lambdas", async () => {
    const text = '# é😀\r\nfn compute(input_value: i32) -> i32\r\n'
      + ' let local_value = input_value\r\n if true then\r\n'
      + '  let local_value = 2\r\n  println(local_value)\r\n end if\r\n'
      + ' for loop_value in [1,2]\r\n  println(loop_value)\r\n end for\r\n'
      + ' let local_callback = fn(argument_value: i32) -> i32\r\n'
      + '  return argument_value\r\n end fn\r\n return local_value\r\nend fn\r\n';
    const document = await create(workspace, "semantic lexical/locals.ny", text);
    await analyze(document);
    await expectDefinition(document, "= input_value", "input_value", document, "compute(input_value:");
    await expectDefinition(document, "println(local_value)", "local_value", document, "let local_value = 2");
    await expectDefinition(document, "return local_value", "local_value", document, "let local_value = input_value");
    await expectDefinition(document, "println(loop_value)", "loop_value", document, "for loop_value in");
    await expectDefinition(document, "return argument_value", "argument_value", document, "fn(argument_value:");
    await replace(document, text.replace("return local_value", "return unresolved_value"));
    await analyze(document);
    await expectNoDefinition(document, "println(local_value)", "local_value");
    await replace(document, text);
    await analyze(document);
    await expectDefinition(document, "return local_value", "local_value", document, "let local_value = input_value");
    return { language: "ny", lineEndings: "CRLF", bindings: 5 };
  });

  await check("Matrix semantic definitions preserve Unicode, folded names, shadowing and escaped parameters", async () => {
    const text = 'book semantic.lexical\r\n!!! é😀\r\n'
      + 'compute receives café-value as number, `value` as number returns number begin paragraph\r\n'
      + ' value local-item is CAFé_VALUE\r\n when true then\r\n'
      + '  value local-item is 2\r\n  say local-item\r\n end when\r\n'
      + ' repeat for each loop-item in [1,2]\r\n  say loop-item\r\n end repeat\r\n'
      + ' value local-callback is anonymous receives argument-item as number returns number begin paragraph\r\n'
      + '  give argument-item\r\n end paragraph\r\n'
      + ' evaluate `value`\r\n give LOCALITEM\r\nend paragraph\r\n';
    const document = await create(workspace, "semantic lexical/locals.matrix", text);
    await analyze(document);
    await expectDefinition(document, "is CAFé_VALUE", "CAFé_VALUE", document, "receives café-value", "café-value");
    await expectDefinition(document, "say local-item", "local-item", document, "value local-item is 2");
    await expectDefinition(document, "give LOCALITEM", "LOCALITEM", document, "value local-item is CAFé_VALUE", "local-item");
    await expectDefinition(document, "say loop-item", "loop-item", document, "each loop-item in");
    await expectDefinition(document, "give argument-item", "argument-item", document, "receives argument-item");
    await expectDefinition(document, "evaluate `value`", "value", document, "`value` as number");
    const moved = text.replace(" value local-item is", " !!! new source line\r\n value local-item is");
    await replace(document, moved);
    await analyze(document);
    await expectDefinition(document, "give LOCALITEM", "LOCALITEM", document, "value local-item is CAFé_VALUE", "local-item");
    return { language: "matrix", lineEndings: "CRLF", bindings: 6, dirty: true };
  });

  for (const language of ["ny", "matrix"]) await check(`${language}: semantic definitions follow dirty cross-language imports without stale destinations`, async () => {
    const dependencyLanguage = language === "ny" ? "matrix" : "ny";
    const directory = `semantic imports ${language}`;
    const depText = dependencyLanguage === "ny"
      ? 'module helper\npub fn fetch_value(input: i32) -> i32\n return input\nend fn\n'
      : 'book helper\npublic fetch-value receives input-item as override integer returns override integer begin paragraph\n give input-item\nend paragraph\n';
    const entryText = language === "ny"
      ? 'import helper.*\nfn main() -> i32\n return fetch_value(7)\nend fn\n'
      : 'book semantic.entry\nread everything from helper\nmain returns override integer begin paragraph\n give (fetch-value with 7)\nend paragraph\n';
    const dependency = await create(workspace, `${directory}/helper.${dependencyLanguage}`, depText);
    const entry = await create(workspace, `${directory}/main.${language}`, entryText);
    const use = language === "ny" ? "fetch_value" : "fetch-value";
    const declared = dependencyLanguage === "ny" ? "fetch_value" : "fetch-value";
    const header = dependencyLanguage === "ny" ? `fn ${declared}(` : `public ${declared} receives`;
    await analyze(entry);
    await expectDefinition(entry, use, use, dependency, header, declared);
    const comment = dependencyLanguage === "ny" ? "# é😀 dirty declaration\r\n\r\n" : "!!! é😀 dirty declaration\r\n\r\n";
    const dirtyText = comment + depText.replace(/\n/g, "\r\n");
    await replace(dependency, dirtyText);
    await analyze(entry);
    await expectDefinition(entry, use, use, dependency, header, declared);
    assert.equal(dependency.isDirty, true);
    assert.equal(await fs.readFile(dependency.uri.fsPath, "utf8"), depText);
    await replace(dependency, dirtyText.replace(declared, dependencyLanguage === "ny" ? "renamed_value" : "renamed-value"));
    await analyze(entry);
    await expectNoDefinition(entry, use, use);
    await replace(dependency, dirtyText);
    await analyze(entry);
    await expectDefinition(entry, use, use, dependency, header, declared);
    assert.equal(await fs.readFile(dependency.uri.fsPath, "utf8"), depText);
    return { language, dependencyLanguage, saved: false };
  });

  await check("semantic definitions target never-written named untitled imports and disappear after close", async () => {
    const directory = path.join(workspace, "semantic new import");
    await fs.mkdir(directory, { recursive: true });
    const targetPath = path.join(directory, "fresh.matrix");
    let dependency = await vscode.workspace.openTextDocument(vscode.Uri.file(targetPath).with({ scheme: "untitled" }));
    dependency = await vscode.languages.setTextDocumentLanguage(dependency, "matrix");
    await replace(dependency, 'book fresh\r\n!!! é😀\r\npublic new-answer returns override integer begin paragraph\r\n give 7\r\nend paragraph\r\n');
    const entry = await create(workspace, "semantic new import/main.ny",
      'import fresh.*\nfn main() -> i32\n return new_answer()\nend fn\n');
    await analyze(entry);
    const result = await expectDefinition(entry, "return new_answer", "new_answer", dependency,
      "public new-answer", "new-answer");
    assert.equal(result.targetUri.scheme, "untitled");
    await assert.rejects(fs.stat(targetPath), { code: "ENOENT" });
    await vscode.window.showTextDocument(dependency);
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
    await eventually(() => dependency.isClosed, "named untitled semantic target closes");
    await analyze(entry);
    await expectNoDefinition(entry, "return new_answer", "new_answer");
    await assert.rejects(fs.stat(targetPath), { code: "ENOENT" });
  });

  if (process.platform !== "win32") await check("semantic definitions reject dirty include aliases and observe arbitrary include disk changes", async () => {
    const directory = path.join(workspace, "semantic include alias");
    await fs.mkdir(directory, { recursive: true });
    const headerPath = path.join(directory, "helper.inc");
    const aliasPath = path.join(directory, "helper-alias.inc");
    const headerText = 'pub fn included_value() -> i32\n return 7\nend fn\n';
    await fs.writeFile(headerPath, headerText);
    await fs.symlink(headerPath, aliasPath);
    await fs.writeFile(path.join(directory, "wrapper.ny"), 'module wrapper\n@include "helper.inc"\n');
    const entry = await create(workspace, "semantic include alias/main.ny",
      'import wrapper.*\nfn main() -> i32\n let local_count = included_value()\n return local_count\nend fn\n');
    await analyze(entry);
    await expectDefinition(entry, "return local_count", "local_count", entry, "let local_count =");
    // Keep the source tab open: replacing its preview tab would disable its
    // provider and make the alias guard appear to pass without exercising it.
    await vscode.window.showTextDocument(entry, { preview: false });
    const alias = await vscode.workspace.openTextDocument(aliasPath);
    await vscode.window.showTextDocument(alias, { preview: false });
    await replace(alias, headerText.replace("return 7", 'return "dirty header"'));
    assert.equal(alias.isDirty, true);
    await expectNoDefinition(entry, "return local_count", "local_count");
    assert.equal(await fs.readFile(headerPath, "utf8"), headerText);
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
    await eventually(() => !vscode.window.tabGroups.all.some(group => group.tabs.some(tab =>
      tab.input?.uri?.toString() === alias.uri.toString())), "include alias tab closes");
    await vscode.window.showTextDocument(entry, { preview: false });
    await expectDefinition(entry, "return local_count", "local_count", entry, "let local_count =");

    // The target declaration is unchanged, but a reached .inc file changes
    // the meaning of its initializer. Cache validation and the exact source
    // watcher must cover that file even though it has no editable bindings.
    await fs.writeFile(headerPath, headerText.replace("-> i32", "-> text").replace("return 7", 'return "disk change"'));
    await expectNoDefinition(entry, "return local_count", "local_count");
    await eventually(() => vscode.languages.getDiagnostics(entry.uri).some(item =>
      item.source === "nil-analysis" && /type mismatch/i.test(item.message)),
      "arbitrary include disk watcher rechecks dependent source");
    await fs.writeFile(headerPath, headerText);
    await expectDefinition(entry, "return local_count", "local_count", entry, "let local_count =");
    return { includeExtension: ".inc", alias: true, unchangedTarget: true };
  });

  await check("unsupported expanded-source, member and type bindings never use legacy guessed definitions", async () => {
    const expanded = await create(workspace, "semantic unsupported/expanded.ny",
      '@define VALUE 7\nfn main() -> i32\n let local_value = VALUE\n return local_value\nend fn\n');
    await analyze(expanded);
    await expectNoDefinition(expanded, "return local_value", "local_value");
    const document = await create(workspace, "semantic unsupported/members.matrix",
      'book semantic.members\nPoint begin record\n field x as number\nend record\n'
      + 'read-point receives point-value as Point returns number begin paragraph\n'
      + ' give point-value.x\nend paragraph\n');
    await analyze(document);
    // The object itself is supported; the unresolved member/type ranges must
    // stay absent even though the older LSP can guess similarly named symbols.
    await expectDefinition(document, "give point-value", "point-value", document, "receives point-value");
    await expectNoDefinition(document, "point-value.x", "x");
    await expectNoDefinition(document, "as Point returns", "Point");
  });
};
