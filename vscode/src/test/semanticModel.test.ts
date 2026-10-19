import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { definitionAt, parseSemanticIndex, SemanticIndex } from "../semanticModel";
import { parseAnalysisOutput } from "../analysisModel";

const root = path.join(path.parse(process.cwd()).root, "semantic");
const source = path.join(root, "main.matrix"), target = path.join(root, "helper.ny");
function fixture(): SemanticIndex {
  return { schema: "nil.semantic.v1", partial: true,
    sources: [{ path: source, text: "!!! é😀\r\nfetch-value" }, { path: target, text: "fn fetch_value()" }],
    symbols: [{ id: 7, kind: "function", name: "fetch_value", type: "i32", path: target, start: 3, length: 11 }],
    references: [{ symbol_id: 7, path: source, start: 12, length: 11 }] };
}
test("compiler definitions use binding IDs across Unicode, CRLF and surface spellings", () => {
  const index = parseSemanticIndex(fixture())!;
  const match = definitionAt(index, source, 12)!;
  assert.equal(index.sources[0]!.text.slice(match.origin.start, match.origin.end), "fetch-value");
  assert.equal(match.targetText.slice(match.targetOffsets.start, match.targetOffsets.end), "fetch_value");
  assert.equal(match.target.path, target);
  assert.equal(definitionAt(index, source, 2), undefined);
  assert.equal(definitionAt(index, source, 500), undefined);
});
test("same-name lexical bindings remain distinct and ambiguous occurrences are refused", () => {
  const index: SemanticIndex = { schema: "nil.semantic.v1", partial: true,
    sources: [{ path: source, text: "x x x x" }],
    symbols: [0, 2].map((start, id) => ({ id, kind: "variable", name: "x", type: "i32", path: source, start, length: 1 })),
    references: [{ symbol_id: 1, path: source, start: 4, length: 1 }, { symbol_id: 0, path: source, start: 6, length: 1 }] };
  parseSemanticIndex(index);
  assert.equal(definitionAt(index, source, 4)!.target.start, 2);
  assert.equal(definitionAt(index, source, 6)!.target.start, 0);
  index.references.push({ symbol_id: 0, path: source, start: 4, length: 1 });
  assert.equal(definitionAt(index, source, 4), undefined);
});
test("semantic protocol rejects broken identities, missing sources and invalid byte boundaries", () => {
  for (const mutate of [
    (v: any) => v.symbols.push(v.symbols[0]),
    (v: any) => v.references[0].symbol_id = 999,
    (v: any) => v.sources.pop(),
    (v: any) => v.sources.push(v.sources[0]),
    (v: any) => v.references[0].start = 5,
    (v: any) => v.references[0].length = 999,
    (v: any) => v.symbols[0].length = 0,
    (v: any) => v.symbols[0].id = -1,
    (v: any) => v.sources[0].text += "\ud800",
    (v: any) => v.sources[0].path = "relative.matrix",
    (v: any) => v.partial = false,
    (v: any) => v.schema = "nil.semantic.v2"
  ]) {
    const value = fixture(); mutate(value); assert.throws(() => parseSemanticIndex(value));
  }
});
test("semantic protocol enforces source, symbol, reference and encoded payload bounds", () => {
  const base = fixture();
  for (const value of [
    { ...base, sources: Array(257).fill(base.sources[0]) },
    { ...base, symbols: Array(32769).fill(base.symbols[0]) },
    { ...base, references: Array(131073).fill(base.references[0]) },
    { ...base, sources: [{ path: source, text: "x".repeat(4 * 1024 * 1024) }] }
  ]) assert.throws(() => parseSemanticIndex(value));
});
test("semantic index is optional but cannot accompany failed compiler analysis", () => {
  assert.equal(parseSemanticIndex(null), undefined);
  assert.equal(parseSemanticIndex(undefined), undefined);
  const envelope = { schema: "nil.analysis.v1", source, language: "matrix", success: true, diagnostics: [], reflection: {}, semantic: fixture() };
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(envelope), source, 0).semantic, fixture());
  assert.throws(() => parseAnalysisOutput(JSON.stringify({ ...envelope, success: false }), source, 1));
});
