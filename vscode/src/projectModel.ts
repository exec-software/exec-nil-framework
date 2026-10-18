import * as fs from "node:fs";
import * as path from "node:path";
import { ToolInvocation } from "./taskModel";

export const projectFilename = "nil.project.json";
export const maxProjectBytes = 1024 * 1024;

export interface ProjectAnalysis {
  readonly disabledRules: readonly string[];
  readonly warningsAsErrors: boolean;
}

export interface NilProject {
  readonly projectFile: string;
  readonly directory: string;
  readonly name: string;
  readonly entry: string;
  readonly includePaths: readonly string[];
  readonly sdkPath?: string;
  readonly outputDirectory: string;
  readonly analysis: ProjectAnalysis;
}

// JSON.parse accepts duplicate keys, unlike the project driver. Parse objects
// with decoded-key uniqueness so editor checks cannot use a different policy.
export function parseProjectJson(text: string): unknown {
  if (Buffer.byteLength(text, "utf8") > maxProjectBytes) throw new Error("Project file exceeds 1 MiB.");
  let position = 0;
  const fail = (message: string): never => { throw new Error(`Invalid project JSON at character ${position + 1}: ${message}`); };
  const space = () => { while (/[\x20\t\r\n]/.test(text[position] ?? "\0")) position++; };
  const string = (): string => {
    const start = position++;
    while (position < text.length) {
      const next = text[position++];
      if (next === "\\") position++;
      else if (next === '"') {
        try {
          const decoded = JSON.parse(text.slice(start, position)) as string;
          for (let i = 0; i < decoded.length; i++) {
            const code = decoded.charCodeAt(i);
            if (code >= 0xd800 && code <= 0xdbff) {
              const trailing = decoded.charCodeAt(++i);
              if (!(trailing >= 0xdc00 && trailing <= 0xdfff)) return fail("unpaired Unicode surrogate");
            } else if (code >= 0xdc00 && code <= 0xdfff) return fail("unpaired Unicode surrogate");
          }
          return decoded;
        }
        catch { return fail("invalid string"); }
      }
    }
    return fail("unterminated string");
  };
  const value = (depth: number): unknown => {
    if (depth > 32) fail("nesting exceeds 32 levels");
    space();
    const next = text[position];
    if (next === '"') return string();
    if (next === "{" || next === "[") {
      position++;
      const object = next === "{";
      const close = object ? "}" : "]";
      const record: Record<string, unknown> = Object.create(null);
      const array: unknown[] = [];
      const keys = new Set<string>();
      space();
      if (text[position] === close) { position++; return object ? record : array; }
      while (position < text.length) {
        space();
        if (object) {
          if (text[position] !== '"') fail("expected an object key");
          const key = string();
          if (keys.has(key)) fail(`duplicate key '${key}'`);
          keys.add(key);
          space();
          if (text[position++] !== ":") fail("expected ':'");
          record[key] = value(depth + 1);
        } else array.push(value(depth + 1));
        space();
        if (text[position] === close) { position++; return object ? record : array; }
        if (text[position++] !== ",") fail(`expected ',' or '${close}'`);
      }
      return fail(`expected '${close}'`);
    }
    const token = /^(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/.exec(text.slice(position))?.[0];
    if (!token) return fail("expected a JSON value");
    position += token.length;
    return JSON.parse(token) as unknown;
  };
  const result = value(0);
  space();
  if (position !== text.length) fail("unexpected trailing content");
  return result;
}

function record(value: unknown, label: string, keys: readonly string[]): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error(`${label} must be an object.`);
  for (const key of Object.keys(value)) if (!keys.includes(key)) throw new Error(`Unknown ${label} field '${key}'.`);
  return value as Record<string, unknown>;
}

function nonempty(value: unknown, label: string): string {
  if (typeof value !== "string" || value.length === 0 || value.includes("\0")) throw new Error(`${label} must be a nonempty string without NUL bytes.`);
  return value;
}

function strings(value: unknown, label: string): string[] {
  if (!Array.isArray(value)) throw new Error(`${label} must be an array of strings.`);
  return value.map(item => nonempty(item, label));
}

export function parseProject(text: string, projectFile: string): NilProject {
  const data = record(parseProjectJson(text), "project", ["schema", "name", "entry", "includePaths", "sdkPath", "outputDirectory", "analysis"]);
  if (data.schema !== "nil.project.v1") throw new Error("Project schema must be 'nil.project.v1'.");
  const name = nonempty(data.name, "Project name");
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(name) || Buffer.byteLength(name) > 128 || /\.(ny|matrix|mat)$/i.test(name)) {
    throw new Error("Project name must use letters, digits, '.', '_' or '-', start with a letter or digit, and not end in a source extension (maximum 128 bytes).");
  }
  const entry = nonempty(data.entry, "Project entry");
  if (!/\.(ny|matrix|mat)$/i.test(entry)) throw new Error("Project entry must be a Ny (.ny) or Matrix (.matrix, .mat) source file.");
  const file = path.resolve(projectFile);
  const directory = path.dirname(file);
  const resolve = (value: unknown, label: string) => path.resolve(directory, nonempty(value, label));
  const analysis = data.analysis === undefined ? {} : record(data.analysis, "analysis", ["disabledRules", "warningsAsErrors"]);
  const disabledRules = analysis.disabledRules === undefined ? [] : strings(analysis.disabledRules, "analysis.disabledRules");
  if (disabledRules.some(code => !/^NIL[0-9]{4}$/.test(code)) || new Set(disabledRules).size !== disabledRules.length) {
    throw new Error("analysis.disabledRules must contain distinct NIL rule codes (for example NIL1001).");
  }
  if (analysis.warningsAsErrors !== undefined && typeof analysis.warningsAsErrors !== "boolean") throw new Error("analysis.warningsAsErrors must be boolean.");
  return {
    projectFile: file, directory, name, entry: path.resolve(directory, entry),
    includePaths: [...new Set([directory, ...(data.includePaths === undefined ? [] : strings(data.includePaths, "includePaths").map(root => path.resolve(directory, root)))])],
    ...(data.sdkPath === undefined ? {} : { sdkPath: resolve(data.sdkPath, "sdkPath") }),
    outputDirectory: data.outputDirectory === undefined ? path.join(directory, "build") : resolve(data.outputDirectory, "outputDirectory"),
    analysis: { disabledRules, warningsAsErrors: analysis.warningsAsErrors === true }
  };
}

export function pathWithin(file: string, directory: string): boolean {
  const relative = path.relative(path.resolve(directory), path.resolve(file));
  return relative === "" || (!path.isAbsolute(relative) && relative !== ".." && !relative.startsWith(`..${path.sep}`));
}

export function findProjectFile(source: string, workspace: string, exists: (file: string) => boolean = fs.existsSync): string | undefined {
  if (!pathWithin(source, workspace)) return undefined;
  const boundary = path.resolve(workspace);
  let directory = path.dirname(path.resolve(source));
  while (pathWithin(directory, boundary)) {
    const file = path.join(directory, projectFilename);
    if (exists(file)) return file;
    if (directory === boundary) break;
    directory = path.dirname(directory);
  }
  return undefined;
}

export function readProject(file: string): NilProject {
  const descriptor = fs.openSync(file, fs.constants.O_RDONLY | (fs.constants.O_NONBLOCK ?? 0));
  try {
    const stat = fs.fstatSync(descriptor);
    if (!stat.isFile()) throw new Error("Project path must be a regular file.");
    if (stat.size > maxProjectBytes) throw new Error("Project file exceeds 1 MiB.");
    const bytes = Buffer.alloc(maxProjectBytes + 1);
    let count = 0;
    while (count < bytes.length) {
      const read = fs.readSync(descriptor, bytes, count, bytes.length - count, count);
      if (read === 0) break;
      count += read;
    }
    if (count > maxProjectBytes) throw new Error("Project file exceeds 1 MiB.");
    const text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes.subarray(0, count));
    return parseProject(text, file);
  } finally { fs.closeSync(descriptor); }
}

export type ProjectAction = "check" | "build" | "run";
export type ProjectTarget = "vm" | "native";

export function projectInvocation(options: {
  builder: string; projectFile: string; action: ProjectAction; target: ProjectTarget;
  sdkPath?: string; compiler: string; interpreter?: string; programArgs?: readonly string[];
}): ToolInvocation {
  const args = [options.action, "--project", options.projectFile, "--target", options.target];
  if (options.sdkPath) args.push("--sdk", options.sdkPath);
  args.push("--compiler", options.compiler);
  if (options.interpreter) args.push("--interpreter", options.interpreter);
  if (options.action === "run" && options.programArgs?.length) args.push("--", ...options.programArgs);
  return { command: options.builder, args };
}
