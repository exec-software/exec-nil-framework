import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { completionByteOffset } from "../completionModel";
import { CompilerSignature, parseCompilerSignature } from "../signatureModel";
import { parseAnalysisOutput } from "../analysisModel";

const source = path.join(path.parse(process.cwd()).root, "signature.matrix");
function fixture(): CompilerSignature {
  const text = "!!! é😀\r\nsay combine with second being unknown, first being 1\r\n";
  const start = completionByteOffset(text, text.indexOf(" second"))!;
  return { schema: "nil.signature.v1", partial: true, path: source,
    cursor: completionByteOffset(text, text.indexOf("unknown") + 2)!, start,
    length: completionByteOffset(text, text.lastIndexOf("\r\n"))! - start,
    sources: [{ path: source, text }],
    label: "combine receives first as number, second as text returns text",
    parameters: [
      { name: "first", type: "number", label: "first as number", variadic: false },
      { name: "second", type: "text", label: "second as text", variadic: false }
    ], active_parameter: 1, return_type: "text" };
}

test("signature accepts UTF-8 call regions and resolved named-argument selection", () => {
  assert.deepEqual(parseCompilerSignature(fixture()), fixture());
  const value = fixture(); value.active_parameter = null;
  assert.deepEqual(parseCompilerSignature(value), value);
  value.parameters[1]!.variadic = true;
  assert.deepEqual(parseCompilerSignature(value), value);
});

test("signature accepts empty argument regions and does not require successful checking", () => {
  const value = fixture(); value.start = value.cursor; value.length = 0;
  value.label = "zero returns number"; value.return_type = "number";
  value.parameters = []; value.active_parameter = null;
  assert.deepEqual(parseCompilerSignature(value), value);
  const envelope = { schema: "nil.analysis.v1", source, language: "matrix", success: false,
    diagnostics: [], reflection: {}, semantic: null, signature: fixture() };
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(envelope), source, 1).signature, fixture());
  assert.equal(parseCompilerSignature(null), undefined);
  assert.equal(parseCompilerSignature(undefined), undefined);
});

test("signature rejects invalid identities, byte boundaries and parameter metadata", () => {
  for (const mutate of [
    (v: any) => v.schema = "nil.signature.v2",
    (v: any) => v.partial = false,
    (v: any) => v.path = "relative.matrix",
    (v: any) => v.path = path.join(path.dirname(source), "missing.matrix"),
    (v: any) => v.sources.push(v.sources[0]),
    (v: any) => v.sources[0].text += "\ud800",
    (v: any) => v.start = 5,
    (v: any) => v.cursor = v.start - 1,
    (v: any) => v.cursor = v.start + v.length + 1,
    (v: any) => v.length = Number.MAX_SAFE_INTEGER,
    (v: any) => v.active_parameter = -1,
    (v: any) => v.active_parameter = 2,
    (v: any) => v.label = "",
    (v: any) => v.label += "\n",
    (v: any) => v.parameters[0].label = "missing from signature",
    (v: any) => v.parameters[0].name = "",
    (v: any) => v.parameters[0].type = "\0",
    (v: any) => v.parameters[0].variadic = true,
    (v: any) => v.parameters[1].variadic = "false"
  ]) {
    const value = fixture(); mutate(value); assert.throws(() => parseCompilerSignature(value));
  }
});

test("signature bounds snapshots, parameters and encoded output without truncating", () => {
  for (const mutate of [
    (v: any) => v.sources = Array(257).fill(v.sources[0]),
    (v: any) => v.parameters = Array(257).fill(v.parameters[0]),
    (v: any) => v.sources[0].text += "x".repeat(4 * 1024 * 1024)
  ]) {
    const value = fixture(); mutate(value); assert.throws(() => parseCompilerSignature(value));
  }
});
