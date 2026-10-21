import * as path from "node:path";
import { createByteRangeMapper } from "./sourceOffsets";
import { SemanticSource } from "./semanticModel";

export interface CompilerSignature {
  schema: "nil.signature.v1";
  partial: true;
  path: string;
  cursor: number;
  start: number;
  length: number;
  sources: SemanticSource[];
  label: string;
  parameters: Array<{ name: string; type: string; label: string; variadic: boolean }>;
  active_parameter: number | null;
  return_type: string;
}

const record = (value: unknown): value is Record<string, unknown> => typeof value === "object" && value !== null && !Array.isArray(value);
const integer = (value: unknown): value is number => typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
const text = (value: unknown): value is string => typeof value === "string" && !value.includes("\0") && Buffer.from(value, "utf8").toString("utf8") === value;
const singleLine = (value: unknown): value is string => text(value) && !/[\r\n]/.test(value);

/** A signature is a separate query result and may accompany invalid arguments. */
export function parseCompilerSignature(value: unknown): CompilerSignature | undefined {
  if (value === undefined || value === null) return undefined;
  const fail = (): never => { throw new Error("Compiler returned an invalid nil.signature.v1 result."); };
  if (!record(value) || value.schema !== "nil.signature.v1" || value.partial !== true ||
      !text(value.path) || !path.isAbsolute(value.path) || !integer(value.cursor) || !integer(value.start) ||
      !integer(value.length) || !Number.isSafeInteger(value.start + value.length) ||
      value.cursor < value.start || value.cursor > value.start + value.length ||
      !singleLine(value.label) || !value.label || !singleLine(value.return_type) ||
      !Array.isArray(value.sources) || !value.sources.length || value.sources.length > 256 ||
      !Array.isArray(value.parameters) || value.parameters.length > 256 ||
      !(value.active_parameter === null || (integer(value.active_parameter) && value.active_parameter < value.parameters.length)) ||
      Buffer.byteLength(JSON.stringify(value), "utf8") > 4 * 1024 * 1024) return fail();
  const sources = new Map<string, string>();
  for (const source of value.sources) {
    if (!record(source) || !text(source.path) || !path.isAbsolute(source.path) || !text(source.text) ||
        sources.has(path.normalize(source.path))) return fail();
    sources.set(path.normalize(source.path), source.text);
  }
  const primary = sources.get(path.normalize(value.path));
  if (primary === undefined) return fail();
  const map = createByteRangeMapper(primary);
  if (!map(value.start, value.length) || !map(value.cursor, 0)) return fail();
  for (let index = 0; index < value.parameters.length; ++index) {
    const parameter = value.parameters[index];
    if (!record(parameter) || !singleLine(parameter.name) || !parameter.name || !singleLine(parameter.type) ||
        !singleLine(parameter.label) || !parameter.label || !value.label.includes(parameter.label) ||
        typeof parameter.variadic !== "boolean" || (parameter.variadic && index !== value.parameters.length - 1)) return fail();
  }
  return value as unknown as CompilerSignature;
}
