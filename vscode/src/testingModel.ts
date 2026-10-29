import * as path from "node:path";

export interface NilTestCase {
  readonly id: string;
  readonly name: string;
  readonly suite: string;
  readonly kind: string;
  readonly function: string;
  readonly file: string;
  readonly line: number;
}

export interface NilTestResult extends NilTestCase {
  readonly route: "vm" | "native";
  readonly status: "passed" | "failed" | "timeout" | "cancelled" | "error";
  readonly durationMs: number;
  readonly message: string;
  readonly stdout: string;
  readonly stderr: string;
}

function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Invalid niltest record.");
  return value as Record<string, unknown>;
}

function text(value: unknown, label: string, allowNul = false): string {
  if (typeof value !== "string" || (!allowNul && value.includes("\0"))) throw new Error(`Invalid niltest ${label}.`);
  return value;
}

function testCase(value: unknown): NilTestCase {
  const data = record(value);
  const id = text(data.id, "id");
  const file = text(data.file, "file");
  if (!id || id.length > 4096 || (file && !path.isAbsolute(file))) throw new Error("Invalid niltest identity or source path.");
  if (!Number.isSafeInteger(data.line) || (data.line as number) < 0) throw new Error("Invalid niltest source line.");
  return { id, file, line: data.line as number, name: text(data.name, "name"),
    suite: text(data.suite, "suite"), kind: text(data.kind, "kind"), function: text(data.function, "function") };
}

function document(source: string, schema: string): Record<string, unknown> {
  if (Buffer.byteLength(source) > 32 * 1024 * 1024) throw new Error("niltest report exceeds 32 MiB.");
  const data = record(JSON.parse(source));
  if (data.schema !== schema || !Array.isArray(data.tests) || data.tests.length > 100_000) {
    throw new Error(`Expected ${schema} from niltest. Update the configured SDK if necessary.`);
  }
  return data;
}

export function parseTestDiscovery(source: string): NilTestCase[] {
  const data = document(source, "nil.tests.v1");
  const cases = (data.tests as unknown[]).map(testCase);
  if (new Set(cases.map(item => item.id)).size !== cases.length) throw new Error("niltest returned duplicate test IDs.");
  return cases;
}

export function parseTestResults(source: string): { tests: NilTestResult[]; errors: string[] } {
  const data = document(source, "nil.test.results.v1");
  const tests = (data.tests as unknown[]).map(value => {
    const item = record(value);
    const metadata = testCase(value);
    if ((item.route !== "vm" && item.route !== "native") ||
        !["passed", "failed", "timeout", "cancelled", "error"].includes(String(item.status)) ||
        typeof item.durationMs !== "number" || !Number.isFinite(item.durationMs) || item.durationMs < 0) {
      throw new Error("Invalid niltest result status, route, or duration.");
    }
    return { ...metadata, route: item.route, status: item.status, durationMs: item.durationMs,
      message: text(item.message, "message"), stdout: text(item.stdout, "stdout", true), stderr: text(item.stderr, "stderr", true) } as NilTestResult;
  });
  if (new Set(tests.map(item => JSON.stringify([item.id, item.route]))).size !== tests.length) {
    throw new Error("niltest returned duplicate results.");
  }
  if (!Array.isArray(data.errors)) throw new Error("Invalid niltest errors.");
  return { tests, errors: data.errors.map(error => text(error, "error")) };
}
