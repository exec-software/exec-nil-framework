import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { definitionAt, parseSemanticIndex, planRename, referencesAt, renamePreservesBindings, SemanticIndex } from "../semanticModel";
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

function editableFixture(name = "item", firstId = 1): SemanticIndex {
  const text = `!!! é😀\r\nvalue ${name} is 0\r\n${name}\r\nvalue other is 1\r\nother`;
  const span = (word: string, offset: number) => ({ path: source, start: Buffer.byteLength(text.slice(0, offset), "utf8"), length: word.length });
  return { schema: "nil.semantic.v1", partial: true, sources: [{ path: source, text }],
    symbols: [{ id: firstId, kind: "variable", name, type: "i64", ...span(name, text.indexOf(name)), references_complete: true, rename_safe: true },
      { id: firstId + 1, kind: "variable", name: "other", type: "i64", ...span("other", text.indexOf("other")) }],
    references: [{ symbol_id: firstId, ...span(name, text.lastIndexOf(name)) }, { symbol_id: firstId + 1, ...span("other", text.lastIndexOf("other")) }],
    rename_policy: { schema: "nil.rename.v1", sources: [{ path: source, language: "matrix", identifier_keys: [name.replace(/[-_]/g, "").toLowerCase(), "other"], reserved_keys: ["value", "give"] }] } };
}

test("references require compiler completeness and respect declaration inclusion", () => {
  const index = parseSemanticIndex(editableFixture())!;
  const offset = index.sources[0]!.text.indexOf("item");
  assert.equal(referencesAt(index, source, offset, false)!.length, 1);
  assert.equal(referencesAt(index, source, offset, true)!.length, 2);
  delete index.symbols[0]!.rename_safe;
  assert.ok(parseSemanticIndex(index));
  assert.equal(referencesAt(index, source, offset, true)!.length, 2);
  assert.equal(planRename(index, source, offset, "updated"), undefined);
  assert.equal(referencesAt(index, source, index.sources[0]!.text.indexOf("other"), true), undefined);
  delete index.symbols[0]!.references_complete;
  assert.equal(referencesAt(index, source, offset, true), undefined);
});

test("rename validates compiler policy, Matrix spelling and exact UTF-8 spans", () => {
  const index = editableFixture();
  const offset = index.sources[0]!.text.indexOf("item");
  const plan = planRename(index, source, offset, "total-count")!;
  assert.equal(plan.text, editableFixture("total-count").sources[0]!.text);
  assert.equal(plan.edits.length, 2);
  for (const edit of plan.edits) assert.equal(index.sources[0]!.text.slice(edit.offsets.start, edit.offsets.end), "item");
  for (const name of ["OTHER", "o-t-h-e-r", "GIVE", "give", "item", "_", "a--b", "a-1", "`new`", "a b", "é", "a".repeat(257)]) {
    assert.equal(planRename(index, source, offset, name), undefined, name);
  }
  delete index.rename_policy;
  assert.equal(planRename(index, source, offset, "total"), undefined);
});

test("speculative rename preserves the entire binding graph with new snapshot IDs", () => {
  const before = editableFixture();
  for (const newName of ["total-count", "x"]) {
    const plan = planRename(before, source, before.sources[0]!.text.indexOf("item"), newName)!;
    const after = editableFixture(newName, 41);
    assert.equal(renamePreservesBindings(before, after, plan), true);
    for (const mutate of [
      (index: SemanticIndex) => { index.references[1]!.symbol_id = 41; },
      (index: SemanticIndex) => { index.symbols[1]!.type = "String"; },
      (index: SemanticIndex) => { index.references.pop(); },
      (index: SemanticIndex) => { index.sources[0]!.text += " "; },
      (index: SemanticIndex) => { delete index.symbols[0]!.rename_safe; }
    ]) {
      const changed = structuredClone(after); mutate(changed);
      assert.equal(renamePreservesBindings(before, changed, plan), false);
    }
  }
});

test("editing capability and policy validation fails closed", () => {
  for (const mutate of [
    (v: any) => v.symbols[0].references_complete = false,
    (v: any) => delete v.symbols[0].references_complete,
    (v: any) => v.symbols[0].kind = "function",
    (v: any) => v.rename_policy.schema = "nil.rename.v2",
    (v: any) => v.rename_policy.sources[0].language = "unknown",
    (v: any) => v.rename_policy.sources[0].path = "/missing.matrix",
    (v: any) => v.rename_policy.sources[0].identifier_keys.push("item"),
    (v: any) => v.rename_policy.sources[0].reserved_keys = ["\ud800"]
  ]) {
    const index = editableFixture(); mutate(index); assert.throws(() => parseSemanticIndex(index));
  }
});

test("rename policy enforces per-source and aggregate identifier budgets", () => {
  const index = editableFixture();
  index.rename_policy!.sources[0]!.identifier_keys = Array.from({ length: 32769 }, (_, i) => `key${i}`);
  assert.throws(() => parseSemanticIndex(index));
  index.rename_policy!.sources = [];
  for (let i = 0; i < 5; i++) {
    const extra = path.join(root, `budget${i}.matrix`);
    index.sources.push({ path: extra, text: "" });
    index.rename_policy!.sources.push({ path: extra, language: "matrix", reserved_keys: [],
      identifier_keys: Array.from({ length: 32768 }, (_, j) => `key${j}`) });
  }
  assert.throws(() => parseSemanticIndex(index));
});
