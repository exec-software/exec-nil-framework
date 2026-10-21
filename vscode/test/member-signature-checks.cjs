const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const label = item => typeof item.label === "string" ? item.label : item.label.label;
const compilerMember = item => /^(field|method)(:|$)/.test(item.detail || "");
const replacement = item => item.range?.replacing || item.range;
async function eventually(operation, description) {
  const until = Date.now() + 15_000;
  do { const result = await operation(); if (result) return result; await pause(100); } while (Date.now() < until);
  throw new Error(`Timed out: ${description}`);
}
async function replace(document, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
  assert.equal(await vscode.workspace.applyEdit(edit), true);
}
async function create(workspace, filename, text) {
  const filenamePath = path.join(workspace, filename);
  await fs.mkdir(path.dirname(filenamePath), { recursive: true });
  await fs.writeFile(filenamePath, text);
  const document = await vscode.workspace.openTextDocument(filenamePath);
  await vscode.window.showTextDocument(document, { preview: false });
  return document;
}
function positionIn(document, context, beforeCursor = context) {
  const start = document.getText().indexOf(context);
  assert.notEqual(start, -1, `Missing fixture context: ${context}`);
  assert.ok(context.startsWith(beforeCursor), `Cursor prefix must belong to ${context}`);
  return document.positionAt(start + beforeCursor.length);
}
async function members(document, context, token, prefix = token) {
  const contextStart = document.getText().indexOf(context);
  const tokenStart = context.lastIndexOf(token);
  assert.ok(contextStart >= 0 && tokenStart >= 0, `Missing member fixture token: ${context}`);
  const start = contextStart + tokenStart;
  const range = new vscode.Range(document.positionAt(start), document.positionAt(start + token.length));
  const position = document.positionAt(start + prefix.length);
  const result = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, position);
  return { range, items: result?.items || [] };
}
async function checkedMembers(document, context, token, prefix = token) {
  await vscode.window.showTextDocument(document, { preview: false });
  const result = await eventually(async () => {
    const value = await members(document, context, token, prefix);
    return value.items.some(compilerMember) && value;
  }, `compiler member completion: ${context}`);
  const candidates = result.items.filter(compilerMember);
  // Absence can be retried while an editor generation settles. A nonempty
  // wrong answer must fail immediately, rather than hide behind another retry.
  assert.equal(new Set(candidates.map(label)).size, candidates.length);
  for (const item of candidates) {
    assert.equal(result.items.filter(other => label(other) === label(item)).length, 1,
      "The tolerant provider must not duplicate a compiler-resolved member.");
    assert.ok(replacement(item)?.isEqual(result.range), "Replace only the complete written member token.");
  }
  return { ...result, candidates };
}
function expectMember(result, name, detail, insertText = name) {
  const items = result.candidates.filter(item => label(item) === name);
  assert.equal(items.length, 1, `Expected ${name}; got ${result.candidates.map(label)}`);
  assert.equal(items[0].detail, detail);
  assert.equal(items[0].insertText, insertText);
  return items[0];
}
async function signatures(document, context, beforeCursor = context) {
  return vscode.commands.executeCommand("vscode.executeSignatureHelpProvider", document.uri,
    positionIn(document, context, beforeCursor));
}
function parameterLabels(signature) {
  return signature.parameters.map(item => Array.isArray(item.label)
    ? signature.label.slice(item.label[0], item.label[1]) : item.label);
}
function expectSignature(result, names, activeParameter) {
  assert.equal(result.signatures.length, 1, "Return the checked callable, without competing guesses.");
  assert.equal(result.activeSignature, 0);
  const signature = result.signatures[0];
  assert.equal(signature.parameters.length, names.length);
  const labels = parameterLabels(signature);
  for (let i = 0; i < names.length; i++) {
    assert.ok(labels[i].includes(names[i]), `Expected parameter ${names[i]}; got ${labels[i]}`);
  }
  assert.equal(result.activeParameter, activeParameter);
  return signature;
}
async function checkedSignature(document, context, beforeCursor, names, activeParameter) {
  await vscode.window.showTextDocument(document, { preview: false });
  const result = await eventually(async () => {
    const value = await signatures(document, context, beforeCursor);
    return value?.signatures?.length && value;
  }, `compiler signature: ${context}`);
  expectSignature(result, names, activeParameter);
  return result;
}

exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) await check(`${language}: compiler members follow receiver types and exact UTF-8 replacement ranges`, async () => {
    const text = language === "ny" ?
      '# é😀\r\nstruct Numeric\r\n pub shared_count: i32\r\nend struct\r\nstruct Textual\r\n pub shared_text: text\r\nend struct\r\nfn main() -> i32\r\n let numeric = Numeric { shared_count: 7 }\r\n let textual = Textual { shared_text: "hello" }\r\n println(numeric.shared_count)\r\n println(textual.shared_text)\r\n return 0\r\nend fn\r\n' :
      'book members.receivers\r\n!!! é😀\r\nnumeric begin record\r\n field shared-count as number\r\nend record\r\ntextual begin record\r\n field shared-text as text\r\nend record\r\nmain returns number begin paragraph\r\n value first is build numeric with shared-count being 7\r\n value second is build textual with shared-text being "hello"\r\n say first.shared-count\r\n say second.shared-text\r\n give 0\r\nend paragraph\r\n';
    const document = await create(workspace, `member receivers/${language}.${language}`, text);
    const numericToken = language === "ny" ? "shared_count" : "shared-count";
    const textToken = language === "ny" ? "shared_text" : "shared-text";
    const first = await checkedMembers(document, language === "ny" ? `numeric.${numericToken}` : `first.${numericToken}`, numericToken, "shared");
    assert.deepEqual(first.candidates.map(label), [numericToken]);
    expectMember(first, numericToken, language === "ny" ? "field: i32" : "field: number");
    const second = await checkedMembers(document, language === "ny" ? `textual.${textToken}` : `second.${textToken}`, textToken, "shared");
    assert.deepEqual(second.candidates.map(label), [textToken]);
    expectMember(second, textToken, language === "ny" ? "field: String" : "field: text");
    return { language, lineEndings: "CRLF", receiverTypes: 2 };
  });

  await check("compiler members follow dirty cross-language imports and exclude private methods", async () => {
    const original = 'module widgets\npub struct Widget\n pub seed: i32\nend struct\nimpl Widget\n pub fn fetch_disk(self) -> i32\n  return 1\n end fn\n fn fetch_private(self) -> i32\n  return 2\n end fn\nend impl\n';
    const dirty = original.replace('fetch_disk(self) -> i32\n  return 1', 'fetch_latest(self) -> text\n  return "latest"');
    const dependency = await create(workspace, "member imports/widgets.ny", original);
    await replace(dependency, dirty);
    const entry = await create(workspace, "member imports/main.matrix", 'book members.imported\nread Widget from widgets\nmain returns number begin paragraph\n value instance is build Widget with seed being 0\n say instance.fetch-pending with nothing\n give 0\nend paragraph\n');
    const first = await checkedMembers(entry, "instance.fetch-pending", "fetch-pending", "fetch");
    assert.deepEqual(first.candidates.map(label), ["fetch_latest"]);
    expectMember(first, "fetch_latest", "method: text");
    assert.equal(first.items.some(item => /private|disk/.test(label(item))), false);
    await replace(dependency, dirty.replaceAll("latest", "newest"));
    const changed = await checkedMembers(entry, "instance.fetch-pending", "fetch-pending", "fetch");
    assert.deepEqual(changed.candidates.map(label), ["fetch_newest"]);
    expectMember(changed, "fetch_newest", "method: text");
    assert.equal(await fs.readFile(dependency.uri.fsPath, "utf8"), original);
    return { imported: "Ny to Matrix", dirty: true, privateExcluded: true };
  });

  await check("unknown member receivers do not publish compiler guesses", async () => {
    const document = await create(workspace, "member unsupported/main.ny", 'struct Known\n pub shared_count: i32\nend struct\nfn main() -> i32\n return missing.shared_pending\nend fn\n');
    const result = await members(document, "missing.shared_pending", "shared_pending", "shared");
    assert.equal(result.items.some(compilerMember), false);
  });

  await check("Ny signatures select nested calls, empty calls and unresolved argument expressions", async () => {
    const prefix = '# é😀\r\nfn outer(first: i32, second: i32) -> i32\r\n return first + second\r\nend fn\r\nfn inner(value: i32) -> i32\r\n return value\r\nend fn\r\nfn main() -> i32\r\n return ';
    const document = await create(workspace, "signature calls/main.ny", prefix + 'outer(inner(7), 9)\r\nend fn\r\n');
    await checkedSignature(document, "outer(inner(7), 9)", "outer(inner(7", ["value"], 0);
    await checkedSignature(document, "outer(inner(7), 9)", "outer(inner(7), ", ["first", "second"], 1);
    await replace(document, prefix + 'outer()\r\nend fn\r\n');
    await checkedSignature(document, "outer()", "outer(", ["first", "second"], 0);
    await replace(document, prefix + 'outer(missing, 9)\r\nend fn\r\n');
    await checkedSignature(document, "outer(missing, 9)", "outer(miss", ["first", "second"], 0);
    return { lineEndings: "CRLF", nested: true, incompleteArguments: true };
  });

  await check("Matrix signatures map named source arguments to declared parameter order", async () => {
    const text = 'book signatures.labels\r\n!!! é😀\r\nordered receives first-part as number, second-part as number returns number begin paragraph\r\n give first-part plus second-part\r\nend paragraph\r\ninner receives value-part as number returns number begin paragraph\r\n give value-part\r\nend paragraph\r\nmain returns number begin paragraph\r\n give (ordered with second-part being (inner with value-part being 2), first-part being 1)\r\nend paragraph\r\n';
    const document = await create(workspace, "signature labels/main.matrix", text);
    const call = "ordered with second-part being (inner with value-part being 2), first-part being 1";
    await checkedSignature(document, call, "ordered with second-part being ", ["first-part", "second-part"], 1);
    await checkedSignature(document, call, "ordered with second-part being (inner with value-part being 2", ["value-part"], 0);
    await checkedSignature(document, call, call, ["first-part", "second-part"], 0);
    await replace(document, text.replace("ordered with second-part being", "ordered with unknown-part being"));
    const unknown = await eventually(async () => {
      const value = await signatures(document, "ordered with unknown-part being", "ordered with unknown-part being");
      return value?.signatures?.length && value;
    }, "unknown Matrix argument label retains the resolved signature");
    assert.equal(unknown.signatures.length, 1);
    assert.ok(unknown.signatures[0].label.includes("first-part") && unknown.signatures[0].label.includes("second-part"));
    assert.deepEqual(unknown.signatures[0].parameters, [], "An unknown label must not falsely highlight parameter zero.");
    await replace(document, text.replace(`give (${call})`, "give ordered with "));
    await checkedSignature(document, "give ordered with ", "give ordered with ", ["first-part", "second-part"], 0);
    return { language: "matrix", namedOrder: [1, 0], nested: true, unknownLabel: true, bareWith: true };
  });

  await check("signature help follows dirty imported declarations across frontends", async () => {
    const original = 'book provider\npublic combine receives saved-part as override integer, other-part as override integer returns override integer begin paragraph\n give saved-part plus other-part\nend paragraph\n';
    const dependency = await create(workspace, "signature imports/provider.matrix", original);
    await replace(dependency, original.replaceAll("saved-part", "dirty-part"));
    const entry = await create(workspace, "signature imports/main.ny", 'module signatures.entry\nimport provider.*\nfn main() -> i32\n return combine(1, 2)\nend fn\n');
    await checkedSignature(entry, "combine(1, 2)", "combine(1, ", ["dirty_part", "other_part"], 1);
    await replace(dependency, original.replaceAll("saved-part", "newest-part"));
    await checkedSignature(entry, "combine(1, 2)", "combine(1, ", ["newest_part", "other_part"], 1);
    assert.equal(await fs.readFile(dependency.uri.fsPath, "utf8"), original);
    return { dirty: true, imported: "Matrix to Ny" };
  });

  await check("Matrix signature help follows immutable callback bindings rather than paragraph spelling", async () => {
    const text = 'book signatures.callbacks\noperation receives first-part as number, second-part as number returns number begin paragraph\n give first-part plus second-part\nend paragraph\nalternate receives second-part as number, first-part as number returns number begin paragraph\n give first-part plus second-part\nend paragraph\nmain returns number begin paragraph\n value operation is the paragraph alternate\n give (operation with first-part being 1, second-part being 2)\nend paragraph\n';
    const document = await create(workspace, "signature callbacks/main.matrix", text);
    await checkedSignature(document, "operation with first-part being 1, second-part being 2", "operation with first-part being 1", ["second_part", "first_part"], 1);
    return { shadowed: true, activeParameter: 1 };
  });

  await check("method signatures omit the implicit receiver and preserve named argument mapping", async () => {
    const text = 'book signatures.methods\ncalculator begin record\n field seed as number\nend record\ncalculator begin implementation\n calculate receives left-part as number, right-part as number returns number begin paragraph\n  give left-part plus right-part\n end paragraph\nend implementation\nmain returns number begin paragraph\n value instance is build calculator with seed being 0\n give (instance.calculate with right-part being 2, left-part being 1)\nend paragraph\n';
    const document = await create(workspace, "signature methods/main.matrix", text);
    await checkedSignature(document, "instance.calculate with right-part being 2, left-part being 1", "instance.calculate with right-part being 2", ["left-part", "right-part"], 1);
    return { implicitReceiverExcluded: true };
  });

  await check("unresolved callees do not inherit same-spelled unrelated signatures", async () => {
    const document = await create(workspace, "signature unsupported/main.ny", 'fn existing(value: i32) -> i32\n return value\nend fn\nfn main() -> i32\n return absent(7)\nend fn\n');
    const result = await signatures(document, "absent(7)", "absent(");
    assert.ok(!result || result.signatures.length === 0);
  });

  if (process.platform !== "win32") await check("signature queries retry operational failures and cancel changed source snapshots", async () => {
    const configuration = vscode.workspace.getConfiguration("ny");
    const originalCompiler = configuration.get("compiler.path");
    const fake = path.join(workspace, "signature-probe.cjs");
    const log = path.join(workspace, "signature-probe.jsonl");
    const unavailable = path.join(workspace, "signature-unavailable.flag");
    const entryPath = path.join(workspace, "signature cancellation", "main.ny");
    const dependencyPath = path.join(workspace, "signature cancellation", "dependency.ny");
    const source = marker => `# ${marker}\nfn main() -> i32\n return probe(7)\nend fn\n`;
    const dependency = await create(workspace, "signature cancellation/dependency.ny", "# CURRENT\n");
    const entry = await create(workspace, "signature cancellation/main.ny", source("CURRENT"));
    await fs.writeFile(fake, `#!/usr/bin/env node
const fs = require('node:fs');
const primary = process.argv[process.argv.indexOf('--workspace-stdin') + 1];
const query = process.argv.indexOf('--signature-at');
let input = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => input += chunk);
process.stdin.on('end', () => {
  const request = JSON.parse(input);
  const current = request.sources.find(item => item.path === primary);
  const dependency = request.sources.find(item => item.path === ${JSON.stringify(dependencyPath)});
  const ours = primary === ${JSON.stringify(entryPath)} && query >= 0;
  const stale = ours && [current?.text, dependency?.text].some(text => text?.includes('WAIT_'));
  if (ours) fs.appendFileSync(${JSON.stringify(log)}, JSON.stringify({ pid:process.pid,text:current.text,dependency:dependency?.text }) + '\\n');
  if (ours && fs.existsSync(${JSON.stringify(unavailable)})) {
    fs.unlinkSync(${JSON.stringify(unavailable)});
    process.exitCode = 2;
    return;
  }
  const name = stale ? 'only_stale' : 'current';
  const signature = ours ? {schema:'nil.signature.v1',partial:true,path:primary,cursor:Number(process.argv[query + 1]),
    start:Buffer.byteLength(current.text.slice(0,current.text.indexOf('probe(7)') + 6)),length:1,
    sources:[current,...(dependency?[dependency]:[])],label:'probe(' + name + ': i32) -> i32',
    parameters:[{name,type:'i32',label:name + ': i32',variadic:false}],active_parameter:0,return_type:'i32'} : undefined;
  setTimeout(() => process.stdout.write(JSON.stringify({schema:'nil.analysis.v1',source:primary,
    language:primary.endsWith('.ny')?'ny':'matrix',success:true,reflection:{},diagnostics:[],sources:[],signature})),stale?5000:0);
});
`, { mode: 0o755 });
    const waitForRequest = marker => eventually(async () => {
      let text; try { text = await fs.readFile(log, "utf8"); } catch { return false; }
      return text.trim().split("\n").map(line => JSON.parse(line)).find(item =>
        item.text.includes(marker) || item.dependency?.includes(marker));
    }, `delayed signature ${marker} starts`);
    const exited = pid => eventually(() => {
      try { process.kill(pid, 0); return false; } catch (error) { return error.code === "ESRCH"; }
    }, "cancelled signature compiler exits");
    try {
      await configuration.update("compiler.path", fake, vscode.ConfigurationTarget.Workspace);
      await checkedSignature(entry, "probe(7)", "probe(", ["current"], 0);
      const versions = [entry.version, dependency.version];
      await fs.writeFile(unavailable, "fail the next signature query");
      await eventually(async () => {
        const failed = await signatures(entry, "probe(7)", "probe(7");
        assert.ok(!failed || failed.signatures.length === 0,
          "An unavailable signature query must not return a cached answer.");
        try { await fs.stat(unavailable); return false; }
        catch (error) { if (error.code === "ENOENT") return true; throw error; }
      }, "unavailable signature query actually executes");
      await checkedSignature(entry, "probe(7)", "probe(7", ["current"], 0);
      assert.deepEqual([entry.version, dependency.version], versions);
      await replace(entry, source("WAIT_PRIMARY"));
      const stalePrimary = signatures(entry, "probe(7)", "probe(");
      const primaryRequest = await waitForRequest("WAIT_PRIMARY");
      await replace(entry, source("CURRENT"));
      const primaryResult = await stalePrimary;
      assert.equal(primaryResult?.signatures.some(item => item.label.includes("only_stale")) || false, false);
      await exited(primaryRequest.pid);
      await replace(dependency, "# WAIT_DEPENDENCY\n");
      const staleDependency = signatures(entry, "probe(7)", "probe(");
      const dependencyRequest = await waitForRequest("WAIT_DEPENDENCY");
      await replace(dependency, "# CURRENT\n");
      const dependencyResult = await staleDependency;
      assert.equal(dependencyResult?.signatures.some(item => item.label.includes("only_stale")) || false, false);
      await exited(dependencyRequest.pid);
      await checkedSignature(entry, "probe(7)", "probe(", ["current"], 0);
      return { unavailableRetried: true, cancelled: 2, recovered: true };
    } finally {
      await configuration.update("compiler.path", originalCompiler, vscode.ConfigurationTarget.Workspace);
    }
  });
};
