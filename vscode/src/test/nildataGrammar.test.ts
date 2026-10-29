import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import test from "node:test";
import { loadWASM, OnigScanner, OnigString } from "vscode-oniguruma";
import { IGrammar, INITIAL, ITokenizeLineResult, parseRawGrammar, Registry, StateStack } from "vscode-textmate";

let grammarPromise: Promise<IGrammar> | undefined;
function loadDataGrammar(): Promise<IGrammar> {
  grammarPromise ??= (async () => {
    const wasm = fs.readFileSync(require.resolve("vscode-oniguruma/release/onig.wasm"));
    await loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const grammarPath = path.join(process.cwd(), "syntaxes", "nildata.tmLanguage.json");
    const registry = new Registry({
      onigLib: Promise.resolve({ createOnigScanner: sources => new OnigScanner(sources), createOnigString: value => new OnigString(value) }),
      loadGrammar: async scope => scope === "source.nildata" ? parseRawGrammar(fs.readFileSync(grammarPath, "utf8"), grammarPath) : null
    });
    const grammar = await registry.loadGrammar("source.nildata");
    assert.ok(grammar);
    return grammar;
  })();
  return grammarPromise;
}
function hasScope(result: ITokenizeLineResult, line: string, fragment: string, scope: string): void {
  const start = line.indexOf(fragment);
  assert.notEqual(start, -1);
  const token = result.tokens.find(item => item.startIndex <= start && item.endIndex >= start + fragment.length);
  assert.ok(token?.scopes.includes(scope + ".nildata"), `${fragment} needs ${scope} in ${line}: ${JSON.stringify(result.tokens)}`);
}

test("NILData header and nested object/array structure have independent data scopes", async () => {
  const grammar = await loadDataGrammar();
  let state: StateStack = INITIAL;
  const line = (source: string) => { const result = grammar.tokenizeLine(source, state); state = result.ruleStack; return result; };
  hasScope(line("nildata 1 # version"), "nildata 1 # version", "nildata", "keyword.other.version");
  hasScope(line("object"), "object", "object", "storage.type.container");
  hasScope(line("  items is an array"), "  items is an array", "items", "support.type.property-name");
  hasScope(line("    item is an object"), "    item is an object", "item", "keyword.other.array-item");
  const member = "      item is false # named member, not an array entry";
  hasScope(line(member), member, "item", "support.type.property-name");
  hasScope(line('      "0" is null'), '      "0" is null', '"0"', "support.type.property-name");
  hasScope(line("    end object"), "    end object", "end", "keyword.control.end");
  hasScope(line("    item is 1e9999"), "    item is 1e9999", "1e9999", "constant.numeric.json");
  hasScope(line("  end array"), "  end array", "end", "keyword.control.end");
  hasScope(line("  after is true"), "  after is true", "after", "support.type.property-name");
  line("end object");
  assert.equal(line("true").tokens.some(token => token.scopes.some(scope => scope.startsWith("meta.structure."))), false);
  for (const root of ["array", "an array", "object", "an object"]) {
    assert.ok(grammar.tokenizeLine(root, INITIAL).tokens.some(token => token.scopes.includes("storage.type.container.nildata")), root);
  }
});

test("NILData strings keep comments, interpolation-looking text, and keywords literal", async () => {
  const grammar = await loadDataGrammar();
  const state = grammar.tokenizeLine("object", INITIAL).ruleStack;
  const source = '  "hash#key" is "# null {name} end object" # comment';
  const result = grammar.tokenizeLine(source, state);
  hasScope(result, source, "hash#key", "support.type.property-name");
  hasScope(result, source, "# null {name} end object", "string.quoted.double");
  hasScope(result, source, "# comment", "comment.line.number-sign");
  assert.equal(result.tokens.some(token => token.scopes.some(scope => /interpolation|\.matrix|\.ny$/.test(scope))), false);
  for (const keyword of ["null", "true", "false"]) {
    hasScope(grammar.tokenizeLine(keyword, INITIAL), keyword, keyword, "constant.language");
    const quoted = `"${keyword}"`;
    const string = grammar.tokenizeLine(quoted, INITIAL);
    hasScope(string, quoted, keyword, "string.quoted.double");
    assert.equal(string.tokens.some(token => token.scopes.includes("constant.language.nildata")), false);
  }
  const escapes = String.raw`"quote\" slash\/ backslash\\ controls\b\f\n\r\t nul\u0000"`;
  const escaped = grammar.tokenizeLine(escapes, INITIAL);
  for (const fragment of [String.raw`\"`, String.raw`\/`, String.raw`\\`, String.raw`\b`, String.raw`\f`, String.raw`\n`, String.raw`\r`, String.raw`\t`, String.raw`\u0000`]) {
    hasScope(escaped, escapes, fragment, "constant.character.escape");
  }
  const invalid = String.raw`"bad\q"`;
  hasScope(grammar.tokenizeLine(invalid, INITIAL), invalid, String.raw`\q`, "invalid.illegal.escape");
});

test("NILData numbers follow JSON grammar and executable forms are not code", async () => {
  const grammar = await loadDataGrammar();
  for (const value of ["0", "-0", "-1.6", "9007199254740993", "1.2300", "1e9999", "1E+9999", "-1.25e-20"]) {
    hasScope(grammar.tokenizeLine(value + " # number", INITIAL), value + " # number", value, "constant.numeric.json");
  }
  for (const value of ["01", "-01", "+1", ".5", "1.", "1e", "NaN", "Infinity", "0x10", "falsehood", "TRUE", 'formatted "text"', '$"{value}"', "call()", "list"]) {
    const result = grammar.tokenizeLine(value, INITIAL);
    assert.ok(result.tokens.some(token => token.scopes.includes("invalid.illegal.unexpected.nildata")), value);
    assert.equal(result.tokens.some(token => token.scopes.includes("constant.numeric.json.nildata")), false, value);
    assert.equal(result.tokens.some(token => token.scopes.some(scope => /entity.name.function|interpolation/.test(scope))), false, value);
  }
  const state = grammar.tokenizeLine("object", INITIAL).ruleStack;
  const invalidNested = "  entries is array";
  hasScope(grammar.tokenizeLine(invalidNested, state), invalidNested, "array", "invalid.illegal.unexpected");
});

test("NILData malformed strings recover at the next line", async () => {
  const grammar = await loadDataGrammar();
  const object = grammar.tokenizeLine("object", INITIAL).ruleStack;
  const broken = grammar.tokenizeLine('  broken is "unterminated', object);
  const source = "  healthy is true";
  const recovered = grammar.tokenizeLine(source, broken.ruleStack);
  hasScope(recovered, source, "healthy", "support.type.property-name");
  hasScope(recovered, source, "true", "constant.language");
});

test("NILData indentation and folding recognize containers without treating text as structure", () => {
  const config = JSON.parse(fs.readFileSync("language-configuration.nildata.json", "utf8")) as {
    comments: { lineComment: string }; brackets: string[][];
    indentationRules: { increaseIndentPattern: string; decreaseIndentPattern: string };
    folding: { markers: { start: string; end: string } };
  };
  assert.equal(config.comments.lineComment, "#");
  assert.deepEqual(config.brackets, []);
  const open = new RegExp(config.indentationRules.increaseIndentPattern);
  const close = new RegExp(config.indentationRules.decreaseIndentPattern);
  const fold = new RegExp(config.folding.markers.start);
  for (const source of ["object", "an object", "array", "an array", "  children is an array # data", '  "quoted key" is an object', "  item is an array"]) {
    assert.ok(open.test(source), source);
    assert.ok(fold.test(source), source);
  }
  for (const source of ['  text is "an object"', '  text is "end array"', "# an array", "  values is array", "  list", "  item is 1"]) {
    assert.equal(open.test(source), false, source);
    assert.equal(fold.test(source), false, source);
  }
  for (const source of ["end object", "  end array # done"]) {
    assert.ok(close.test(source), source);
    assert.ok(new RegExp(config.folding.markers.end).test(source), source);
  }
});
