import * as path from "node:path";
import { createByteRangeMapper, TextOffsets } from "./sourceOffsets";

export interface SemanticSource { path: string; text: string; }
export interface SemanticLocation { path: string; start: number; length: number; }
export interface SemanticSymbol extends SemanticLocation {
  id: number; kind: string; name: string; type: string;
  references_complete?: true; rename_safe?: true;
}
export interface SemanticReference extends SemanticLocation { symbol_id: number; }
export interface RenameSourcePolicy {
  path: string; language: "ny" | "matrix"; identifier_keys: string[]; reserved_keys: string[];
}
export interface SemanticIndex {
  schema: "nil.semantic.v1";
  partial: true;
  sources: SemanticSource[];
  symbols: SemanticSymbol[];
  references: SemanticReference[];
  rename_policy?: { schema: "nil.rename.v1"; sources: RenameSourcePolicy[] };
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
        !validText(symbol.kind) || !symbol.kind || !validText(symbol.name) || !symbol.name || !validText(symbol.type) ||
        (symbol.references_complete !== undefined && symbol.references_complete !== true) ||
        (symbol.rename_safe !== undefined && (symbol.rename_safe !== true || symbol.references_complete !== true)) ||
        (symbol.references_complete === true && symbol.kind !== "variable")) return fail();
    symbols.add(symbol.id);
  }
  for (const reference of value.references) {
    if (!record(reference) || !identifier(reference.symbol_id) || !symbols.has(reference.symbol_id) || !location(reference)) return fail();
  }
  if (value.rename_policy !== undefined) {
    const policy = value.rename_policy;
    if (!record(policy) || policy.schema !== "nil.rename.v1" || !Array.isArray(policy.sources) || policy.sources.length > 256) return fail();
    const paths = new Set<string>();
    let identifierCount = 0;
    for (const source of policy.sources) {
      if (!record(source) || !validText(source.path) || !sources.has(path.normalize(source.path)) || paths.has(path.normalize(source.path)) ||
          (source.language !== "ny" && source.language !== "matrix")) return fail();
      paths.add(path.normalize(source.path));
      for (const keys of [source.identifier_keys, source.reserved_keys]) {
        if (!Array.isArray(keys) || keys.length > 131072 || keys.some(key => !validText(key) || !key) || new Set(keys).size !== keys.length) return fail();
      }
      identifierCount += (source.identifier_keys as string[]).length;
      if ((source.identifier_keys as string[]).length > 32768 || identifierCount > 131072) return fail();
    }
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

/** Completeness is a compiler capability, never inferred from the number of hits. */
export function referencesAt(index: SemanticIndex, source: string, offset: number, includeDeclaration: boolean): SemanticLocation[] | undefined {
  const match = definitionAt(index, source, offset);
  if (!match?.target.references_complete) return undefined;
  const locations: SemanticLocation[] = index.references.filter(item => item.symbol_id === match.target.id);
  if (includeDeclaration) locations.push(match.target);
  const unique = new Map<string, SemanticLocation>();
  for (const location of locations) {
    if (path.normalize(location.path) !== path.normalize(match.target.path)) return undefined;
    unique.set(JSON.stringify([path.normalize(location.path), location.start, location.length]), location);
  }
  return [...unique.values()].sort((a, b) => a.start - b.start);
}

export interface RenamePlan {
  target: SemanticSymbol;
  language: "ny" | "matrix";
  newName: string;
  text: string;
  edits: Array<SemanticLocation & { offsets: TextOffsets }>;
}

function nameKey(name: string, language: "ny" | "matrix"): string {
  return language === "matrix" ? name.replace(/[-_]/g, "").replace(/[A-Z]/g, letter => letter.toLowerCase()) : name;
}

export function planRename(index: SemanticIndex, source: string, offset: number, newName: string): RenamePlan | undefined {
  const match = definitionAt(index, source, offset);
  if (!match?.target.rename_safe) return undefined;
  const policy = index.rename_policy?.sources.find(item => path.normalize(item.path) === path.normalize(match.target.path));
  if (!policy) return undefined;
  const validName = policy.language === "matrix" ? /^[_A-Za-z][_A-Za-z0-9]*(?:-[A-Za-z][_A-Za-z0-9]*)*$/ : /^[_A-Za-z][_A-Za-z0-9]*$/;
  if (newName.length > 256 || !validName.test(newName)) return undefined;
  const key = nameKey(newName, policy.language);
  if (!key || policy.identifier_keys.includes(key) || policy.reserved_keys.includes(key)) return undefined;
  const locations = referencesAt(index, source, offset, true);
  if (!locations?.length) return undefined;
  const bytes = Buffer.from(match.targetText, "utf8");
  const map = createByteRangeMapper(match.targetText);
  const pieces: Buffer[] = [];
  const edits: RenamePlan["edits"] = [];
  let end = 0;
  for (const location of locations) {
    const offsets = map(location.start, location.length);
    if (!offsets || location.start < end) return undefined;
    pieces.push(bytes.subarray(end, location.start), Buffer.from(newName, "utf8"));
    edits.push({ ...location, offsets });
    end = location.start + location.length;
  }
  pieces.push(bytes.subarray(end));
  return { target: match.target, language: policy.language, newName, text: Buffer.concat(pieces).toString("utf8"), edits };
}

/** Check the entire retained graph after speculative checking, independent of snapshot-local IDs. */
export function renamePreservesBindings(before: SemanticIndex, after: SemanticIndex, plan: RenamePlan): boolean {
  const normalized = (value: string): string => path.normalize(value);
  const targetPath = normalized(plan.target.path);
  const transform = (location: SemanticLocation): SemanticLocation | undefined => {
    if (normalized(location.path) !== targetPath) return location;
    let delta = 0;
    for (const edit of plan.edits) {
      if (location.start + location.length <= edit.start) break;
      if (location.start === edit.start && location.length === edit.length) {
        return { path: location.path, start: location.start + delta, length: plan.newName.length };
      }
      if (location.start < edit.start + edit.length) return undefined;
      delta += plan.newName.length - edit.length;
    }
    return { ...location, start: location.start + delta };
  };
  const identity = (item: SemanticLocation): string => JSON.stringify([normalized(item.path), item.start, item.length]);
  if (before.sources.length !== after.sources.length || before.symbols.length !== after.symbols.length || before.references.length !== after.references.length) return false;
  for (const source of before.sources) {
    const expected = normalized(source.path) === targetPath ? plan.text : source.text;
    if (after.sources.find(item => normalized(item.path) === normalized(source.path))?.text !== expected) return false;
  }
  const updated = new Map(after.symbols.map(item => [identity(item), item]));
  const mapped = new Map<number, number>();
  for (const symbol of before.symbols) {
    const location = transform(symbol);
    const next = location && updated.get(identity(location));
    if (!next || next.kind !== symbol.kind || next.type !== symbol.type) return false;
    if (symbol.id === plan.target.id) {
      if (!next.rename_safe || nameKey(next.name, plan.language) !== nameKey(plan.newName, plan.language)) return false;
    } else if (next.name !== symbol.name) return false;
    mapped.set(symbol.id, next.id);
  }
  if (new Set(mapped.values()).size !== before.symbols.length) return false;
  const actual = new Set(after.references.map(item => JSON.stringify([identity(item), item.symbol_id])));
  for (const reference of before.references) {
    const location = transform(reference);
    if (!location || !actual.has(JSON.stringify([identity(location), mapped.get(reference.symbol_id)]))) return false;
  }
  return actual.size === before.references.length;
}
