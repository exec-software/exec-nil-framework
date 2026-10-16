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
