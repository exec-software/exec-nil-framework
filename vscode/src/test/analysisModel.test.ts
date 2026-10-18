import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";

import {
  analysisInvocation,
  byteRangeToOffsets,
  createByteRangeMapper,
  parseAnalysisOutput
} from "../analysisModel";

const workRoot = path.join(path.parse(process.cwd()).root, "work");
const source = path.join(workRoot, "page.matrix");

function diagnostic(overrides: Record<string, unknown> = {}) {
  return {
    code: "E001",
    severity: "error",
    message: "Expected a closing delimiter",
    path: source,
    start: 7,
    length: 0,
    hint: "Close the expression",
    fix: {
      start: 7,
      length: 0,
      replacement: ")",
      description: "Insert closing delimiter"
    },
    ...overrides
  };
}

function envelope(overrides: Record<string, unknown> = {}) {
  return {
    schema: "nil.analysis.v1",
    source,
    language: "matrix",
    success: false,
    diagnostics: [diagnostic()],
    reflection: {},
    ...overrides
  };
}

test("analysis accepts compiler errors with exit code one and preserves fixes", () => {
  const response = envelope();
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(response), source, 1), response);
});

test("successful analysis preserves warnings, notes, help, and unlocated diagnostics", () => {
  const response = envelope({
    language: "ny",
    success: true,
    diagnostics: ["warning", "note", "help"].map((severity) => diagnostic({
      severity, path: null, start: null, length: null, fix: null
    })),
    reflection: { functions: [{ name: "main" }] }
  });
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(response), source, 0), response);
  const emptyResponse = envelope({ success: true, diagnostics: [] });
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(emptyResponse), source, 0), emptyResponse);
});

test("analysis rejects process failures even when stdout contains a valid envelope", () => {
  for (const exitCode of [-1, 2, 127]) {
    assert.throws(() => parseAnalysisOutput(JSON.stringify(envelope()), source, exitCode));
  }
});

test("analysis rejects inconsistent exit status, success flags, and error diagnostics", () => {
  assert.throws(() => parseAnalysisOutput(JSON.stringify(envelope()), source, 0));
  assert.throws(() => parseAnalysisOutput(
    JSON.stringify(envelope({ success: true, diagnostics: [] })), source, 1
  ));
  assert.throws(() => parseAnalysisOutput(JSON.stringify(envelope({ success: true })), source, 0));
});

test("analysis rejects malformed JSON and non-object results", () => {
  for (const stdout of ["", "not JSON", "{} trailing output", "null", "[]", '"result"', "true"]) {
    assert.throws(() => parseAnalysisOutput(stdout, source, 0), stdout);
  }
});

test("analysis rejects a response for a different source file", () => {
  assert.throws(() => parseAnalysisOutput(
    JSON.stringify(envelope({ source: path.join(workRoot, "other.matrix") })), source, 1
  ));
});

test("analysis compares normalized absolute source paths", () => {
  const normalizedEquivalent = `${workRoot}${path.sep}.${path.sep}page.matrix`;
  assert.doesNotThrow(() => parseAnalysisOutput(
    JSON.stringify(envelope({ source: normalizedEquivalent })), source, 1
  ));
});

test("analysis rejects oversized output and excessive diagnostic counts", () => {
  const oversized = envelope({ reflection: { text: "x".repeat(8 * 1024 * 1024) } });
  assert.throws(() => parseAnalysisOutput(JSON.stringify(oversized), source, 1));
  const excessive = envelope({ diagnostics: Array.from({ length: 10_001 }, () => diagnostic()) });
  assert.throws(() => parseAnalysisOutput(JSON.stringify(excessive), source, 1));
});

test("analysis requires the supported schema and complete result shape", () => {
  const invalidFields: Record<string, unknown>[] = [
    { schema: "nil.analysis.v2" },
    { schema: undefined },
    { source: 42 },
    { source: "page.matrix" },
    { language: "typescript" },
    { language: undefined },
    { success: "false" },
    { success: undefined },
    { diagnostics: {} },
    { diagnostics: null },
    { diagnostics: undefined },
    { reflection: null },
    { reflection: [] },
    { reflection: "symbols" },
    { reflection: undefined }
  ];
  for (const fields of invalidFields) {
    assert.throws(() => parseAnalysisOutput(JSON.stringify(envelope(fields)), source, 1),
      `Invalid envelope fields: ${JSON.stringify(fields)}`);
  }
});

test("analysis validates diagnostic fields before exposing compiler output", () => {
  const invalidDiagnostics: unknown[] = [
    null,
    [],
    "error",
    diagnostic({ code: 1 }),
    diagnostic({ code: undefined }),
    diagnostic({ severity: "fatal" }),
    diagnostic({ severity: ["warning"] }),
    diagnostic({ message: null }),
    diagnostic({ path: 1 }),
    diagnostic({ path: "page.matrix" }),
    diagnostic({ hint: null }),
    diagnostic({ hint: undefined }),
    diagnostic({ fix: undefined })
  ];
  for (const value of invalidDiagnostics) {
    assert.throws(() => parseAnalysisOutput(
      JSON.stringify(envelope({ diagnostics: [value] })), source, 1
    ), `Invalid diagnostic: ${JSON.stringify(value)}`);
  }
});

test("analysis requires paired nullable or nonnegative safe integer diagnostic spans", () => {
  const invalidSpans: Record<string, unknown>[] = [
    { start: null, length: 0 },
    { start: 0, length: null },
    { start: undefined },
    { length: undefined },
    { start: -1 },
    { length: -1 },
    { start: 0.5 },
    { length: 0.5 },
    { start: "0" },
    { length: "0" },
    { start: Number.MAX_SAFE_INTEGER + 1 },
    { length: Number.MAX_SAFE_INTEGER + 1 },
    { start: Number.MAX_SAFE_INTEGER, length: 1 }
  ];
  for (const span of invalidSpans) {
    assert.throws(() => parseAnalysisOutput(
      JSON.stringify(envelope({ diagnostics: [diagnostic(span)] })), source, 1
    ), `Invalid diagnostic span: ${JSON.stringify(span)}`);
  }
});

test("analysis rejects malformed fixes and unsafe edit spans", () => {
  const validFix = diagnostic().fix;
  const invalidFixes: unknown[] = [
    [],
    "insert a delimiter",
    {},
    { ...validFix, start: null },
    { ...validFix, length: null },
    { ...validFix, start: -1 },
    { ...validFix, length: -1 },
    { ...validFix, start: 0.5 },
    { ...validFix, start: Number.MAX_SAFE_INTEGER + 1 },
    { ...validFix, length: Number.MAX_SAFE_INTEGER + 1 },
    { ...validFix, start: Number.MAX_SAFE_INTEGER, length: 1 },
    { ...validFix, replacement: null },
    { ...validFix, description: undefined }
  ];
  for (const fix of invalidFixes) {
    assert.throws(() => parseAnalysisOutput(
      JSON.stringify(envelope({ diagnostics: [diagnostic({ fix })] })), source, 1
    ), `Invalid fix: ${JSON.stringify(fix)}`);
  }
  assert.throws(() => parseAnalysisOutput(
    JSON.stringify(envelope({ diagnostics: [diagnostic({ path: null })] })), source, 1
  ), "A fix requires an absolute diagnostic path");
});

test("UTF-8 spans map accents, emoji, and CRLF to UTF-16 editor offsets", () => {
  const text = "Aé😀\r\nZ";
  const boundaries = [
    [0, 0], [1, 1], [3, 2], [7, 4], [8, 5], [9, 6], [10, 7]
  ] as const;
  for (const [startByte, startOffset] of boundaries) {
    for (const [endByte, endOffset] of boundaries) {
      if (endByte < startByte) continue;
      assert.deepEqual(byteRangeToOffsets(text, startByte, endByte - startByte), {
        start: startOffset, end: endOffset
      }, `Byte range ${startByte}..${endByte}`);
    }
  }
});

test("UTF-8 spans reject either endpoint inside a multibyte scalar", () => {
  const text = "Aé😀\r\nZ";
  for (const interiorByte of [2, 4, 5, 6]) {
    assert.equal(byteRangeToOffsets(text, interiorByte, 0), undefined);
    assert.equal(byteRangeToOffsets(text, interiorByte, 10 - interiorByte), undefined);
    assert.equal(byteRangeToOffsets(text, 0, interiorByte), undefined);
  }
});

test("UTF-8 spans allow insertion at EOF and in an empty document", () => {
  assert.deepEqual(byteRangeToOffsets("é😀", 6, 0), { start: 3, end: 3 });
  assert.deepEqual(byteRangeToOffsets("", 0, 0), { start: 0, end: 0 });
  assert.equal(byteRangeToOffsets("", 0, 1), undefined);
});

test("a reusable byte mapper keeps independent document indexes and survives invalid ranges", () => {
  const unicode = createByteRangeMapper("é😀");
  const ascii = createByteRangeMapper("abcdef");
  assert.deepEqual(unicode(2, 4), { start: 1, end: 3 });
  assert.deepEqual(ascii(2, 4), { start: 2, end: 6 });
  assert.equal(unicode(3, 0), undefined);
  assert.deepEqual(unicode(0, 2), { start: 0, end: 1 });
  assert.deepEqual(unicode(6, 0), { start: 3, end: 3 });
  assert.deepEqual(ascii(0, 6), { start: 0, end: 6 });
});

test("UTF-8 spans reject invalid integers, overflow, and ranges beyond the document", () => {
  const invalidRanges = [
    [-1, 0], [0, -1], [0.5, 0], [0, 0.5],
    [NaN, 0], [0, NaN], [Infinity, 0], [0, Infinity],
    [Number.MAX_SAFE_INTEGER + 1, 0], [0, Number.MAX_SAFE_INTEGER + 1],
    [Number.MAX_SAFE_INTEGER, 1],
    [4, 0], [3, 1], [2, 2]
  ] as const;
  for (const [start, length] of invalidRanges) {
    assert.equal(byteRangeToOffsets("abc", start, length), undefined,
      `Invalid byte range start=${start}, length=${length}`);
  }
});

test("analysis invocation includes the source parent before SDK and configured roots", () => {
  const sdk = path.join(workRoot, "sdk");
  const imports = path.join(workRoot, "imports");
  const roots = Object.freeze([imports, sdk, workRoot, imports]);
  assert.deepEqual(analysisInvocation("nilcompiler", source, sdk, true, roots), {
    command: "nilcompiler",
    args: [
      "--analyze", "--source-stdin", source, "--no-stdlib",
      "-I", workRoot, "-I", sdk, "-I", imports
    ]
  });
  assert.deepEqual(roots, [imports, sdk, workRoot, imports]);
});

test("analysis invocation omits unconfigured options while retaining local imports", () => {
  assert.deepEqual(analysisInvocation("nilcompiler", source, undefined, false, []), {
    command: "nilcompiler",
    args: ["--analyze", "--source-stdin", source, "-I", workRoot]
  });
});

test("analysis invocation preserves spaces and shell characters as literal arguments", () => {
  const compiler = path.join(workRoot, "SDK tools", "nilcompiler");
  const shellSource = path.join(workRoot, "with spaces;$(echo nope).matrix");
  const imports = path.join(workRoot, "imports & 'quoted'");
  assert.deepEqual(analysisInvocation(compiler, shellSource, undefined, false, [imports]), {
    command: compiler,
    args: ["--analyze", "--source-stdin", shellSource, "-I", workRoot, "-I", imports]
  });
});

test("analysis source snapshots are optional, bounded, absolute and unique", () => {
  const imported = path.join(workRoot, "imported.ny");
  const response = envelope({ sources: [{ path: imported, text: "é😀\r\n" }] });
  assert.deepEqual(parseAnalysisOutput(JSON.stringify(response), source, 1), response);
  for (const sources of [null, {}, [{ path: "relative.ny", text: "" }], [{ path: source, text: "" }],
    [{ path: imported, text: 7 }], [{ path: imported, text: "" }, { path: imported, text: "" }],
    [{ path: imported, text: "\n".repeat(600_000) }]]) {
    assert.throws(() => parseAnalysisOutput(JSON.stringify(envelope({ sources })), source, 1));
  }
});

test("analysis invocation carries the same rule policy as project checks", () => {
  assert.deepEqual(analysisInvocation("nilcompiler", source, undefined, false, [], {
    disabledRules: ["NIL1001", "NIL2002"], warningsAsErrors: true
  }).args, ["--analyze", "--source-stdin", source, "--disable-rule", "NIL1001", "--disable-rule", "NIL2002",
    "--warnings-as-errors", "-I", workRoot]);
});
