import assert from "node:assert/strict";
import * as path from "node:path";
import test from "node:test";
import { parseTestDiscovery, parseTestResults } from "../testingModel";

const entry = { id: "test#1", name: "a Unicode check: λ", suite: "numbers", kind: "unit",
  function: "niltest_case_1", file: path.resolve("example.matrix"), line: 7 };
const discovery = (tests: unknown[]) => JSON.stringify({ schema: "nil.tests.v1", tests });
const result = { ...entry, route: "vm", status: "passed", durationMs: 1.25, message: "", stdout: "λ\n", stderr: "" };
const report = (tests: unknown[], errors: unknown[] = []) => JSON.stringify({ schema: "nil.test.results.v1", tests, errors });

test("niltest discovery retains compiler identity, Unicode labels, and source lines", () => {
  assert.deepEqual(parseTestDiscovery(discovery([entry])), [entry]);
  assert.deepEqual(parseTestDiscovery(discovery([{ ...entry, file: "", line: 0 }]))[0]?.line, 0);
});

test("niltest discovery rejects duplicate identities and invalid locations", () => {
  assert.throws(() => parseTestDiscovery(discovery([entry, entry])), /duplicate/);
  assert.throws(() => parseTestDiscovery(discovery([{ ...entry, file: "relative.matrix" }])), /source path/);
  assert.throws(() => parseTestDiscovery(discovery([{ ...entry, line: -1 }])), /source line/);
  assert.throws(() => parseTestDiscovery(discovery([{ ...entry, line: 1.5 }])), /source line/);
  assert.throws(() => parseTestDiscovery('{"schema":"future","tests":[]}'), /nil.tests.v1/);
});

test("niltest report separates printed test output from protocol status", () => {
  const failing = { ...result, status: "failed", stdout: '{"status":"passed"}', stderr: "expected 2; got 3" };
  assert.deepEqual(parseTestResults(report([failing])), { tests: [failing], errors: [] });
  assert.deepEqual(parseTestResults(report([], ["compiler unavailable"])), { tests: [], errors: ["compiler unavailable"] });
  assert.throws(() => parseTestResults(report([result, result])), /duplicate/);
  assert.equal(parseTestResults(report([result, { ...result, route: "native" }])).tests.length, 2);
  assert.equal(parseTestResults(report([{ ...result, stdout: "binary\0output" }])).tests[0]?.stdout, "binary\0output");
});

test("niltest report rejects malformed status and duration instead of passing a test", () => {
  for (const patch of [{ status: "ok" }, { route: "unknown" }, { durationMs: -1 }, { durationMs: "2" }]) {
    assert.throws(() => parseTestResults(report([{ ...result, ...patch }])), /status, route, or duration/);
  }
  assert.throws(() => parseTestResults(report([result], [false])), /error/);
  const incomplete = { ...result, status: "failed", message: "test returned without completing the harness" };
  assert.equal(parseTestResults(report([incomplete])).tests[0]?.message, incomplete.message);
  assert.throws(() => parseTestResults("program output\n" + report([result])));
});
