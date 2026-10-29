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

test("Matrix explicit consider blocks highlight contextually and support folding", async () => {
  const grammar = await loadMatrixGrammar();
  for (const line of ["when Result.Ok holding found then begin block", "END BLOCK", "otherwise BeGiN BlOcK"]) {
    const fragment = line.match(/block/i)![0];
    assertScope(grammar, line, fragment, "keyword.control.block");
  }
  for (const name of ["block", "block-value", "begin-block", "end-block", "`block`"])
    assertScope(grammar, "say " + name, name === "`block`" ? "block" : name, "variable.other.readwrite");
  for (const line of ['say "begin block"', "!!! end block", "say item.block", "say begin-block"]) {
    const result = grammar.tokenizeLine(line, INITIAL);
    assert.equal(result.tokens.some(token => token.scopes.includes("keyword.control.block.matrix")), false, line);
  }
  let state = INITIAL;
  for (const line of ["consider outcome", "when Result.Ok holding found then begin block",
    "when found then", "say \"end block\"", "end when", "end block", "when Result.Error holding failure then say failure", "end consider"])
    state = grammar.tokenizeLine(line, state).ruleStack;
  const after = grammar.tokenizeLine("say block", state);
  assert.ok(scopesAt(after, "say block", "block").includes("variable.other.readwrite.matrix"));

  const config = JSON.parse(fs.readFileSync("language-configuration.matrix.json", "utf8")) as {
    indentationRules: { increaseIndentPattern: string; decreaseIndentPattern: string };
    folding: { markers: { start: string; end: string } };
  };
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  const foldStart = new RegExp(config.folding.markers.start);
  const foldEnd = new RegExp(config.folding.markers.end);
  for (const line of ["when Result.Ok holding found then begin block", "otherwise BEGIN BLOCK !!! fallback"]) {
    assert.ok(increase.test(line), line);
    assert.ok(foldStart.test(line), line);
  }
  assert.ok(decrease.test("    end block"));
  assert.ok(foldEnd.test("    END BLOCK"));
  for (const line of ['say "begin block"', "value begin-block is 1", "when yes then begin block say 1 end block"]) {
    assert.equal(increase.test(line), false, line);
    assert.equal(foldStart.test(line), false, line);
  }
});

test("Matrix prose patterns fold keywords, preserve capture names and nest safely", async () => {
  const grammar = await loadMatrixGrammar();
  const opened = grammar.tokenizeLine("price-pattern BEGIN PATTERN", INITIAL);
  assert.ok(scopesAt(opened, "price-pattern BEGIN PATTERN", "price-pattern").includes("entity.name.pattern.matrix"));
  let state = opened.ruleStack;
  const line = "CAPTURE amount-value MATCHING one or more ascii digits";
  const capture = grammar.tokenizeLine(line, state);
  assert.ok(scopesAt(capture, line, "amount-value").includes("entity.name.capture.matrix"));
  assert.ok(scopesAt(capture, line, "MATCHING").includes("keyword.control.pattern.matrix"));
  state = grammar.tokenizeLine("require next not to match begin pattern", capture.ruleStack).ruleStack;
  const literal = grammar.tokenizeLine('literal "{amount-value} end pattern"', state);
  assert.ok(literal.tokens.some(token => token.scopes.includes("string.quoted.double.pattern.matrix")));
  assert.equal(literal.tokens.some(token => token.scopes.includes("meta.interpolation.matrix")), false);
  state = grammar.tokenizeLine("END PATTERN", literal.ruleStack).ruleStack;
  const outer = grammar.tokenizeLine("optional literal \" USD\"", state);
  assert.ok(outer.tokens.some(token => token.scopes.includes("keyword.control.pattern.matrix")));
  state = grammar.tokenizeLine("end pattern", outer.ruleStack).ruleStack;
  const ordinary = grammar.tokenizeLine("value pattern is capture-value", state);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("meta.pattern.matrix")), false);
  for (const name of ["pattern", "capture", "capture-value", "begin-pattern", "collect-query", "take-value"]) {
    const result = grammar.tokenizeLine("say " + name, INITIAL);
    assert.ok(scopesAt(result, "say " + name, name).includes("variable.other.readwrite.matrix"));
    assert.equal(scopesAt(result, "say " + name, name).some(scope => /keyword\.(control\.)?(pattern|query)/.test(scope)), false);
  }
});

test("Matrix collection query scopes stay inside their block", async () => {
  const grammar = await loadMatrixGrammar();
  let state = grammar.tokenizeLine("value chosen is COLLECT BEGIN QUERY", INITIAL).ruleStack;
  for (const line of ["from each item in items", "where item is greater than 0", "order by item ascending", "skip 1", "take 2", "give item plus 1"]) {
    const result = grammar.tokenizeLine(line, state);
    assert.ok(result.tokens.some(token => token.scopes.includes("keyword.control.query.matrix")), line);
    state = result.ruleStack;
  }
  state = grammar.tokenizeLine("END QUERY", state).ruleStack;
  const ordinary = grammar.tokenizeLine("value take is where-value", state);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("meta.query.matrix")), false);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("keyword.control.query.matrix")), false);
  const snippets = JSON.parse(fs.readFileSync("snippets/matrix.code-snippets", "utf8")) as Record<string, { prefix: string; body: string[] }>;
  assert.equal(snippets["Text pattern"]!.prefix, "pattern");
  assert.equal(snippets["Collect query"]!.prefix, "query");
  assert.equal(snippets["Text pattern"]!.body.at(-1), "end pattern");
  assert.equal(snippets["Collect query"]!.body.at(-1), "end query");
});

test("Matrix Boolean aliases and dynamic types respect lexical context", async () => {
  const grammar = await loadMatrixGrammar();
  for (const type of ["boolean", "flag", "anything", "FLAG", "ANYTHING"])
    assertScope(grammar, `value enabled as ${type} is yes`, type, "storage.type.builtin");
  for (const literal of ["true", "false", "yes", "no", "YES", "NO"]) {
    assertScope(grammar, `value enabled is ${literal}`, literal, "constant.language.boolean");
    assertScope(grammar, `@tag(enabled=${literal})`, literal, "constant.language.boolean");
  }
  assertScope(grammar, "value flag is yes", "flag", "variable.other.readwrite");
  assertScope(grammar, "say flag", "flag", "variable.other.readwrite");
  assertScope(grammar, "value result as flag.Type is input", "flag.Type", "entity.name.type");
  for (const name of ["yes-value", "no_value", "anything-value"])
    assertScope(grammar, "say " + name, name, "variable.other.readwrite");
});

test("Matrix query keywords match the compiler's projection and ordering vocabulary", async () => {
  const grammar = await loadMatrixGrammar();
  const state = grammar.tokenizeLine("collect begin query", INITIAL).ruleStack;
  for (const word of ["give", "GIVE", "ascending", "ASCENDING", "descending", "DESCENDING"]) {
    const result = grammar.tokenizeLine(word, state);
    assert.ok(scopesAt(result, word, word).includes("keyword.control.query.matrix"), word);
  }
  for (const word of ["select", "SELECT", "asc", "ASC", "desc", "DESC", "ascending-value", "descending_value"]) {
    const result = grammar.tokenizeLine(word, state);
    assert.equal(scopesAt(result, word, word).includes("keyword.control.query.matrix"), false, word);
  }
});

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

test("Matrix switch forms highlight and indent while retaining member names", async () => {
  const grammar = await loadMatrixGrammar();
  for (const [line, word] of [['SWITCH mode', 'SWITCH'], ['case 1 then', 'case'],
    ['stop switching with 2', 'switching'], ['end switch', 'switch']])
    assertScope(grammar, line!, word!, 'keyword.control.verb');
  for (const word of ['switch', 'default', 'switch-value'])
    assertScope(grammar, 'settings.' + word, word, 'variable.other.member');
  const config = JSON.parse(fs.readFileSync('language-configuration.matrix.json', 'utf8'));
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  for (const line of ['switch mode', 'CASE 1 THEN', 'otherwise']) assert.ok(increase.test(line), line);
  for (const line of ['case 2 then', 'otherwise', 'END SWITCH']) assert.ok(decrease.test(line), line);
  for (const line of ['say "switch mode"', '!!! switch mode', 'say switch-value']) assert.equal(increase.test(line), false, line);
  const snippets = JSON.parse(fs.readFileSync('snippets/matrix.code-snippets', 'utf8'));
  assert.equal(snippets['Switch statement'].body.at(-1), 'end switch');
  assert.ok(snippets['Switch expression'].body.some((line: string) => line.includes('stop switching with')));
});

test("Matrix annotation regions scope names and properties without consuming declarations", async () => {
  const grammar = await loadMatrixGrammar();
  for (const name of ["doc", "Company.review", "Company::review", "custom-note"]) {
    assertScope(grammar, `@${name}(enabled=true, level=-1.5)`, name, "storage.type.annotation");
    assertScope(grammar, `note ${name} with enabled being true`, name, "storage.type.annotation");
  }
  assertScope(grammar, '@doc(summary="kept")', "summary", "variable.parameter.annotation");
  assertScope(grammar, 'NOTE doc WITH summary BEING "kept"', "summary", "variable.parameter.annotation");
  assertScope(grammar, '@custom(mode=Policy.Safe)', "Policy.Safe", "constant.other.symbol.annotation");
  assertScope(grammar, 'context "kept"', "context", "storage.type.annotation");
  let state = grammar.tokenizeLine('@doc(', INITIAL).ruleStack;
  const line = '    summary="a literal ) and @inline",';
  const result = grammar.tokenizeLine(line, state);
  assert.ok(scopesAt(result, line, "summary").includes("variable.parameter.annotation.matrix"));
  assert.ok(scopesAt(result, line, "@inline").includes("string.quoted.double.matrix"));
  state = grammar.tokenizeLine('    enabled=true)', result.ruleStack).ruleStack;
  const declaration = grammar.tokenizeLine('main begin paragraph', state);
  assert.equal(declaration.tokens.some(token => token.scopes.includes("meta.annotation.matrix")), false);
  assert.ok(scopesAt(declaration, 'main begin paragraph', 'main').includes('entity.name.function.matrix'));
  // Backslashes remain literal in Matrix annotation text, including before a closing quote.
  const raw = '@custom(path="C:\\", enabled=true)';
  assertScope(grammar, raw, "enabled", "variable.parameter.annotation");
  for (const name of ["note-value", "context-value"]) {
    assertScope(grammar, 'say ' + name, name, 'variable.other.readwrite');
  }
});

test("Matrix preprocessor directives remain distinct from annotation metadata", async () => {
  const grammar = await loadMatrixGrammar();
  for (const name of ["pragma", "include", "define", "undef", "ifdef", "ifndef", "if", "elif", "else", "endif", "error", "warning", "message", "schema"])
    assertScope(grammar, '@' + name + ' FEATURE', '@' + name, 'keyword.control.directive');
  assertScope(grammar, '@custom(enabled=true)', 'custom', 'storage.type.annotation');
  assertScope(grammar, '@define-note(enabled=true)', 'define-note', 'storage.type.annotation');
  assertScope(grammar, '@include.Review(enabled=true)', 'include.Review', 'storage.type.annotation');
  const state = grammar.tokenizeLine('value document is begin text', INITIAL).ruleStack;
  const literal = grammar.tokenizeLine('@include "unchanged.matrix"', state);
  assert.equal(literal.tokens.some(token => token.scopes.includes('keyword.control.directive.matrix')), false);
  assertScope(grammar, '!!! @define FEATURE true', '!!! @define FEATURE true', 'comment.line.exclamation');
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

test("Matrix mutable parameter modifiers preserve name scopes in every header position", async () => {
  const grammar = await loadMatrixGrammar();
  for (const prefix of ["receives mutable", "receives more mutable", "receives more variable",
    "receives first as number and mutable", "receives first as number, variable",
    "receives first as number and more mutable", "receives first as number, more variable"]) {
    const line = `sample ${prefix} input-value as number returns number begin paragraph`;
    assertScope(grammar, line, "input-value", "variable.parameter");
    if (prefix.includes("mutable")) assertScope(grammar, line, "mutable", "storage.modifier");
  }
  for (const line of ["    mutable input-value as number", "    more mutable input-value as number",
    "    MUTABLE input-value AS NUMBER"]) {
    assertScope(grammar, line, "input-value", "variable.parameter");
    assertScope(grammar, line, line.includes("MUTABLE") ? "MUTABLE" : "mutable", "storage.modifier");
  }
  assertScope(grammar, "variable input-value as number is 0", "input-value", "variable.other.readwrite");
});

test("Matrix binding references scope parameters separately from reference expressions and types", async () => {
  const grammar = await loadMatrixGrammar();
  for (const prefix of ["receives reference", "receives first as number and reference",
    "receives first as number, reference", ""]) {
    const line = prefix ? `update ${prefix} amount as number begin paragraph` : "    reference amount as number";
    assertScope(grammar, line, "reference", "storage.modifier");
    assertScope(grammar, line, "amount", "variable.parameter");
  }
  assertScope(grammar, "update with amount being reference to total", "reference", "storage.type.builtin");
  assertScope(grammar, "item as reference to Record", "reference", "storage.type.builtin");
});

test("Matrix short mut/ref aliases preserve parameter and reference scopes", async () => {
  const grammar = await loadMatrixGrammar();
  for (const prefix of ["receives mut", "receives more mut", "receives first as number and mut",
    "receives ref", "receives first as number, ref"]) {
    const line = `update ${prefix} amount as number begin paragraph`;
    assertScope(grammar, line, "amount", "variable.parameter");
    assertScope(grammar, line, prefix.includes("mut") ? "mut" : "ref", "storage.modifier");
  }
  assertScope(grammar, "    MUT amount as number", "MUT", "storage.modifier");
  assertScope(grammar, "    REF amount as number", "amount", "variable.parameter");
  assertScope(grammar, "update with amount being ref to total", "ref", "storage.type.builtin");
  assertScope(grammar, "item as ref to Record", "ref", "storage.type.builtin");
  for (const word of ["mut", "mutable"])
    assertScope(grammar, `change on ${word} receiver begin paragraph`, word, "keyword.other.modifier");
  for (const name of ["mut-value", "ref-value", "mutable-value", "reference-value"])
    assertScope(grammar, "say " + name, name, "variable.other.readwrite");
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

test("Matrix inline expectations highlight the statement without reserving names", async () => {
  const grammar = await loadMatrixGrammar();
  assertScope(grammar, "    expect total equals 42", "expect", "keyword.control.verb");
  assertScope(grammar, "EXPECT yes", "EXPECT", "keyword.control.verb");
  assertScope(grammar, "value expect-value is 42", "expect-value", "variable.other.readwrite");
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
  assert.deepEqual("-1.6 -1.6e-3 -0x2A frame-kind".match(words), ["-1.6", "-1.6e-3", "-0x2A", "frame-kind"]);
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  for (const line of ["MAIN RETURNS NUMBER BEGIN PARAGRAPH", "when ready then !!! check", "value banner is begin text", "attempt", "repeat for step from 0 to 3", "otherwise", "caught anything as problem"]) assert.ok(increase.test(line), line);
  for (const line of ['say "begin paragraph"', '!!! begin paragraph', "main returns number begin paragraph give 0 end paragraph", "value again is apply worker across items", "say begin-paragraph", "say end-text"]) assert.equal(increase.test(line), false, line);
  assert.ok(decrease.test("END PARAGRAPH"));
  assert.equal(decrease.test("end-paragraph"), false);
});

test("Matrix signed numbers, tuple types, and formatted expressions have distinct scopes", async () => {
  const grammar = await loadMatrixGrammar();
  assertScope(grammar, "value offset is negative 1.6", "negative", "keyword.operator.word");
  assertScope(grammar, "value point as tuple of (floating, text) is (-1.6, \"ready\")", "tuple", "storage.type.builtin");
  assertScope(grammar, "say -1.6e-3", "-1.6e-3", "constant.numeric.decimal");
  assertScope(grammar, "say -0x2A", "-0x2A", "constant.numeric.hex");
  const line = 'say FORMATTED "{{raw}} {negative height plus -1.6} {describe with input being "text"}"';
  assertScope(grammar, line, "FORMATTED", "keyword.other.formatted");
  assertScope(grammar, line, "{{", "constant.character.escape.brace");
  assertScope(grammar, line, "negative", "keyword.operator.word");
  assertScope(grammar, line, "height", "variable.other.readwrite");
  assertScope(grammar, line, "-1.6", "constant.numeric.decimal");
  assertScope(grammar, line, "describe", "entity.name.function.call");
  assertScope(grammar, line, "text", "string.quoted.double");
  assert.ok(scopesAt(grammar.tokenizeLine(line, INITIAL), line, "height").includes("meta.embedded.expression.matrix"));
  for (const ordinary of ['say "{negative height}"', 'say `formatted`', 'say formatted-value', 'say formatted']) {
    assert.equal(grammar.tokenizeLine(ordinary, INITIAL).tokens.some(token =>
      token.scopes.includes("meta.interpolation.matrix")), false, ordinary);
  }
  const closed = grammar.tokenizeLine('say formatted "{height}"', INITIAL);
  assertScope(grammar, "value ordinary is 1", "value", "storage.type.variable");
  assert.equal(grammar.tokenizeLine("value ordinary is 1", closed.ruleStack).tokens.some(token =>
    token.scopes.includes("string.quoted.double.interpolated.matrix")), false);
  const continued = grammar.tokenizeLine('say formatted "height: {', INITIAL);
  const expression = grammar.tokenizeLine('negative height', continued.ruleStack);
  assert.ok(scopesAt(expression, 'negative height', 'negative').includes("keyword.operator.word.matrix"));
  assert.ok(scopesAt(expression, 'negative height', 'height').includes("meta.embedded.expression.matrix"));
  const finished = grammar.tokenizeLine('}"', expression.ruleStack);
  assert.equal(grammar.tokenizeLine('say height', finished.ruleStack).tokens.some(token =>
    token.scopes.includes("meta.interpolation.matrix")), false);
});

test("Matrix snippets use current paragraph, type, text, and extension spellings", () => {
  const snippets = JSON.parse(fs.readFileSync("snippets/matrix.code-snippets", "utf8")) as Record<string, { prefix: string; body: string[] }>;
  const prefixes = new Set(Object.values(snippets).map(s => s.prefix));
  for (const prefix of ["paragraph", "main", "extension", "begin-text", "means", "call", "override", "extend-text", "tuple", "unpack", "formatted", "@annotation", "note", "@doc", "note-doc", "note-inline", "context", "@include", "@ifdef"]) assert.ok(prefixes.has(prefix), prefix);
  assert.match(snippets["Contract"]!.body.join("\n"), /returns \$\{3:text\}/);
  assert.match(snippets["Paragraph"]!.body.join("\n"), /give \$\{2:argument\}/);
  assert.match(snippets["Variable binding"]!.body.join("\n"), /is \$\{3:0\}/);
  assert.deepEqual(snippets["Raw text block"]!.body, ["begin text", "    ${0:text}", "end text"]);
  assert.deepEqual(snippets["Consider block"]!.body, ["consider ${1:subject}",
    "    when ${2:pattern} then begin block", "        ${0}", "    end block", "end consider"]);
  assert.deepEqual(snippets["Formatted text"]!.body, ['formatted "${1:Height}: {${2:height}}"']);
  assert.deepEqual(snippets["Metadata annotation"]!.body, ['@${1:custom}(${2:key}=${3:true})']);
  assert.deepEqual(snippets["Prose metadata annotation"]!.body, ['note ${1:custom} with ${2:key} being ${3:true}']);
});
