import * as path from "node:path";

/** Source extensions select the compiler frontend, independently of editor mode. */
export type SourceLanguage = "ny" | "matrix";

export interface SourceDocument {
  readonly languageId: string;
  readonly uri: { readonly scheme: string; readonly fsPath: string };
}

export const languageDocumentSelector = [
  { language: "ny", scheme: "file" },
  { language: "ny", scheme: "untitled" },
  { language: "matrix", scheme: "file" },
  { language: "matrix", scheme: "untitled" }
];

export function sourceLanguage(source: string): SourceLanguage | undefined {
  // Keep this in step with nilcompiler's extension-based frontend selection.
  if (/\.(matrix|mat)$/i.test(source)) return "matrix";
  if (/\.ny$/i.test(source)) return "ny";
  return undefined;
}

export function sourceLabel(source: string): string {
  return sourceLanguage(source) === "matrix" ? "Matrix" : "Ny";
}

export function savedSourcePath(document: SourceDocument | undefined): string | undefined {
  if (!document || document.uri.scheme !== "file") return undefined;
  const language = sourceLanguage(document.uri.fsPath);
  return language && document.languageId === language ? document.uri.fsPath : undefined;
}

/** Disk-backed commands are unaffected by unrelated untitled scratch buffers. */
export function isDirtySavedSource(document: SourceDocument & { readonly isDirty: boolean }): boolean {
  return document.isDirty && savedSourcePath(document) !== undefined;
}

/** Named untitled files already have an import identity, without a disk write. */
export function analysisSourcePath(document: SourceDocument | undefined): string | undefined {
  if (!document || !["file", "untitled"].includes(document.uri.scheme) || !path.isAbsolute(document.uri.fsPath)) return undefined;
  const language = sourceLanguage(document.uri.fsPath);
  return language && document.languageId === language ? document.uri.fsPath : undefined;
}

export function taskSourcePath(
  source: string | undefined,
  activeDocument: SourceDocument | undefined
): string | undefined {
  if (!source || source === "${file}") return savedSourcePath(activeDocument);
  // Explicit sources need no open editor. Preserve VS Code workspace/input
  // variables in their directories while requiring a compiler source suffix.
  return sourceLanguage(source) ? source : undefined;
}

export function debugProgramKind(program: string): "source" | "nil" | undefined {
  if (sourceLanguage(program)) return "source";
  return /\.nil$/i.test(program) ? "nil" : undefined;
}

// Imported modules may use either frontend, so every compilation observes both
// diagnostic forms regardless of the entry file's extension.
export const compilerProblemMatchers = ["$nilcompiler", "$matrixcompiler"];
