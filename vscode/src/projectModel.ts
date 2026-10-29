import * as fs from "node:fs";
import * as path from "node:path";
import { ToolInvocation } from "./taskModel";
import { maxProjectBytes, parseProjectDocument } from "./projectFormats";
export { maxProjectBytes, parseProjectJson } from "./projectFormats";

export const projectFilename = "nil.project.nildata";
export const projectFilenames = [projectFilename, "nil.project.json", "nil.project.yaml", "nil.project.yml"] as const;
export function isProjectFile(file: string): boolean {
  return projectFilenames.some(name => path.basename(file) === name);
}

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
  readonly projectReferences: readonly string[];
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
  const data = record(parseProjectDocument(text, projectFile), "project", ["schema", "name", "entry", "includePaths", "sdkPath", "outputDirectory", "analysis", "projectReferences"]);
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
  const projectReferences = (data.projectReferences === undefined ? [] : strings(data.projectReferences, "projectReferences")).map(reference => path.resolve(directory, reference));
  if (new Set(projectReferences).size !== projectReferences.length) throw new Error("projectReferences must contain distinct project paths.");
  return {
    projectFile: file, directory, name, entry: path.resolve(directory, entry),
    includePaths: [...new Set([directory, ...(data.includePaths === undefined ? [] : strings(data.includePaths, "includePaths").map(root => path.resolve(directory, root)))])],
    ...(data.sdkPath === undefined ? {} : { sdkPath: resolve(data.sdkPath, "sdkPath") }),
    outputDirectory: data.outputDirectory === undefined ? path.join(directory, "build") : resolve(data.outputDirectory, "outputDirectory"),
    analysis: { disabledRules, warningsAsErrors: analysis.warningsAsErrors === true },
    projectReferences
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
    for (const name of projectFilenames) {
      const file = path.join(directory, name);
      if (exists(file)) return file;
    }
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
