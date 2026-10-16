import * as path from "node:path";
import { ToolInvocation } from "./taskModel";

export const maxAnalysisOutputBytes = 8 * 1024 * 1024;

export interface AnalysisFix {
  start: number;
  length: number;
  replacement: string;
  description: string;
}

export interface AnalysisDiagnostic {
  code: string;
  severity: "error" | "warning" | "note" | "help";
  message: string;
  path: string | null;
  start: number | null;
  length: number | null;
  hint: string;
  fix: AnalysisFix | null;
}

export interface AnalysisResult {
  schema: "nil.analysis.v1";
  source: string;
  language: "ny" | "matrix";
  success: boolean;
  diagnostics: AnalysisDiagnostic[];
  reflection: Record<string, unknown>;
}

function object(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function validSpan(start: unknown, length: unknown): boolean {
  return typeof start === "number" && typeof length === "number" &&
    Number.isSafeInteger(start) && Number.isSafeInteger(length) &&
    start >= 0 && length >= 0 && Number.isSafeInteger(start + length);
}

export function parseAnalysisOutput(stdout: string, expectedSource: string, exitCode: number): AnalysisResult {
  if (exitCode !== 0 && exitCode !== 1) throw new Error(`Analysis exited with status ${exitCode}.`);
  if (Buffer.byteLength(stdout, "utf8") > maxAnalysisOutputBytes) throw new Error("Analysis output exceeds 8 MiB.");
  let result: unknown;
  try { result = JSON.parse(stdout); }
  catch { throw new Error("Compiler did not return nil.analysis.v1 JSON. Update the SDK or check ny.compiler.path and ny.sdkPath."); }
  if (!object(result) || result.schema !== "nil.analysis.v1" ||
      typeof result.source !== "string" || !path.isAbsolute(result.source) ||
      path.normalize(result.source) !== path.normalize(expectedSource) ||
      (result.language !== "ny" && result.language !== "matrix") ||
      typeof result.success !== "boolean" || result.success !== (exitCode === 0) ||
      !Array.isArray(result.diagnostics) || result.diagnostics.length > 10_000 || !object(result.reflection)) {
    throw new Error("Compiler returned an invalid nil.analysis.v1 result.");
  }
  for (const diagnostic of result.diagnostics) {
    if (!object(diagnostic) || typeof diagnostic.code !== "string" ||
        typeof diagnostic.severity !== "string" || !["error", "warning", "note", "help"].includes(diagnostic.severity) ||
        typeof diagnostic.message !== "string" || typeof diagnostic.hint !== "string" ||
        !(diagnostic.path === null || (typeof diagnostic.path === "string" && path.isAbsolute(diagnostic.path))) ||
        !((diagnostic.start === null && diagnostic.length === null) || validSpan(diagnostic.start, diagnostic.length)) ||
        (result.success && diagnostic.severity === "error")) {
      throw new Error("Compiler returned an invalid analysis diagnostic.");
    }
    const fix = diagnostic.fix;
    if (fix !== null && (!object(fix) || diagnostic.path === null || !validSpan(fix.start, fix.length) ||
        typeof fix.replacement !== "string" || typeof fix.description !== "string")) {
      throw new Error("Compiler returned an invalid analysis fix.");
    }
  }
  return result as unknown as AnalysisResult;
}

export interface TextOffsets { start: number; end: number; }

// Index scalar boundaries once: compiler spans are UTF-8 bytes; VS Code offsets
// count UTF-16 code units. Interior bytes must never become an approximate edit.
export function createByteRangeMapper(text: string): (start: number, length: number) => TextOffsets | undefined {
  const boundaries = new Uint32Array(Buffer.byteLength(text, "utf8") + 1);
  let bytes = 0;
  let units = 0;
  boundaries[0] = 1;
  for (const scalar of text) {
    bytes += Buffer.byteLength(scalar, "utf8");
    units += scalar.length;
    boundaries[bytes] = units + 1;
  }
  return (start, length) => {
    if (!validSpan(start, length) || start + length >= boundaries.length) return undefined;
    const first = boundaries[start];
    const last = boundaries[start + length];
    return first && last ? { start: first - 1, end: last - 1 } : undefined;
  };
}

export function byteRangeToOffsets(text: string, start: number, length: number): TextOffsets | undefined {
  return createByteRangeMapper(text)(start, length);
}

export function analysisInvocation(
  compiler: string, source: string, sdkPath: string | undefined,
  noStdlib: boolean, includeRoots: readonly string[]
): ToolInvocation {
  const args = ["--analyze", "--source-stdin", source];
  if (noStdlib) args.push("--no-stdlib");
  const roots = new Set([path.dirname(source), ...(sdkPath ? [sdkPath] : []), ...includeRoots].map(root => path.normalize(root)));
  for (const root of roots) args.push("-I", root);
  return { command: compiler, args };
}
