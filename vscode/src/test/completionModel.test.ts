import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { completionByteOffset, CompilerCompletion, parseCompilerCompletion } from "../completionModel";
import { parseAnalysisOutput } from "../analysisModel";

const source = path.join(path.parse(process.cwd()).root, "completion.matrix");
function fixture(): CompilerCompletion {
  const text = "!!! é😀\r\ngive `cou-suffix`\r\n";
  return { schema: "nil.completion.v1", partial: true, path: source,
    cursor: completionByteOffset(text, text.indexOf("cou") + 3)!,
    start: completionByteOffset(text, text.indexOf("`"))!, length: Buffer.byteLength("`cou-suffix`"),
    sources: [{ path: source, text }],
    items: [{ name: "count-value", kind: "variable", type: "number", insert_text: "count-value" }] };
}

test("completion accepts exact UTF-8 cursor and full-token replacement across Unicode and CRLF", () => {
  assert.deepEqual(parseCompilerCompletion(fixture()), fixture());
  assert.equal(completionByteOffset("é😀x", 3), 6);
  assert.equal(completionByteOffset("é😀x", 2), undefined);
  assert.equal(completionByteOffset("abc", 4), undefined);
  assert.equal(completionByteOffset("abc", -1), undefined);
});

test("completion can accompany an unresolved-prefix diagnostic without checked semantic bindings", () => {
  const envelope = { schema: "nil.analysis.v1", source, language: "matrix", success: false,
    diagnostics: [], reflection: {}, semantic: null, completion: fixture() };
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(envelope), source, 1).completion, fixture());
  assert.equal(parseCompilerCompletion(null), undefined);
  assert.equal(parseCompilerCompletion(undefined), undefined);
});

test("completion rejects malformed identities, offsets, candidates and duplicate edits", () => {
  for (const mutate of [
    (v: any) => v.schema = "nil.completion.v2",
    (v: any) => v.partial = false,
    (v: any) => v.path = "relative.matrix",
    (v: any) => v.path = path.join(path.dirname(source), "missing.matrix"),
    (v: any) => v.sources.push(v.sources[0]),
    (v: any) => v.sources[0].text += "\ud800",
    (v: any) => v.length = 0,
    (v: any) => v.length = 999,
    (v: any) => v.start = 5,
    (v: any) => v.cursor = 5,
    (v: any) => v.cursor = v.start + v.length + 1,
    (v: any) => v.items[0].kind = "field",
    (v: any) => v.items[0].insert_text = "",
    (v: any) => v.items[0].insert_text = "one\ntwo",
    (v: any) => v.items[0].name = "\0",
    (v: any) => v.items.push({ ...v.items[0], name: "duplicate-label" })
  ]) {
    const value = fixture(); mutate(value); assert.throws(() => parseCompilerCompletion(value));
  }
});

test("completion bounds sources, candidates and encoded payload without truncating", () => {
  for (const mutate of [
    (v: any) => v.sources = Array(257).fill(v.sources[0]),
    (v: any) => v.items = Array(4097).fill(v.items[0]),
    (v: any) => v.sources[0].text += "x".repeat(4 * 1024 * 1024)
  ]) {
    const value = fixture(); mutate(value); assert.throws(() => parseCompilerCompletion(value));
  }
  const empty = fixture(); empty.items = [];
  assert.deepEqual(parseCompilerCompletion(empty), empty);
});
