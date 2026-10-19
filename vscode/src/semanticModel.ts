import * as path from "node:path";
import { createByteRangeMapper, TextOffsets } from "./sourceOffsets";

export interface SemanticSource { path: string; text: string; }
export interface SemanticLocation { path: string; start: number; length: number; }
export interface SemanticSymbol extends SemanticLocation { id: number; kind: string; name: string; type: string; }
export interface SemanticReference extends SemanticLocation { symbol_id: number; }
export interface SemanticIndex {
  schema: "nil.semantic.v1";
  partial: true;
  sources: SemanticSource[];
  symbols: SemanticSymbol[];
  references: SemanticReference[];
}

const record = (value: unknown): value is Record<string, unknown> => typeof value === "object" && value !== null && !Array.isArray(value);
const identifier = (value: unknown): value is number => typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
const validText = (value: unknown): value is string => typeof value === "string" && !value.includes("\0") && Buffer.from(value, "utf8").toString("utf8") === value;

export function parseSemanticIndex(value: unknown): SemanticIndex | undefined {
  if (value === null || value === undefined) return undefined;
  const fail = (): never => { throw new Error("Compiler returned an invalid nil.semantic.v1 index."); };
  if (!record(value) || value.schema !== "nil.semantic.v1" || value.partial !== true ||
      !Array.isArray(value.sources) || value.sources.length > 256 ||
      !Array.isArray(value.symbols) || value.symbols.length > 32768 ||
      !Array.isArray(value.references) || value.references.length > 131072 ||
      Buffer.byteLength(JSON.stringify(value), "utf8") > 4 * 1024 * 1024) return fail();
  const sources = new Map<string, ReturnType<typeof createByteRangeMapper>>();
  for (const source of value.sources) {
    if (!record(source) || !validText(source.path) || !path.isAbsolute(source.path) ||
        !validText(source.text) || sources.has(path.normalize(source.path))) return fail();
    sources.set(path.normalize(source.path), createByteRangeMapper(source.text));
  }
  const location = (item: Record<string, unknown>): boolean => validText(item.path) && identifier(item.start) &&
    identifier(item.length) && item.length > 0 && sources.get(path.normalize(item.path))?.(item.start, item.length) !== undefined;
  const symbols = new Set<number>();
  for (const symbol of value.symbols) {
    if (!record(symbol) || !identifier(symbol.id) || symbols.has(symbol.id) || !location(symbol) ||
        !validText(symbol.kind) || !symbol.kind || !validText(symbol.name) || !symbol.name || !validText(symbol.type)) return fail();
    symbols.add(symbol.id);
  }
  for (const reference of value.references) {
    if (!record(reference) || !identifier(reference.symbol_id) || !symbols.has(reference.symbol_id) || !location(reference)) return fail();
  }
  return value as unknown as SemanticIndex;
}

export interface DefinitionMatch {
  origin: TextOffsets;
  target: SemanticSymbol;
  targetText: string;
  targetOffsets: TextOffsets;
}

/** Only resolved compiler occurrences participate; a matching name is not a binding. */
export function definitionAt(index: SemanticIndex, source: string, offset: number): DefinitionMatch | undefined {
  const normalized = path.normalize(source);
  const text = index.sources.find(item => path.normalize(item.path) === normalized)?.text;
  if (text === undefined || !identifier(offset) || offset > text.length) return undefined;
  const map = createByteRangeMapper(text);
  const matches: Array<{ id: number; offsets: TextOffsets }> = [];
  const add = (item: SemanticLocation, id: number): void => {
    if (path.normalize(item.path) !== normalized) return;
    const offsets = map(item.start, item.length);
    if (offsets && offsets.start <= offset && offset <= offsets.end) matches.push({ id, offsets });
  };
  for (const reference of index.references) add(reference, reference.symbol_id);
  for (const symbol of index.symbols) add(symbol, symbol.id);
  if (!matches.length || new Set(matches.map(item => item.id)).size !== 1) return undefined;
  const match = matches[0]!;
  const target = index.symbols.find(item => item.id === match.id)!;
  const targetText = index.sources.find(item => path.normalize(item.path) === path.normalize(target.path))!.text;
  const targetOffsets = createByteRangeMapper(targetText)(target.start, target.length);
  return targetOffsets ? { origin: match.offsets, target, targetText, targetOffsets } : undefined;
}
