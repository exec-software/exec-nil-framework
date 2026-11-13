const assert = require("node:assert/strict");
const test = require("node:test");
const { createTaskEvidence, inspectProcess } = require("./task-evidence.cjs");
const fixture = () => ({ task: { definition: { task: "project-build" }, execution: {
  process: "/SDK λ/nilbuild", args: ["build", "--project", "/private/work/project/nil.project.json"] } } });

test("task evidence associates dispatch, lifecycle and process events without changing completion", async () => {
  const evidence = createTaskEvidence(), execution = fixture();
  const dispatch = evidence.dispatch("ny.buildProject");
  evidence.event("task-start", execution);
  evidence.event("process-start", execution, { processId: 123 });
  evidence.event("process-end", execution, { exitCode: 0 });
  evidence.event("task-end", execution); evidence.complete(dispatch, 0);
  assert.equal(evidence.report.tasks.length, 1);
  assert.equal(evidence.report.tasks[0].processId, 123);
  assert.equal(evidence.report.tasks[0].ended, true);
  assert.ok(evidence.report.events.every(row => row.id === "task-1"));
  assert.equal(dispatch.completed, true);
  await evidence.failure("late", [execution], "/private/work", "/tmp", "darwin", async () => assert.fail("ended process sampled"));
  assert.deepEqual(evidence.report.failureSnapshots[0].processes, []);
});

test("failure evidence distinguishes dispatch-only, task-created and child-started", async () => {
  const evidence = createTaskEvidence(), unstarted = fixture(), started = fixture();
  evidence.dispatch("ny.buildProject");
  evidence.event("task-start", unstarted);
  evidence.event("process-start", started, { processId: 321 });
  const inspected = [];
  await evidence.failure("timeout", [unstarted, started], "/private/work", "/tmp", "darwin", async task => {
    inspected.push(task.processId); return { processId: task.processId };
  });
  assert.deepEqual(inspected, [321]);
  assert.deepEqual(evidence.report.failureSnapshots[0].active, ["task-1", "task-2"]);
  assert.equal(evidence.report.dispatches[0].completed, false);
});

test("a process-end suppresses sampling before the task-end notification arrives", async () => {
  const evidence = createTaskEvidence(), execution = fixture();
  evidence.event("process-start", execution, { processId: 123 });
  evidence.event("process-end", execution, { exitCode: 0 });
  assert.equal(evidence.report.tasks[0].ended, false);
  assert.equal(evidence.report.tasks[0].processEnded, true);
  await evidence.failure("waiting for task end", [execution], "/private/work", "/tmp", "darwin",
    async () => assert.fail("completed child sampled"));
  assert.deepEqual(evidence.report.failureSnapshots[0].processes, []);
});

test("diagnostics refuse an unowned PID or mismatched process before sampling", async () => {
  let calls = 0;
  const task = { processId: 123, command: "/SDK λ/nilbuild", args: ["build", "/private/work/project/file"] };
  const run = async (command, args) => {
    calls++; assert.equal(command, "/bin/ps");
    return { stdout: args.at(-1) === "command=" ? "/other/tool /private/work/project/file" : "123 1 1 0:00 0 S 1:00" };
  };
  assert.equal((await inspectProcess(task, "/private/work", "/uncreated/output", run)).sampled, false);
  assert.equal(calls, 2);
  await inspectProcess({ ...task, processId: -1 }, "/private/work", "/uncreated/output", async () => assert.fail());
  await inspectProcess({ ...task, args: ["/other/work/file"] }, "/private/work", "/uncreated/output", async () => assert.fail());
});

test("matching owned task receives only bounded read-only process diagnostics", async () => {
  const task = { processId: 123, command: "/SDK λ/nilbuild", args: ["build", "/private/work/project/file"] };
  const calls = [];
  const result = await inspectProcess(task, "/private/work", "/no-existing-task-evidence-123/sample.txt", async (command, args, options) => {
    calls.push({ command, args }); assert.ok(options.timeout > 0 && options.timeout <= 6000);
    return { stdout: args.at(-1) === "command=" ? "/SDK λ/nilbuild build /private/work/project/file" : "metrics", stderr: "" };
  });
  assert.equal(result.sampled, true);
  assert.deepEqual(calls.at(-1), { command: "/usr/bin/sample", args: ["123", "1", "1", "-file", "/no-existing-task-evidence-123/sample.txt"] });
});
