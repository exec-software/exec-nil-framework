import * as path from "node:path";
import { createByteRangeMapper } from "./sourceOffsets";
import { SemanticSource } from "./semanticModel";

export interface CompilerCompletion {
  schema: "nil.completion.v1";
  partial: true;
  path: string;
  cursor: number;
  start: number;
  length: number;
  sources: SemanticSource[];
  items: Array<{ name: string; kind: "function" | "variable" | "parameter"; type: string; insert_text: string }>;
}

const record = (value: unknown): value is Record<string, unknown> => typeof value === "object" && value !== null && !Array.isArray(value);
const integer = (value: unknown): value is number => typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
const text = (value: unknown): value is string => typeof value === "string" && !value.includes("\0") && Buffer.from(value, "utf8").toString("utf8") === value;

/** Completion is its own query result; unresolved prefixes need not pass type checking. */
export function parseCompilerCompletion(value: unknown): CompilerCompletion | undefined {
  if (value === undefined || value === null) return undefined;
  const fail = (): never => { throw new Error("Compiler returned an invalid nil.completion.v1 result."); };
  if (!record(value) || value.schema !== "nil.completion.v1" || value.partial !== true ||
      !text(value.path) || !path.isAbsolute(value.path) || !integer(value.cursor) || !integer(value.start) ||
      !integer(value.length) || !value.length || value.cursor < value.start || value.cursor > value.start + value.length ||
      !Array.isArray(value.sources) || !value.sources.length || value.sources.length > 256 ||
      !Array.isArray(value.items) || value.items.length > 4096 ||
      Buffer.byteLength(JSON.stringify(value), "utf8") > 4 * 1024 * 1024) return fail();
  const sources = new Map<string, string>();
  for (const source of value.sources) {
    if (!record(source) || !text(source.path) || !path.isAbsolute(source.path) || !text(source.text) || sources.has(path.normalize(source.path))) return fail();
    sources.set(path.normalize(source.path), source.text);
  }
  const primary = sources.get(path.normalize(value.path));
  if (primary === undefined) return fail();
  const map = createByteRangeMapper(primary);
  if (!map(value.start, value.length) || !map(value.cursor, 0)) return fail();
  const insertions = new Set<string>();
  for (const item of value.items) {
    if (!record(item) || !text(item.name) || !item.name || !text(item.type) || !text(item.insert_text) || !item.insert_text ||
        (item.kind !== "function" && item.kind !== "variable" && item.kind !== "parameter") ||
        /[\r\n]/.test(item.name + item.insert_text) || insertions.has(item.insert_text)) return fail();
    insertions.add(item.insert_text);
  }
  return value as unknown as CompilerCompletion;
}

/** Avoid sending an offset in the middle of a UTF-16 surrogate pair. */
export function completionByteOffset(source: string, offset: number): number | undefined {
  if (!integer(offset) || offset > source.length) return undefined;
  if (offset > 0 && offset < source.length && /[\uD800-\uDBFF]/.test(source[offset - 1]!) && /[\uDC00-\uDFFF]/.test(source[offset]!)) return undefined;
  return Buffer.byteLength(source.slice(0, offset), "utf8");
}
