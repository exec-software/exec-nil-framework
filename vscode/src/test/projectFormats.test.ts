import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { parseProjectDocument, parseProjectJson, parseProjectNildata, parseProjectYaml } from "../projectFormats";
import { findProjectFile, isProjectFile, parseProject, projectFilename, projectFilenames } from "../projectModel";

const plain = (value: unknown): unknown => JSON.parse(JSON.stringify(value));
const directory = path.resolve("format-project-tests");
const expected = { schema: "nil.project.v1", name: "hello", entry: "src/main.matrix",
  includePaths: ["lib", "two # words"], projectReferences: ["../shared/nil.project.nildata"],
  analysis: { disabledRules: ["NIL1001"], warningsAsErrors: false } };
const nildata = `# Configuration
nildata 1
object
  schema is "nil.project.v1"
  name is "hello"
  entry is "src/main.matrix"
  includePaths is an array
    item is "lib"
    item is "two # words" # quoted hash stays text
  end array
  projectReferences is an array
    item is "../shared/nil.project.nildata"
  end array
  analysis is an object
    disabledRules is an array
      item is "NIL1001"
    end array
    warningsAsErrors is false
  end object
end object
`;
const yaml = `---
schema: nil.project.v1
name: 'hello'
entry: src/main.matrix
includePaths:
  - lib
  - "two # words" # quoted hash stays text
projectReferences: ["../shared/nil.project.nildata"]
analysis:
  disabledRules:
    - NIL1001
  warningsAsErrors: false
...
`;

test("preferred NILData and legacy JSON/YAML resolve identical project configurations", () => {
  for (const [extension, text] of [["nildata", nildata], ["json", JSON.stringify(expected)], ["yaml", yaml], ["yml", yaml]]) {
    const file = path.join(directory, `nil.project.${extension}`);
    assert.deepEqual(plain(parseProjectDocument(text!, file)), expected);
    const project = parseProject(text!, file);
    assert.equal(project.name, "hello");
    assert.deepEqual(project.includePaths, [directory, path.join(directory, "lib"), path.join(directory, "two # words")]);
    assert.deepEqual(project.projectReferences, [path.resolve(directory, "../shared/nil.project.nildata")]);
    assert.equal(project.analysis.warningsAsErrors, false);
  }
  assert.deepEqual(plain(parseProjectDocument(JSON.stringify(expected), "legacy-manifest")), expected);
  assert.deepEqual(plain(parseProjectDocument(nildata, "custom.NILDATA")), expected);
});

test("project discovery prefers NILData within the nearest directory and recognizes every compatibility format", () => {
  assert.equal(projectFilename, "nil.project.nildata");
  const source = path.join(directory, "nested", "src", "main.matrix");
  const outer = path.join(directory, projectFilename);
  const available = new Set([outer, ...projectFilenames.map(name => path.join(directory, "nested", name))]);
  for (const name of projectFilenames) {
    const selected = path.join(directory, "nested", name);
    assert.equal(findProjectFile(source, directory, file => available.has(file)), selected);
    assert.equal(isProjectFile(selected), true);
    available.delete(selected);
  }
  assert.equal(findProjectFile(source, directory, file => available.has(file)), outer);
  assert.equal(isProjectFile(path.join(directory, "settings.json")), false);
});

test("project references reject invalid types and duplicate resolved paths", () => {
  for (const projectReferences of [null, "other.nildata", [1], [""], ["bad\0.nildata"],
    ["shared/nil.project.nildata", "shared/../shared/nil.project.nildata"]]) {
    assert.throws(() => parseProject(JSON.stringify({ ...expected, projectReferences }), path.join(directory, "nil.project.json")));
  }
});

test("NILData follows the shared explicit-block grammar and decoded-key uniqueness", () => {
  assert.deepEqual(plain(parseProjectNildata('nildata 1\nan object\n "unicode key" is "😀"\n empty is an array\n end array\nend object\n')), { "unicode key": "😀", empty: [] });
  assert.deepEqual(plain(parseProjectNildata('nildata 1\narray\nitem is null\nitem is -1.6\nitem is true\nend array\n')), [null, -1.6, true]);
  for (const text of ["object\nend object", "nildata 2\nobject\nend object", "nildata 1\n", "nildata 1\nobject",
    'nildata 1\nobject\nx is 1\n"\\u0078" is 2\nend object',
    'nildata 1\nobject\nx is {}\nend object', 'nildata 1\nobject\nx is []\nend object',
    'nildata 1\nobject\nx is an array\n"item" is 1\nend array\nend object',
    'nildata 1\nobject\nx is a list\nend object', 'nildata 1\nobject\nx is bare\nend object',
    'nildata 1\nobject\nx is "\\ud800"\nend object', 'nildata 1\nobject\n"x"is 1\nend object',
    'nildata 1\nobject\nx is 1\nend object\ntrue', 'nildata 1\nobject\nx is 01\nend object']) {
    assert.throws(() => parseProjectNildata(text), text);
  }
});

test("YAML compatibility handles declarative containers and rejects unsupported constructs", () => {
  const value = parseProjectYaml("text: 'can''t # stop' # comment\npath: C:\\src\\main.matrix\nurl: https://example.test/#part\nnumbers: [1,-1.6,true,null]\nobjects:\n  -\n    value: 1\n  - []\n");
  assert.deepEqual(plain(value), { text: "can't # stop", path: "C:\\src\\main.matrix", url: "https://example.test/#part", numbers: [1, -1.6, true, null], objects: [{ value: 1 }, []] });
  assert.deepEqual(plain(parseProjectYaml('{"outer": {"x": 1}}')), { outer: { x: 1 } });
  for (const text of ["x: 1\nx: 2", 'x: 1\n"\\u0078": 2', "x: &anchor foo\ny: *anchor", "x:\n  <<: source",
    "x: |\n  text", "x: >\n  text", "x: !type value", "x:\n\tvalue: true", "x: true\n  y: false",
    "x: [one, two]", "x: 'oops", 'x: "\\ud800"', 'x: "bad" extra', "x: 1\n---\nx: 2", "---\n...",
    "x:\n  - name: value", "x:\n  - one\n  other: two", "  x: true"]) {
    assert.throws(() => parseProjectYaml(text), text);
  }
});

test("YAML quotes inside plain scalars do not consume comments or mapping delimiters", () => {
  assert.deepEqual(plain(parseProjectYaml(`entry: src/it's-main.matrix # comment
other: src/a"b.matrix # comment
text: plain 'quoted' words # comment
it's-a-key: yes # comment
items:
  - it's-matrix # comment
flow: {"text":"it's # literal","nested":["also # literal",{"value":"a\\\"b"}]} # comment
`)), { entry: "src/it's-main.matrix", other: 'src/a"b.matrix', text: "plain 'quoted' words",
    "it's-a-key": "yes", items: ["it's-matrix"], flow: { text: "it's # literal", nested: ["also # literal", { value: 'a"b' }] } });
});

test("all project formats preserve special keys without modifying object prototypes", () => {
  for (const value of [parseProjectJson('{"__proto__":{"polluted":true}}'),
    parseProjectNildata('nildata 1\nobject\n__proto__ is an object\npolluted is true\nend object\nend object'),
    parseProjectYaml('__proto__:\n  polluted: true')]) {
    assert.equal(Object.getPrototypeOf(value), null);
    assert.deepEqual(plain(value), JSON.parse('{"__proto__":{"polluted":true}}'));
    assert.equal(Object.hasOwn({}, "polluted"), false);
  }
});

test("project document readers enforce byte, Unicode, depth and value budgets", () => {
  for (const parse of [parseProjectJson, parseProjectNildata, parseProjectYaml]) {
    assert.throws(() => parse(" ".repeat(1024 * 1024 + 1)), /1 MiB/);
    assert.throws(() => parse("\ud800"));
    assert.throws(() => parse("\0"));
  }
  const array = `[${Array(65535).fill("null").join(",")}]`;
  assert.equal((parseProjectJson(array) as unknown[]).length, 65535);
  assert.equal((parseProjectYaml(array) as unknown[]).length, 65535);
  for (const parse of [parseProjectJson, parseProjectYaml]) {
    assert.throws(() => parse(array.replace("]", ",null]")), /65536/);
    assert.throws(() => parse("[".repeat(33) + "0" + "]".repeat(33)), /nesting/);
  }
  assert.throws(() => parseProjectNildata("nildata 1\narray\n" + "item is null\n".repeat(65536) + "end array"), /65536/);
  assert.throws(() => parseProjectNildata("nildata 1\nobject\n" + "x is an object\n".repeat(32) + "x is 0\n" + "end object\n".repeat(33)), /nesting/);
  assert.throws(() => parseProjectYaml(Array.from({ length: 34 }, (_, i) => " ".repeat(i * 2) + "x:").join("\n")), /nesting/);
});
