import * as path from "node:path";

export const maxProjectBytes = 1024 * 1024;

// JSON.parse accepts duplicate keys, unlike the project driver. Parse objects
// with decoded-key uniqueness so editor checks cannot use a different policy.
export function parseProjectJson(text: string): unknown {
  if (Buffer.byteLength(text, "utf8") > maxProjectBytes) throw new Error("Project file exceeds 1 MiB.");
  let position = 0;
  let nodes = 0;
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
    if (++nodes > 65536) fail("document exceeds 65536 values");
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

function checkSource(text: string): void {
  if (Buffer.byteLength(text, "utf8") > maxProjectBytes) throw new Error("Project file exceeds 1 MiB.");
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code >= 0xd800 && code <= 0xdbff) {
      const trailing = text.charCodeAt(++i);
      if (!(trailing >= 0xdc00 && trailing <= 0xdfff)) throw new Error("Project file requires valid Unicode.");
    } else if (code >= 0xdc00 && code <= 0xdfff) throw new Error("Project file requires valid Unicode.");
  }
}

const trim = (text: string): string => text.replace(/^[ \t\r]+|[ \t\r]+$/g, "");
interface Line { text: string; number: number; indent: number; }

function treeBudget() {
  let nodes = 0;
  const node = (depth: number) => {
    if (depth > 32) throw new Error("Project document nesting exceeds 32 levels.");
    if (++nodes > 65536) throw new Error("Project document exceeds 65536 values.");
  };
  const tree = (value: unknown, depth: number): void => {
    node(depth);
    if (value && typeof value === "object") for (const item of Object.values(value)) tree(item, depth + 1);
  };
  return { node, tree };
}

/** Version-one NILData, matching the shared compiler/runtime reader. */
export function parseProjectNildata(text: string): unknown {
  checkSource(text);
  if (text.includes("\0")) throw new Error("NILData input contains raw NUL; use a JSON Unicode escape.");
  const lines = text.split("\n").map((input, index) => {
    let quoted = false, escaped = false;
    for (let i = 0; i < input.length; i++) {
      const c = input[i];
      if (escaped) { escaped = false; continue; }
      if (quoted && c === "\\") { escaped = true; continue; }
      if (c === '"') quoted = !quoted;
      if (!quoted && c === "#") { input = input.slice(0, i); break; }
    }
    return { text: trim(input), number: index + 1 };
  }).filter(line => line.text !== "");
  let position = 0;
  const budget = treeBudget();
  const fail = (message: string, number = lines[position]?.number ?? (lines.at(-1)?.number ?? 0) + 1): never => {
    throw new Error(`${message} at NILData line ${number}`);
  };
  const scalar = (input: string, number: number): unknown => {
    let parsed: unknown;
    try { parsed = parseProjectJson(input); }
    catch (error) { return fail(error instanceof Error ? error.message : String(error), number); }
    if (parsed !== null && typeof parsed === "object") fail("Containers require 'an object' or 'an array' blocks", number);
    return parsed;
  };
  const member = (line: { text: string; number: number }): [string, string] => {
    let end = 0, key: string;
    if (line.text.startsWith('"')) {
      let escaped = false, closed = false;
      for (end = 1; end < line.text.length; end++) {
        const c = line.text[end];
        if (escaped) { escaped = false; continue; }
        if (c === "\\") { escaped = true; continue; }
        if (c === '"') { end++; closed = true; break; }
      }
      if (!closed) fail("Unterminated quoted member name", line.number);
      key = scalar(line.text.slice(0, end), line.number) as string;
    } else {
      end = line.text.search(/[ \t]/);
      if (end < 0) fail("Expected member 'name is value'", line.number);
      key = line.text.slice(0, end);
      if (!/^[A-Za-z_][A-Za-z_0-9]*$/.test(key)) fail("Member names must be ASCII identifiers or quoted text", line.number);
    }
    if (!/[ \t]/.test(line.text[end] ?? "\0")) fail("Expected whitespace before 'is'", line.number);
    const rest = trim(line.text.slice(end));
    if (!/^is[ \t]/.test(rest)) fail("Expected 'is' followed by a value", line.number);
    const value = trim(rest.slice(3));
    if (!value) fail("Expected a value after 'is'", line.number);
    return [key, value];
  };
  const value = (input: string, depth: number, opening: number, root = false): unknown => {
    budget.node(depth);
    const object = input === "an object" || (root && input === "object");
    const array = input === "an array" || (root && input === "array");
    if (!object && !array) return scalar(input, opening);
    const record: Record<string, unknown> = Object.create(null);
    const items: unknown[] = [];
    const close = object ? "end object" : "end array";
    while (true) {
      const current = lines[position++];
      if (!current) return fail(`Missing '${close}' for block opened`, opening);
      if (current.text === close) return object ? record : items;
      const [key, input] = member(current);
      if (object) {
        if (Object.hasOwn(record, key)) fail(`Duplicate member '${key}'`, current.number);
        record[key] = value(input, depth + 1, current.number);
      } else {
        if (key !== "item" || current.text.startsWith('"')) fail("Array entries must use 'item is value'", current.number);
        items.push(value(input, depth + 1, current.number));
      }
    }
  };
  if (lines[position++]?.text !== "nildata 1") fail("Expected 'nildata 1' header", lines[0]?.number ?? 1);
  const root = lines[position++];
  if (!root) return fail("Expected one root value");
  const result = value(root.text, 0, root.number, true);
  if (position !== lines.length) fail("Unexpected trailing NILData value or token");
  return result;
}

// Compatibility subset deliberately mirrors nilbuild/project_format.hpp. It
// does not invoke a compiler, load tags, or resolve aliases in untrusted files.
export function parseProjectYaml(text: string): unknown {
  checkSource(text);
  if (/[\x00-\x08\x0b\x0c\x0e-\x1f]/.test(text)) throw new Error("YAML input contains an unescaped control character.");
  const delimiter = (input: string, wanted: "#" | ":"): number => {
    let quoted = "", escaped = false;
    let tokenStart = true, flowDepth = 0;
    for (let i = 0; i < input.length; i++) {
      const c = input[i]!;
      if (escaped) { escaped = false; continue; }
      if (quoted === '"' && c === "\\") { escaped = true; continue; }
      if (quoted) {
        if (c === quoted) {
          if (quoted === "'" && input[i + 1] === "'") i++;
          else quoted = "";
        }
        continue;
      }
      if ((c === '"' || c === "'") && tokenStart) { quoted = c; tokenStart = false; continue; }
      const followedBySpace = i + 1 === input.length || /[ \t]/.test(input[i + 1]!);
      if (c === wanted) {
        if (wanted === "#" && (i === 0 || /[ \t]/.test(input[i - 1]!))) return i;
        if (wanted === ":" && followedBySpace) return i;
      }
      if (c === " " || c === "\t") continue;
      if ((c === "[" || c === "{") && tokenStart) { flowDepth++; tokenStart = true; }
      else if ((c === "]" || c === "}") && flowDepth) { flowDepth--; tokenStart = false; }
      else if ((c === ":" && (flowDepth || followedBySpace)) || (c === "," && flowDepth) ||
               (c === "-" && tokenStart && followedBySpace)) tokenStart = true;
      else tokenStart = false;
    }
    return -1;
  };
  const fail = (message: string, number: number): never => { throw new Error(`${message} at YAML line ${number}`); };
  const lines: Line[] = [];
  text.split("\n").forEach((input, index) => {
    const comment = delimiter(input, "#");
    if (comment >= 0) input = input.slice(0, comment);
    const indent = input.search(/[^ ]/);
    if (input[indent] === "\t") fail("Tabs are not allowed for indentation", index + 1);
    const content = trim(input);
    if (content) lines.push({ text: content, indent, number: index + 1 });
  });
  let position = 0;
  const budget = treeBudget();
  const scalar = (input: string, number: number): unknown => {
    if (!input) return null;
    if (/^["[{]/.test(input)) {
      try { return parseProjectJson(input); }
      catch (error) { return fail(`Quoted scalars and flow containers require JSON syntax: ${error instanceof Error ? error.message : String(error)}`, number); }
    }
    if (input.startsWith("'")) {
      let result = "";
      for (let i = 1; i < input.length; i++) {
        if (input[i] !== "'") { result += input[i]!; continue; }
        if (i + 1 === input.length) return result;
        if (input[i + 1] === "'") { result += "'"; i++; continue; }
        fail("Unexpected text after quoted scalar", number);
      }
      fail("Unterminated single-quoted scalar", number);
    }
    if (input === "true" || input === "false") return input === "true";
    if (input === "null" || input === "~") return null;
    if (input === "---" || input === "..." || /^[%!&*|>@`\]},?:]/.test(input) || input === "-" || input.startsWith("- "))
      fail("Unsupported YAML syntax; quote literal text or use NILData", number);
    if (delimiter(input, ":") >= 0) fail("Plain scalars containing ': ' must be quoted", number);
    if (/^(?:-|[0-9])/.test(input)) {
      try { const result = parseProjectJson(input); if (typeof result === "number") return result; }
      catch { /* Non-JSON numbers, versions and filenames remain text. */ }
    }
    return input;
  };
  const sequence = (input: string): boolean => /^-(?:[ \t]|$)/.test(input);
  const key = (input: string, number: number): string => {
    if (!input) fail("Expected a nonempty mapping key", number);
    let result = input;
    if (/^["']/.test(input)) {
      const parsed = scalar(input, number);
      if (typeof parsed !== "string") fail("Mapping keys must be text", number);
      result = parsed as string;
    } else if (/^[&*![{?%@`]/.test(input)) fail("Unsupported YAML mapping key; quote literal text", number);
    if (result === "<<") fail("YAML merge keys are not supported", number);
    return result;
  };
  const nested = (input: string, depth: number, opening: Line): unknown => {
    if (!input && position < lines.length && lines[position]!.indent > opening.indent) return block(depth, lines[position]!.indent);
    const result = scalar(input, opening.number);
    budget.tree(result, depth);
    return result;
  };
  const block = (depth: number, indent: number): unknown => {
    const opening = lines[position]!;
    const list = sequence(opening.text);
    if (!list && (/^[{[]/.test(opening.text) || delimiter(opening.text, ":") < 0)) {
      position++;
      const result = scalar(opening.text, opening.number);
      budget.tree(result, depth);
      return result;
    }
    budget.node(depth);
    const record: Record<string, unknown> = Object.create(null);
    const items: unknown[] = [];
    while (position < lines.length && lines[position]!.indent >= indent) {
      const current = lines[position++]!;
      if (current.indent !== indent) fail("Unexpected indentation", current.number);
      if (list) {
        if (!sequence(current.text)) fail("Expected sequence item '-'", current.number);
        items.push(nested(trim(current.text.slice(1)), depth + 1, current));
      } else {
        if (sequence(current.text)) fail("Cannot mix mapping entries and sequence items", current.number);
        const split = delimiter(current.text, ":");
        if (split < 0) fail("Expected 'key: value' mapping entry", current.number);
        const name = key(trim(current.text.slice(0, split)), current.number);
        if (Object.hasOwn(record, name)) fail(`Duplicate YAML field: ${name}`, current.number);
        record[name] = nested(trim(current.text.slice(split + 1)), depth + 1, current);
      }
    }
    return list ? items : record;
  };
  if (lines[0]?.text === "---" && lines[0].indent === 0) position++;
  if (lines.at(-1)?.text === "..." && lines.at(-1)?.indent === 0) lines.pop();
  if (position === lines.length) throw new Error("Expected one YAML document.");
  if (lines[position]!.indent) fail("The root value must start at column one", lines[position]!.number);
  const result = block(0, 0);
  if (position !== lines.length) fail("Unexpected trailing YAML content", lines[position]!.number);
  return result;
}

export function parseProjectDocument(text: string, filename: string): unknown {
  const extension = path.extname(filename).toLowerCase();
  if (extension === ".nildata") return parseProjectNildata(text);
  if (extension === ".yaml" || extension === ".yml") return parseProjectYaml(text);
  return parseProjectJson(text);
}
