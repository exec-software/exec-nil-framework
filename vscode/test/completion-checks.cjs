const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const label = item => typeof item.label === "string" ? item.label : item.label.label;
const compilerItem = item => /^(function|variable|parameter)(:|$)/.test(item.detail || "");
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
function queryRange(document, context, token, prefix = token) {
  const contextStart = document.getText().indexOf(context);
  assert.notEqual(contextStart, -1, `Missing fixture context: ${context}`);
  const tokenStart = context.indexOf(token);
  assert.notEqual(tokenStart, -1, `Missing fixture token: ${token}`);
  const start = contextStart + tokenStart;
  const range = new vscode.Range(document.positionAt(start), document.positionAt(start + token.length));
  return { range, position: document.positionAt(start + prefix.length) };
}
async function completions(document, context, token, prefix = token, trigger) {
  const query = queryRange(document, context, token, prefix);
  const result = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, query.position, trigger);
  return { ...query, items: result?.items || [] };
}
async function checkedCompletions(document, context, token, prefix = token) {
  await vscode.window.showTextDocument(document, { preview: false });
  const result = await eventually(async () => {
    const result = await completions(document, context, token, prefix);
    return result.items.some(compilerItem) && result;
  }, `compiler completion of ${context}`);
  // Retry only while no compiler answer is available. Once it arrives, any
  // duplicate, stale, or out-of-scope candidate fails on this exact result.
  const candidates = result.items.filter(compilerItem);
  assert.equal(new Set(candidates.map(label)).size, candidates.length, "Compiler candidates must be unique.");
  for (const item of candidates) {
    assert.equal(result.items.filter(other => label(other) === label(item)).length, 1,
      "The tolerant provider must not duplicate a compiler-resolved candidate.");
    assert.ok(replacement(item)?.isEqual(result.range), "Every candidate must replace the complete original token.");
  }
  return { ...result, candidates };
}
function expectCandidate(result, name, detail, insertText = name) {
  const matches = result.candidates.filter(item => label(item) === name);
  assert.equal(matches.length, 1, `Expected exactly one ${name}; got ${result.candidates.map(label)}`);
  assert.equal(matches[0].detail, detail);
  assert.equal(matches[0].insertText, insertText);
  return matches[0];
}
function expectNames(result, names) {
  assert.deepEqual(result.candidates.map(label).sort(), [...names].sort());
}

exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) await check(`${language}: compiler completion respects shadowing and source-order scopes`, async () => {
    const text = language === "ny" ?
      '# é😀\r\nmodule completion.scopes\r\nfn compute(scoped_input: i32) -> i32\r\n let scoped_value = scoped_input\r\n if true then\r\n  let scoped_value = "inner"\r\n  println(scoped_value)\r\n  let scoped_future = 3\r\n end if\r\n for scoped_loop in [1,2]\r\n  println(scoped_loop)\r\n end for\r\n return scoped_value\r\nend fn\r\n' :
      'book completion.scopes\r\n!!! é😀\r\ncompute receives scoped-input as override integer returns override integer begin paragraph\r\n value scoped-value is scoped-input\r\n when true then\r\n  value scoped-value is "inner"\r\n  say scoped-value\r\n  value scoped-future is 3\r\n end when\r\n repeat for each scoped-loop in [1,2]\r\n  say scoped-loop\r\n end repeat\r\n give scoped-value\r\nend paragraph\r\n';
    const document = await create(workspace, `completion scopes/${language}.${language}`, text);
    const spelling = name => language === "ny" ? name.replaceAll("-", "_") : name;
    const value = spelling("scoped-value"), input = spelling("scoped-input"), loop = spelling("scoped-loop");
    const prefix = language === "ny" ? "scoped_" : "scoped";
    const inner = await checkedCompletions(document, language === "ny" ? `println(${value})` : `say ${value}`, value, prefix);
    expectNames(inner, [input, value]);
    expectCandidate(inner, value, language === "ny" ? "variable: String" : "variable: text");
    expectCandidate(inner, input, language === "ny" ? "parameter: i32" : "parameter: override integer");
    const body = await checkedCompletions(document, language === "ny" ? `println(${loop})` : `say ${loop}`, loop, prefix);
    expectNames(body, [input, value, loop]);
    const outer = await checkedCompletions(document, language === "ny" ? `return ${value}` : `give ${value}`, value, prefix);
    expectNames(outer, [input, value]);
    expectCandidate(outer, value, language === "ny" ? "variable: i32" : "variable: override integer");
    return { language, lineEndings: "CRLF", scopes: 3 };
  });

  await check("Ny incomplete identifiers and mid-token completion preserve exact replacement text", async () => {
    const text = '# é😀\r\nfn main() -> i32\r\n let cafe_value = 7\r\n return cafe_unfinished\r\nend fn\r\n';
    const document = await create(workspace, "completion replacement/main.ny", text);
    const result = await checkedCompletions(document, "return cafe_unfinished", "cafe_unfinished", "cafe_");
    expectNames(result, ["cafe_value"]);
    expectCandidate(result, "cafe_value", "variable: i32");
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri, replacement(result.candidates[0]), result.candidates[0].insertText);
    assert.equal(await vscode.workspace.applyEdit(edit), true);
    assert.equal(document.getText(), text.replace("return cafe_unfinished", "return cafe_value"));
    assert.equal(await fs.readFile(document.uri.fsPath, "utf8"), text);
    return { dirty: true, replaced: "cafe_unfinished", inserted: "cafe_value" };
  });

  await check("Matrix completion folds names and replaces escaped Unicode tokens including delimiters", async () => {
    const text = 'book completion.escaped\r\n!!! é😀\r\ncompute receives café-value as number, `value` as number returns number begin paragraph\r\n evaluate `valSuffix`\r\n give CAFé_VALUEx\r\nend paragraph\r\n';
    const document = await create(workspace, "completion replacement/main.matrix", text);
    const escaped = await checkedCompletions(document, "evaluate `valSuffix`", "`valSuffix`", "`val");
    expectNames(escaped, ["value"]);
    expectCandidate(escaped, "value", "parameter: number", "`value`");
    // Repair the earlier unresolved expression before querying the later one.
    await replace(document, text.replace("`valSuffix`", "`value`"));
    const folded = await checkedCompletions(document, "give CAFé_VALUEx", "CAFé_VALUEx", "CAFé_VAL");
    expectNames(folded, ["café-value"]);
    expectCandidate(folded, "café-value", "parameter: number");
    return { lineEndings: "CRLF", escaped: true, folded: true };
  });

  for (const language of ["ny", "matrix"]) await check(`${language}: compiler completion follows dirty cross-language exports`, async () => {
    const dependencyLanguage = language === "ny" ? "matrix" : "ny";
    const original = dependencyLanguage === "ny" ?
      'module helper\npub fn fetch_disk() -> i32\n return 1\nend fn\n' :
      'book helper\npublic fetch-disk returns override integer begin paragraph\n give 1\nend paragraph\n';
    const dirty = dependencyLanguage === "ny" ?
      '# é😀 unsaved export\r\nmodule helper\r\npub fn fetch_latest() -> text\r\n return "latest"\r\nend fn\r\n' :
      'book helper\r\n!!! é😀 unsaved export\r\npublic fetch-latest returns text begin paragraph\r\n give "latest"\r\nend paragraph\r\n';
    const dependency = await create(workspace, `completion imports ${language}/helper.${dependencyLanguage}`, original);
    await replace(dependency, dirty);
    const entry = await create(workspace, `completion imports ${language}/main.${language}`, language === "ny" ?
      'module completion.entry\nimport helper.*\nfn main() -> i32\n return fetch_pending()\nend fn\n' :
      'book completion.entry\nread everything from helper\nmain returns number begin paragraph\n give (fetch-pending with nothing)\nend paragraph\n');
    const token = language === "ny" ? "fetch_pending" : "fetch-pending";
    const result = await checkedCompletions(entry, token, token, "fetch");
    expectNames(result, ["fetch_latest"]);
    expectCandidate(result, "fetch_latest", language === "ny" ? "function: String" : "function: text");
    assert.equal(dependency.isDirty, true);
    assert.equal(await fs.readFile(dependency.uri.fsPath, "utf8"), original);
    await replace(dependency, dirty.replaceAll("latest", "newest"));
    const changed = await checkedCompletions(entry, token, token, "fetch");
    expectNames(changed, ["fetch_newest"]);
    expectCandidate(changed, "fetch_newest", language === "ny" ? "function: String" : "function: text");
    return { language, dependencyLanguage, saved: false };
  });

  await check("compiler completion finds never-saved named imports and invalidates them after close", async () => {
    const directory = path.join(workspace, "completion new import");
    await fs.mkdir(directory, { recursive: true });
    const filename = path.join(directory, "fresh.matrix");
    let dependency = await vscode.workspace.openTextDocument(vscode.Uri.file(filename).with({ scheme: "untitled" }));
    dependency = await vscode.languages.setTextDocumentLanguage(dependency, "matrix");
    await replace(dependency, 'book fresh\npublic fresh-answer returns override integer begin paragraph\n give 7\nend paragraph\n');
    const entry = await create(workspace, "completion new import/main.ny", 'module completion.entry\nimport fresh.*\nfn main() -> i32\n return fresh_pending()\nend fn\n');
    const result = await checkedCompletions(entry, "fresh_pending", "fresh_pending", "fresh_");
    expectNames(result, ["fresh_answer"]);
    expectCandidate(result, "fresh_answer", "function: i32");
    await assert.rejects(fs.stat(filename), { code: "ENOENT" });
    await vscode.window.showTextDocument(dependency);
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
    await eventually(() => dependency.isClosed, "never-saved completion dependency closes");
    const closed = await completions(entry, "fresh_pending", "fresh_pending", "fresh_");
    assert.equal(closed.items.some(item => label(item) === "fresh_answer"), false,
      "Closing an unsaved export must not leave a cached compiler candidate.");
  });

  await check("completion preserves Matrix keyword, type and typed-member fallback", async () => {
    const text = 'book completion.fallback\nPoint begin record\n field x as number\n field y as number\nend record\nmain returns number begin paragraph\n value sample as Point is build Point with x being 3, y being 4\n say sample.x\n give 0\nend paragraph\n';
    const document = await create(workspace, "completion fallback/main.matrix", text);
    const member = await eventually(async () => {
      const result = await completions(document, "say sample.x", "sample.x", "sample.", ".");
      return result.items.some(item => label(item) === "x") && result;
    }, "typed member fallback");
    assert.ok(member.items.some(item => label(item) === "y"));
    assert.equal(member.items.some(compilerItem), false, "Member contexts must not publish ordinary compiler locals.");
    await replace(document, text.replace("as Point is", "as num is"));
    const type = await eventually(async () => {
      const result = await completions(document, "as num is", "num");
      return result.items.some(item => label(item) === "number") && result;
    }, "Matrix type fallback");
    assert.equal(type.items.some(compilerItem), false);
    await replace(document, text.replace(" say sample.x", " giv"));
    const keyword = await eventually(async () => {
      const result = await completions(document, " giv", "giv");
      return result.items.some(item => label(item) === "give") && result;
    }, "Matrix keyword fallback");
    assert.equal(keyword.items.filter(item => label(item) === "give").length, 1);
    return { member: true, type: true, keyword: true };
  });

  if (process.platform !== "win32") await check("compiler completion recovers unavailable queries and cancels changed snapshots", async () => {
    const configuration = vscode.workspace.getConfiguration("ny");
    const originalCompiler = configuration.get("compiler.path");
    const fake = path.join(workspace, "completion-probe.cjs");
    const log = path.join(workspace, "completion-probe.jsonl");
    const unavailable = path.join(workspace, "completion-unavailable.flag");
    const entryPath = path.join(workspace, "completion cancellation", "main.ny");
    const dependencyPath = path.join(workspace, "completion cancellation", "dependency.ny");
    const source = marker => `# ${marker}\nfn main() -> i32\n return probe_waiting\nend fn\n`;
    const dependency = await create(workspace, "completion cancellation/dependency.ny", "# CURRENT\n");
    const entry = await create(workspace, "completion cancellation/main.ny", source("CURRENT"));
    await fs.writeFile(fake, `#!/usr/bin/env node
const fs = require('node:fs');
const primary = process.argv[process.argv.indexOf('--workspace-stdin') + 1];
const query = process.argv.indexOf('--completion-at');
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
  const completion = ours ? {schema:'nil.completion.v1',partial:true,path:primary,cursor:Number(process.argv[query + 1]),
    start:Buffer.byteLength(current.text.slice(0,current.text.indexOf('probe_waiting'))),length:13,
    sources:[current,...(dependency?[dependency]:[])],items:[{name:stale?'probe_only_stale':'probe_current',
      kind:'variable',type:'i32',insert_text:stale?'probe_only_stale':'probe_current'}]} : undefined;
  setTimeout(() => process.stdout.write(JSON.stringify({schema:'nil.analysis.v1',source:primary,
    language:primary.endsWith('.ny')?'ny':'matrix',success:true,reflection:{},diagnostics:[],sources:[],completion})),stale?5000:0);
});
`, { mode: 0o755 });
    const waitForRequest = marker => eventually(async () => {
      let text; try { text = await fs.readFile(log, "utf8"); } catch { return false; }
      return text.trim().split("\n").map(line => JSON.parse(line)).find(item =>
        item.text.includes(marker) || item.dependency?.includes(marker));
    }, `delayed completion ${marker} starts`);
    const exited = pid => eventually(() => {
      try { process.kill(pid, 0); return false; } catch (error) { return error.code === "ESRCH"; }
    }, "cancelled completion compiler exits");
    try {
      await configuration.update("compiler.path", fake, vscode.ConfigurationTarget.Workspace);
      // Wait until the configuration event and file-watch invalidations settle.
      const ready = await checkedCompletions(entry, "probe_waiting", "probe_waiting", "probe");
      expectCandidate(ready, "probe_current", "variable: i32");
      // A different cursor makes a new query, while a .flag control file does
      // not invalidate any source snapshot. The first process fails; retrying
      // that same cursor must recover without editing either document.
      const versions = [entry.version, dependency.version];
      await fs.writeFile(unavailable, "fail the next completion request");
      await eventually(async () => {
        const failed = await completions(entry, "probe_waiting", "probe_waiting", "probe_");
        assert.equal(failed.items.some(compilerItem), false,
          "The failing query must not return a cached or speculative compiler candidate.");
        // A source/configuration invalidation can cancel before process start.
        // Retry only that absence; once the fake consumes the flag, the failed
        // process has actually run and the next same-cursor request must recover.
        try { await fs.stat(unavailable); return false; }
        catch (error) { if (error.code === "ENOENT") return true; throw error; }
      }, "unavailable completion query actually executes");
      const retried = await checkedCompletions(entry, "probe_waiting", "probe_waiting", "probe_");
      expectCandidate(retried, "probe_current", "variable: i32");
      assert.deepEqual([entry.version, dependency.version], versions,
        "Operational completion failure must be retried without changing any source.");
      await replace(entry, source("WAIT_PRIMARY"));
      const stalePrimary = completions(entry, "probe_waiting", "probe_waiting", "probe_");
      const primaryRequest = await waitForRequest("WAIT_PRIMARY");
      await replace(entry, source("CURRENT"));
      const primaryResult = await stalePrimary;
      assert.equal(primaryResult.items.some(item => label(item) === "probe_only_stale"), false);
      await exited(primaryRequest.pid);
      await replace(dependency, "# WAIT_DEPENDENCY\n");
      const staleDependency = completions(entry, "probe_waiting", "probe_waiting", "probe_");
      const dependencyRequest = await waitForRequest("WAIT_DEPENDENCY");
      await replace(dependency, "# CURRENT\n");
      const dependencyResult = await staleDependency;
      assert.equal(dependencyResult.items.some(item => label(item) === "probe_only_stale"), false);
      await exited(dependencyRequest.pid);
      const repaired = await checkedCompletions(entry, "probe_waiting", "probe_waiting", "probe_");
      expectNames(repaired, ["probe_current"]);
      expectCandidate(repaired, "probe_current", "variable: i32");
      return { unavailableRetried: true, cancelled: 2, recovered: true };
    } finally {
      await configuration.update("compiler.path", originalCompiler, vscode.ConfigurationTarget.Workspace);
    }
  });
};
