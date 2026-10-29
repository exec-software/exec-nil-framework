import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import test from "node:test";
import { loadWASM, OnigScanner, OnigString } from "vscode-oniguruma";
import { IGrammar, INITIAL, parseRawGrammar, Registry } from "vscode-textmate";

let grammarPromise: Promise<IGrammar | null> | undefined;

function loadNyGrammar(): Promise<IGrammar | null> {
  if (grammarPromise) {
    return grammarPromise;
  }
  grammarPromise = (async () => {
    const wasmPath = require.resolve("vscode-oniguruma/release/onig.wasm");
    const wasm = fs.readFileSync(wasmPath);
    await loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const grammarPath = path.join(process.cwd(), "syntaxes", "ny.tmLanguage.json");
    const registry = new Registry({
      onigLib: Promise.resolve({
        createOnigScanner: (sources) => new OnigScanner(sources),
        createOnigString: (value) => new OnigString(value)
      }),
      loadGrammar: async (scopeName) => {
        if (scopeName !== "source.ny") {
          return null;
        }
        return parseRawGrammar(fs.readFileSync(grammarPath, "utf8"), grammarPath);
      }
    });
    return await registry.loadGrammar("source.ny");
  })();
  return grammarPromise;
}

function scopesForFragment(grammar: IGrammar, line: string, fragment: string): string[] {
  const start = line.indexOf(fragment);
  assert.notEqual(start, -1, `fragment ${fragment} not found`);
  const result = grammar.tokenizeLine(line, INITIAL);
  const token = result.tokens.find((candidate) => {
    return candidate.startIndex <= start && candidate.endIndex >= start + fragment.length;
  });
  assert.ok(token, `no token spans ${fragment}`);
  return token.scopes;
}

test("grammar compiles and scopes canonical declarations", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  assert.ok(scopesForFragment(grammar, "pub fn greet(name: text) -> text", "fn").includes("storage.type.function.ny"));
  assert.ok(scopesForFragment(grammar, "pub fn greet(name: text) -> text", "greet").includes("entity.name.function.ny"));
  assert.ok(scopesForFragment(grammar, "pub fn greet(name: text) -> text", "text").includes("storage.type.builtin.ny"));
  assert.ok(scopesForFragment(grammar, "type UserId = i64", "type").includes("storage.type.declaration.ny"));
  assert.ok(scopesForFragment(grammar, "type UserId = i64", "UserId").includes("entity.name.type.ny"));
});

test("Ny short and long mutability/reference aliases retain keyword and binding scopes", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  for (const word of ["mut", "mutable"]) {
    assert.ok(scopesForFragment(grammar, `fn update(${word} amount: i64)`, word).includes("storage.modifier.ny"));
    assert.ok(scopesForFragment(grammar, `let ${word} amount: i64 = 0`, word).includes("storage.modifier.mutable.ny"));
    assert.ok(scopesForFragment(grammar, `let ${word} amount: i64 = 0`, "amount").includes("variable.other.readwrite.ny"));
  }
  for (const word of ["ref", "reference"]) {
    assert.ok(scopesForFragment(grammar, `fn update(${word} amount: i64)`, word).includes("keyword.operator.word.ny"));
    assert.ok(scopesForFragment(grammar, `update(${word} amount)`, word).includes("keyword.operator.word.ny"));
    assert.ok(scopesForFragment(grammar, `fn read(item: ${word}<Point>)`, word).includes("keyword.operator.word.ny"));
  }
});

test("Ny switch statements and value exits are highlighted without claiming members", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  for (const [line, word] of [['switch mode {', 'switch'], ['case 1: break 2', 'case'],
    ['default: break 0', 'default'], ['break 3', 'break'], ['end switch', 'switch']])
    assert.ok(scopesForFragment(grammar, line!, word!).includes('keyword.control.ny'));
  for (const word of ['switch', 'default'])
    assert.ok(scopesForFragment(grammar, 'settings.' + word, word).includes('variable.other.member.ny'));
  const config = JSON.parse(fs.readFileSync('language-configuration.json', 'utf8'));
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  for (const line of ['switch value', 'case 1:', 'default:']) assert.ok(increase.test(line), line);
  for (const line of ['case 2:', 'default:', 'end switch']) assert.ok(decrease.test(line), line);
  const snippets = JSON.parse(fs.readFileSync('snippets/ny.code-snippets', 'utf8'));
  assert.equal(snippets['Switch statement'].body.at(-1), 'end switch');
  assert.ok(snippets['Switch expression'].body.some((line: string) => line.includes('break')));
});

test("Ny explicit interpolation scopes expressions, nested strings, and literal braces", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  const line = 'let label = $"{{raw}} {describe("text")} {height + 1.6}"';
  assert.ok(scopesForFragment(grammar, line, '{{').includes('constant.character.escape.brace.ny'));
  assert.ok(scopesForFragment(grammar, line, 'describe').includes('support.function.any-method.ny'));
  assert.ok(scopesForFragment(grammar, line, 'text').includes('string.quoted.double.ny'));
  assert.ok(scopesForFragment(grammar, line, 'height').includes('meta.embedded.expression.ny'));
  assert.ok(scopesForFragment(grammar, line, '1.6').includes('constant.numeric.decimal.ny'));
  const nested = 'let value = $"{match kind { 1 -> "one"; _ -> "other"; }} done"';
  assert.ok(scopesForFragment(grammar, nested, 'done').includes('string.quoted.double.interpolated.ny'));
  assert.equal(scopesForFragment(grammar, nested, 'done').includes('meta.interpolation.ny'), false);
});

test("grammar scopes annotations, literals, operators, and both line comments", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  assert.ok(scopesForFragment(grammar, "@ai_context(intent=\"safe\")", "@ai_context").includes("storage.type.annotation.ny"));
  assert.ok(scopesForFragment(grammar, "let answer: i32 = 0x2A", "0x2A").includes("constant.numeric.hex.ny"));
  assert.ok(scopesForFragment(grammar, "if answer >= 42 then", ">=").includes("keyword.operator.ny"));
  assert.ok(scopesForFragment(grammar, "let inverted = ~value", "~").includes("keyword.operator.ny"));
  assert.ok(scopesForFragment(grammar, 'asm "x86_64", "nop" () end asm', "asm").includes("keyword.control.ny"));
  assert.ok(scopesForFragment(grammar, "// slash comment", "slash comment").includes("comment.line.double-slash.ny"));
  assert.ok(scopesForFragment(grammar, "# hash comment", "hash comment").includes("comment.line.number-sign.ny"));
});

test("Ny annotation properties and qualified names stay inside metadata regions", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  for (const name of ["Company.review", "Company::review"])
    assert.ok(scopesForFragment(grammar, `@${name}(enabled=true)`, '@' + name).includes('storage.type.annotation.ny'));
  assert.ok(scopesForFragment(grammar, '@custom(enabled=true)', 'enabled').includes('variable.parameter.annotation.ny'));
  assert.equal(scopesForFragment(grammar, 'enabled = true', 'enabled').includes('variable.parameter.annotation.ny'), false);
});

test("grammar keeps scientific literals whole and preserves hexadecimal integers", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  for (const literal of ["1e3", "1.5E-3", "2E+2", "5e-324", "0e99999999999999999999"]) {
    assert.ok(scopesForFragment(grammar, `let value = ${literal}-2.0`, literal).includes("constant.numeric.decimal.ny"));
  }
  assert.ok(scopesForFragment(grammar, "let value = 0x1E3", "0x1E3").includes("constant.numeric.hex.ny"));
  const config = JSON.parse(fs.readFileSync("language-configuration.json", "utf8")) as { wordPattern: string };
  assert.deepEqual("1e3 1.5E-3 2E+2 0x1E3".match(new RegExp(config.wordPattern, "g")), ["1e3", "1.5E-3", "2E+2", "0x1E3"]);
});

test("grammar maintains both accepted multiline comment forms", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  const tripleStart = grammar.tokenizeLine("### context", INITIAL);
  assert.ok(tripleStart.tokens.some((token) => token.scopes.includes("comment.block.ny")));
  const tripleEnd = grammar.tokenizeLine("still context ###", tripleStart.ruleStack);
  assert.ok(tripleEnd.tokens.some((token) => token.scopes.includes("comment.block.ny")));

  const cStart = grammar.tokenizeLine("/* context", INITIAL);
  assert.ok(cStart.tokens.some((token) => token.scopes.includes("comment.block.ny")));
  const cEnd = grammar.tokenizeLine("still context */", cStart.ruleStack);
  assert.ok(cEnd.tokens.some((token) => token.scopes.includes("comment.block.ny")));
});

test("Ny prose patterns scope captures and nested blocks without interpolation", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  const opened = grammar.tokenizeLine("pattern price", INITIAL);
  assert.ok(opened.tokens.some(token => token.scopes.includes("entity.name.pattern.ny")));
  let state = opened.ruleStack;
  const capture = grammar.tokenizeLine("capture amount matching one or more ascii digits", state);
  assert.ok(capture.tokens.some(token => token.scopes.includes("entity.name.capture.ny")));
  state = grammar.tokenizeLine("require next not to match begin pattern", capture.ruleStack).ruleStack;
  const literal = grammar.tokenizeLine('literal "{amount} end pattern"', state);
  assert.ok(literal.tokens.some(token => token.scopes.includes("string.quoted.double.pattern.ny")));
  assert.equal(literal.tokens.some(token => token.scopes.includes("meta.interpolation.ny")), false);
  state = grammar.tokenizeLine("end pattern", literal.ruleStack).ruleStack;
  const outer = grammar.tokenizeLine("optional literal \" USD\"", state);
  assert.ok(outer.tokens.some(token => token.scopes.includes("keyword.control.pattern.ny")));
  state = grammar.tokenizeLine("end pattern", outer.ruleStack).ruleStack;
  const ordinary = grammar.tokenizeLine("let pattern = capture + 1", state);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("meta.pattern.ny")), false);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("keyword.control.pattern.ny")), false);
});

test("Ny query clauses are contextual and ordinary expressions retain their scopes", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  let state = grammar.tokenizeLine("let chosen = collect query from item in items", INITIAL).ruleStack;
  for (const line of ["where item > 0", "order by item descending", "skip 1", "take 2", "select item + 1"]) {
    const result = grammar.tokenizeLine(line, state);
    assert.ok(result.tokens.some(token => token.scopes.includes("keyword.control.query.ny")), line);
    state = result.ruleStack;
  }
  state = grammar.tokenizeLine("end query", state).ruleStack;
  const ordinary = grammar.tokenizeLine("let where = take + select", state);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("meta.query.ny")), false);
  assert.equal(ordinary.tokens.some(token => token.scopes.includes("keyword.control.query.ny")), false);
  const snippets = JSON.parse(fs.readFileSync("snippets/ny.code-snippets", "utf8")) as Record<string, { prefix: string; body: string[] }>;
  assert.equal(snippets["Text pattern"]!.prefix, "pattern");
  assert.equal(snippets["Collect query"]!.prefix, "query");
  assert.equal(snippets["Text pattern"]!.body.at(-1), "end pattern");
  assert.equal(snippets["Collect query"]!.body.at(-1), "end query");
});

test("Ny query keywords match the compiler's projection and ordering vocabulary", async () => {
  const grammar = await loadNyGrammar();
  assert.ok(grammar);
  const state = grammar.tokenizeLine("collect query", INITIAL).ruleStack;
  for (const word of ["select", "ascending", "descending"]) {
    const result = grammar.tokenizeLine(word, state);
    assert.ok(result.tokens.some(token => token.scopes.includes("keyword.control.query.ny")), word);
  }
  for (const word of ["give", "asc", "desc", "ascending_value", "descending_value"]) {
    const result = grammar.tokenizeLine(word, state);
    assert.equal(result.tokens.some(token => token.scopes.includes("keyword.control.query.ny")), false, word);
  }
});
