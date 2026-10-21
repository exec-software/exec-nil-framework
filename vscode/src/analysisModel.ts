import * as path from "node:path";
import { ToolInvocation } from "./taskModel";
import { parseSemanticIndex, SemanticIndex } from "./semanticModel";
import { CompilerCompletion, parseCompilerCompletion } from "./completionModel";
import { CompilerSignature, parseCompilerSignature } from "./signatureModel";
export { byteRangeToOffsets, createByteRangeMapper, TextOffsets } from "./sourceOffsets";

export const maxAnalysisSnapshotBytes = 1024 * 1024;

export const maxAnalysisOutputBytes = 8 * 1024 * 1024;
export const maxAnalysisSourceBytes = 2 * 1024 * 1024;
export const maxAnalysisWorkspaceBytes = 8 * 1024 * 1024;
export const maxAnalysisWorkspaceSources = 256;

export interface AnalysisSourceSnapshot { path: string; text: string; }

/** Never silently drop a dirty buffer when a workspace request reaches a limit. */
export function serializeAnalysisWorkspace(sources: readonly AnalysisSourceSnapshot[]): string {
  if (!sources.length || sources.length > maxAnalysisWorkspaceSources) {
    throw new Error("Analysis requires between 1 and 256 open source buffers. Close unused source documents and retry.");
  }
  const paths = new Set<string>();
  let decodedBytes = 0;
  let encodedBytes = Buffer.byteLength('{"schema":"nil.analysis.workspace.v1","sources":[]}', "utf8");
  const validUnicode = (text: string): boolean => {
    for (let i = 0; i < text.length; i++) {
      const unit = text.charCodeAt(i);
      if (unit >= 0xd800 && unit <= 0xdbff) {
        const trailing = text.charCodeAt(++i);
        if (!(trailing >= 0xdc00 && trailing <= 0xdfff)) return false;
      } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
    }
    return true;
  };
  for (const source of sources) {
    if (!path.isAbsolute(source.path) || !/\.(ny|matrix|mat)$/i.test(source.path) ||
        source.path.includes("\0") || !validUnicode(source.path) || Buffer.byteLength(source.path, "utf8") > 4096) {
      throw new Error("Analysis requires absolute Ny or Matrix source paths of at most 4096 UTF-8 bytes.");
    }
    if (paths.has(path.normalize(source.path))) throw new Error("Analysis source paths must be unique.");
    paths.add(path.normalize(source.path));
    if (source.text.includes("\0") || !validUnicode(source.text)) throw new Error(`Analysis source contains NUL or an unpaired Unicode surrogate: ${source.path}`);
    const bytes = Buffer.byteLength(source.text, "utf8");
    if (bytes > maxAnalysisSourceBytes) throw new Error(`Analysis source exceeds the 2 MiB buffer limit: ${source.path}`);
    decodedBytes += bytes;
    encodedBytes += Buffer.byteLength(JSON.stringify(source), "utf8") + (paths.size > 1 ? 1 : 0);
    if (decodedBytes > maxAnalysisWorkspaceBytes || encodedBytes > maxAnalysisWorkspaceBytes) {
      throw new Error("Analysis workspace exceeds the 8 MiB request limit. Close unused source documents and retry.");
    }
  }
  return JSON.stringify({ schema: "nil.analysis.workspace.v1", sources });
}

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
  sources?: Array<{ path: string; text: string }>;
  semantic?: SemanticIndex | null;
  completion?: CompilerCompletion | null;
  signature?: CompilerSignature | null;
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
  if (result.sources !== undefined) {
    if (!Array.isArray(result.sources) || Buffer.byteLength(JSON.stringify(result.sources), "utf8") > maxAnalysisSnapshotBytes) {
      throw new Error("Compiler returned invalid or oversized analysis source snapshots.");
    }
    const paths = new Set<string>();
    for (const snapshot of result.sources) {
      if (!object(snapshot) || typeof snapshot.path !== "string" || !path.isAbsolute(snapshot.path) ||
          typeof snapshot.text !== "string" || path.normalize(snapshot.path) === path.normalize(expectedSource) ||
          paths.has(path.normalize(snapshot.path))) throw new Error("Compiler returned an invalid analysis source snapshot.");
      paths.add(path.normalize(snapshot.path));
    }
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
  const semantic = parseSemanticIndex(result.semantic);
  if (semantic && !result.success) throw new Error("Failed analysis cannot publish semantic bindings.");
  parseCompilerCompletion(result.completion);
  parseCompilerSignature(result.signature);
  return result as unknown as AnalysisResult;
}

export function analysisInvocation(
  compiler: string, source: string, sdkPath: string | undefined,
  noStdlib: boolean, includeRoots: readonly string[],
  policy: { disabledRules: readonly string[]; warningsAsErrors: boolean } = { disabledRules: [], warningsAsErrors: false },
  sourceDirectoryFirst = true
): ToolInvocation {
  const args = ["--analyze", "--workspace-stdin", source, "--semantic-index"];
  if (noStdlib) args.push("--no-stdlib");
  for (const code of policy.disabledRules) args.push("--disable-rule", code);
  if (policy.warningsAsErrors) args.push("--warnings-as-errors");
  // Project checks preserve nilbuild's SDK/manifest root order. The compiler
  // itself appends the source directory, after explicitly supplied roots.
  const roots = new Set([...(sourceDirectoryFirst ? [path.dirname(source)] : []),
    ...(sdkPath ? [sdkPath] : []), ...includeRoots].map(root => path.normalize(root)));
  for (const root of roots) args.push("-I", root);
  return { command: compiler, args };
}
