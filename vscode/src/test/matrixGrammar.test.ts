import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import test from "node:test";
import { loadWASM, OnigScanner, OnigString } from "vscode-oniguruma";
import { IGrammar, INITIAL, ITokenizeLineResult, parseRawGrammar, Registry } from "vscode-textmate";

let grammarPromise: Promise<IGrammar> | undefined;
function loadMatrixGrammar(): Promise<IGrammar> {
  grammarPromise ??= (async () => {
    const wasm = fs.readFileSync(require.resolve("vscode-oniguruma/release/onig.wasm"));
    await loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const grammarPath = path.join(process.cwd(), "syntaxes", "matrix.tmLanguage.json");
    const registry = new Registry({
      onigLib: Promise.resolve({
        createOnigScanner: sources => new OnigScanner(sources),
        createOnigString: value => new OnigString(value)
      }),
      loadGrammar: async scope => scope === "source.matrix"
        ? parseRawGrammar(fs.readFileSync(grammarPath, "utf8"), grammarPath) : null
    });
    const grammar = await registry.loadGrammar("source.matrix");
    assert.ok(grammar);
    return grammar;
  })();
  return grammarPromise;
}

function scopesAt(result: ITokenizeLineResult, line: string, fragment: string): string[] {
  const start = line.indexOf(fragment);
  assert.notEqual(start, -1, `missing ${fragment} in ${line}`);
  const token = result.tokens.find(t => t.startIndex <= start && t.endIndex >= start + fragment.length);
  assert.ok(token, `no single token spans ${fragment} in ${line}`);
  return token.scopes;
}
function assertScope(grammar: IGrammar, line: string, fragment: string, scope: string): void {
  assert.ok(scopesAt(grammar.tokenizeLine(line, INITIAL), line, fragment).includes(scope + ".matrix"),
    `${fragment} should have ${scope} in ${line}`);
}

test("Matrix scopes subject-first declarations, modifiers, parameters, and type aliases", async () => {
  const grammar = await loadMatrixGrammar();
  const declaration = "PUBLIC replaces read-sensor receives variable sensor-id as number returns sensor-reading begin paragraph";
  for (const [fragment, scope] of [
    ["PUBLIC", "storage.modifier"], ["replaces", "storage.modifier"],
    ["read-sensor", "entity.name.function"], ["sensor-id", "variable.parameter"],
    ["number", "storage.type.builtin"], ["sensor-reading", "entity.name.type"]
  ] as const) assertScope(grammar, declaration, fragment, scope);
  assertScope(grammar, "microvolts means override long", "microvolts", "entity.name.type");
  assertScope(grammar, "envelope of type T begin record", "envelope", "entity.name.type");
  assertScope(grammar, "envelope of type T begin record", "T", "entity.name.type.parameter");
  assertScope(grammar, "default-width of type T meeting sizable returns override integer begin paragraph", "default-width", "entity.name.function");
  assertScope(grammar, "byte-width without receiver returns number", "byte-width", "entity.name.function");
  assertScope(grammar, "text begin extension", "text", "storage.type.builtin");
  assertScope(grammar, "field raw-value as sensor-id", "raw-value", "variable.other.member");
  assertScope(grammar, "field raw-value as sensor-id", "sensor-id", "entity.name.type");
  for (const line of ["end paragraph", "END RECORD", "end extension"]) {
    assert.equal(grammar.tokenizeLine(line, INITIAL).tokens.some(t => t.scopes.some(s => s.startsWith("entity.name"))), false);
  }
});

test("Matrix folds ASCII keywords while preserving complete hyphenated and escaped names", async () => {
  const grammar = await loadMatrixGrammar();
  assertScope(grammar, "VALUE total AS NUMBER IS first.raw-value PLUS 2", "VALUE", "storage.type.variable");
  assertScope(grammar, "VALUE total AS NUMBER IS first.raw-value PLUS 2", "NUMBER", "storage.type.builtin");
  for (const name of ["read-file", "is-plausible", "begin-text", "text-value", "shifted-left", "re-ad", "a_s", "read_file", "chapter-name", "café-text", "ſay", "claſſ"]) {
    const line = "say " + name;
    const scopes = scopesAt(grammar.tokenizeLine(line, INITIAL), line, name);
    assert.ok(scopes.includes("variable.other.readwrite.matrix"), name);
    assert.equal(scopes.some(s => /^(keyword|storage|constant)\./.test(s)), false, name);
  }
  assertScope(grammar, "value `text` as text is \"raw\"", "`text`", "variable.other.readwrite");
  const escaped = grammar.tokenizeLine("say `begin text`", INITIAL);
  assert.equal(escaped.tokens.some(t => t.scopes.some(s => s.startsWith("keyword"))), true); // say only
  assert.equal(escaped.tokens.filter(t => t.scopes.some(s => s.startsWith("keyword"))).length, 1);
});

test("Matrix scopes imports, named calls, and precise type overrides", async () => {
  const grammar = await loadMatrixGrammar();
  assertScope(grammar, "read everything from ny.lang.text", "ny.lang.text", "entity.name.namespace");
  assertScope(grammar, "READ ny.lang.math AS math", "AS", "keyword.control.import");
  assertScope(grammar, "say reading.is-plausible with nothing", "is-plausible", "entity.name.function.call");
  assertScope(grammar, "value result is READ-SENSOR with SENSOR-ID being 2", "READ-SENSOR", "entity.name.function.call");
  assertScope(grammar, "value result is READ-SENSOR with SENSOR-ID being 2", "SENSOR-ID", "variable.parameter.argument");
  assertScope(grammar, "value data as owned array of override byte is []", "byte", "storage.type.builtin");
  assertScope(grammar, "value count as override long is 0xFFFFFFFF", "override", "storage.modifier");
  assertScope(grammar, "value count as override long is 0xFFFFFFFF", "0xFFFFFFFF", "constant.numeric.hex");
  assertScope(grammar, "value invalid as override short is 1", "short", "invalid.illegal.unsupported-type");
  assertScope(grammar, "give word SHIFTED LEFT BY 8", "SHIFTED LEFT BY", "keyword.operator.word");
  assertScope(grammar, "when total IS AT LEAST 1 then", "IS AT LEAST", "keyword.operator.word");
});

test("Matrix scopes signed exponents within complete numeric literals", async () => {
  const grammar = await loadMatrixGrammar();
  for (const literal of ["1e3", "1.5E-3", "2E+2", "5e-324", "0e99999999999999999999"]) {
    assertScope(grammar, `say ${literal}.`, literal, "constant.numeric.decimal");
  }
  assertScope(grammar, "say 0x1E3", "0x1E3", "constant.numeric.hex");
  assertScope(grammar, "say value-e3", "value-e3", "variable.other.readwrite");
});

test("Matrix raw quotes and backticks never leak across lines or invent escapes", async () => {
  const grammar = await loadMatrixGrammar();
  const line = 'value path is "C:\\dir\\file" plus quote';
  assertScope(grammar, line, "C:\\dir\\file", "string.quoted.double");
  assertScope(grammar, line, "plus", "keyword.operator.word");
  const adjacent = grammar.tokenizeLine('say "one""two"', INITIAL);
  assert.equal(adjacent.tokens.filter(t => t.scopes.includes("punctuation.definition.string.begin.matrix")).length, 2);
  for (const broken of ['say "unterminated', 'say `unterminated']) {
    const first = grammar.tokenizeLine(broken, INITIAL);
    const next = grammar.tokenizeLine("give 0", first.ruleStack);
    assert.ok(scopesAt(next, "give 0", "give").includes("keyword.control.verb.matrix"));
    assert.equal(next.tokens.some(t => t.scopes.some(s => s.startsWith("string."))), false);
  }
  const literalBackslash = 'say "ends with \\" plus newline';
  assertScope(grammar, literalBackslash, "plus", "keyword.operator.word");
});

test("Matrix text blocks preserve raw content and end only at a standalone terminator", async () => {
  const grammar = await loadMatrixGrammar();
  let state = grammar.tokenizeLine("value banner as text is BeGiN TeXt", INITIAL).ruleStack;
  for (const line of ['!!! a comment marker is raw text', '"quotes" and \\slashes are literal', 'end text !!! still content', 'end text-tag', 'begin text']) {
    const result = grammar.tokenizeLine(line, state);
    assert.ok(result.tokens.every(t => t.scopes.includes("string.quoted.raw.matrix")), line);
    assert.equal(result.tokens.some(t => t.scopes.some(s => s.startsWith("comment."))), false);
    state = result.ruleStack;
  }
  state = grammar.tokenizeLine("  END\t TEXT  ", state).ruleStack;
  const next = grammar.tokenizeLine("say banner", state);
  assert.ok(scopesAt(next, "say banner", "say").includes("keyword.control.verb.matrix"));
  // A bare comment line is not a block delimiter.
  state = grammar.tokenizeLine("!!!", INITIAL).ruleStack;
  const afterComment = grammar.tokenizeLine("say banner", state);
  assert.ok(scopesAt(afterComment, "say banner", "say").includes("keyword.control.verb.matrix"));
  // A comment after the opener makes it ordinary source, matching the lexer.
  state = grammar.tokenizeLine("value banner is begin text !!! no block", INITIAL).ruleStack;
  assert.equal(grammar.tokenizeLine("give 0", state).tokens.some(t => t.scopes.includes("string.quoted.raw.matrix")), false);
});

test("Matrix continued clauses keep parameter and argument scopes without persistent declaration state", async () => {
  const grammar = await loadMatrixGrammar();
  let state = INITIAL;
  const lines = ["read-sensor receives sensor-id as number,", "    channel as number", "    returns array of override byte begin paragraph", "    give fetch with sensor-id being sensor-id,", "        channel being channel", "end paragraph", "say 0"];
  const results = lines.map(line => { const result = grammar.tokenizeLine(line, state); state = result.ruleStack; return result; });
  assert.ok(scopesAt(results[1]!, lines[1]!, "channel").includes("variable.parameter.matrix"));
  assert.ok(scopesAt(results[2]!, lines[2]!, "byte").includes("storage.type.builtin.matrix"));
  assert.ok(scopesAt(results[4]!, lines[4]!, "channel").includes("variable.parameter.argument.matrix"));
  assert.ok(scopesAt(results[6]!, lines[6]!, "say").includes("keyword.control.verb.matrix"));
});

test("Matrix representative source corpus tokenizes across lines without leaking string or Ny scopes", async () => {
  const grammar = await loadMatrixGrammar();
  const samples = path.resolve(process.cwd(), "../matrix/samples");
  for (const file of ["15-telemetry-relay.matrix", "16-names-and-case.matrix", "17-types-and-text.matrix", "18-extensions.matrix"]) {
    let state = INITIAL;
    let rawText = false;
    for (const line of fs.readFileSync(path.join(samples, file), "utf8").split(/\r?\n/)) {
      const result = grammar.tokenizeLine(line, state);
      for (const token of result.tokens) assert.equal(token.scopes.some(s => s.endsWith(".ny")), false, file);
      if (!rawText && /^\s*!!!/.test(line)) {
        assert.ok(result.tokens.some(t => t.scopes.includes("comment.line.exclamation.matrix")), `${file}: ${line}`);
      }
      if (/^\s*end\s+text\s*$/i.test(line)) rawText = false;
      else if (!/^\s*!!!/.test(line) && /\bbegin\s+text\s*$/i.test(line)) rawText = true;
      if (!rawText && /^\s*(?:give|say)\s/.test(line)) {
        assert.equal(result.tokens.some(t => t.scopes.includes("string.quoted.raw.matrix")), false, `${file}: ${line}`);
      }
      state = result.ruleStack;
    }
    const after = grammar.tokenizeLine("say 0", state);
    assert.ok(scopesAt(after, "say 0", "say").includes("keyword.control.verb.matrix"), file);
  }
});

test("Matrix editor word selection and block indentation match its lexical spelling", () => {
  const config = JSON.parse(fs.readFileSync("language-configuration.matrix.json", "utf8")) as {
    wordPattern: string; indentationRules: { increaseIndentPattern: string; decreaseIndentPattern: string };
  };
  const words = new RegExp(config.wordPattern, "g");
  assert.deepEqual("read-regular-bytes FRAME_KIND café-value 0xFF 3.14".match(words), ["read-regular-bytes", "FRAME_KIND", "café-value", "0xFF", "3.14"]);
  assert.deepEqual("1e3 1.5E-3 2E+2 0x1E3".match(words), ["1e3", "1.5E-3", "2E+2", "0x1E3"]);
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  for (const line of ["MAIN RETURNS NUMBER BEGIN PARAGRAPH", "when ready then !!! check", "value banner is begin text", "attempt", "repeat for step from 0 to 3", "otherwise", "caught anything as problem"]) assert.ok(increase.test(line), line);
  for (const line of ['say "begin paragraph"', '!!! begin paragraph', "main returns number begin paragraph give 0 end paragraph", "value again is apply worker across items", "say begin-paragraph", "say end-text"]) assert.equal(increase.test(line), false, line);
  assert.ok(decrease.test("END PARAGRAPH"));
  assert.equal(decrease.test("end-paragraph"), false);
});

test("Matrix snippets use current paragraph, type, text, and extension spellings", () => {
  const snippets = JSON.parse(fs.readFileSync("snippets/matrix.code-snippets", "utf8")) as Record<string, { prefix: string; body: string[] }>;
  const prefixes = new Set(Object.values(snippets).map(s => s.prefix));
  for (const prefix of ["paragraph", "main", "extension", "begin-text", "means", "call", "override", "extend-text"]) assert.ok(prefixes.has(prefix), prefix);
  assert.match(snippets["Contract"]!.body.join("\n"), /returns \$\{3:text\}/);
  assert.match(snippets["Paragraph"]!.body.join("\n"), /give \$\{2:argument\}/);
  assert.match(snippets["Variable binding"]!.body.join("\n"), /is \$\{3:0\}/);
  assert.deepEqual(snippets["Raw text block"]!.body, ["begin text", "    ${0:text}", "end text"]);
});
