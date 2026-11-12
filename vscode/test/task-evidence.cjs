// Test-only lifecycle evidence. This never starts, stops or retries a NIL task.
const path = require("node:path");
const fs = require("node:fs/promises");
const { execFile } = require("node:child_process");
const { promisify } = require("node:util");
const execute = promisify(execFile);

async function inspectProcess(task, workspace, output, run = execute) {
  const evidence = { processId: task.processId, expectedCommand: task.command, expectedArgs: task.args };
  if (!Number.isSafeInteger(task.processId) || task.processId <= 0 || !path.isAbsolute(task.command || "") ||
      !task.args?.some(value => typeof value === "string" && value.startsWith(workspace + path.sep))) {
    return { ...evidence, sampled: false, reason: "No owned workspace task process declaration" };
  }
  const options = { timeout: 2500, maxBuffer: 64 * 1024, encoding: "utf8" };
  try {
    evidence.metrics = (await run("/bin/ps", ["-ww", "-p", String(task.processId), "-o",
      "pid=,ppid=,pgid=,time=,%cpu=,stat=,etime="], options)).stdout;
    evidence.command = (await run("/bin/ps", ["-ww", "-p", String(task.processId), "-o", "command="], options)).stdout.trim();
    // A task PID may already have exited. Reject a PID whose observed command
    // no longer matches this run's executable and private workspace arguments.
    // The command observation and subsequent sample are not atomic.
    if (!evidence.command.startsWith(task.command + " ") ||
        !evidence.command.includes(workspace + path.sep) || !task.args.every(value => evidence.command.includes(value))) {
      return { ...evidence, sampled: false, reason: "Observed process does not match the owned task" };
    }
    try { await fs.access(output); return { ...evidence, sampled: false, reason: "Sample path already exists" }; }
    catch (error) { if (error.code !== "ENOENT") throw error; }
    const sample = await run("/usr/bin/sample", [String(task.processId), "1", "1", "-file", output],
      { timeout: 6000, maxBuffer: 256 * 1024, encoding: "utf8" });
    return { ...evidence, sampled: true, sample: path.basename(output), stdout: sample.stdout, stderr: sample.stderr };
  } catch (error) {
    return { ...evidence, sampled: false, error: String(error), stdout: error.stdout, stderr: error.stderr };
  }
}

function createTaskEvidence() {
  const events = [], tasks = [], dispatches = [], identities = new Map();
  const stamp = () => ({ at: new Date().toISOString(), milliseconds: Date.now() });
  const identify = execution => {
    let task = identities.get(execution);
    if (!task) {
      const process = execution.task.execution;
      task = { id: `task-${tasks.length + 1}`, definition: { ...execution.task.definition },
        command: process?.process || process?.command, args: process?.args, ended: false };
      identities.set(execution, task); tasks.push(task);
    }
    return task;
  };
  const event = (kind, execution, extra = {}) => {
    const task = identify(execution);
    events.push({ kind, id: task.id, ...stamp(), ...extra });
    if (kind === "process-start") task.processId = extra.processId;
    if (kind === "process-end") { task.processEnded = true; task.exitCode = extra.exitCode; }
    if (kind === "task-end") task.ended = true;
  };
  return {
    report: { events, tasks, dispatches, failureSnapshots: [] },
    event,
    dispatch(command) {
      const row = { command, ...stamp(), completed: false }; dispatches.push(row); return row;
    },
    complete(row, exitCode) { row.completed = true; row.exitCode = exitCode; row.elapsedMilliseconds = Date.now() - row.milliseconds; },
    async failure(reason, activeExecutions, workspace, directory, platform = process.platform, inspect = inspectProcess) {
      const active = activeExecutions.map(identify);
      const snapshot = { reason, ...stamp(), active: active.map(task => task.id), processes: [] };
      this.report.failureSnapshots.push(snapshot);
      if (platform !== "darwin") return;
      // Bounded to the three first observed active processes, never enumerate
      // unrelated processes or diagnose a task from a completed execution.
      for (const task of active.filter(task => !task.ended && !task.processEnded && task.processId).slice(0, 3)) {
        snapshot.processes.push(await inspect(task, workspace, path.join(directory, `${task.id}-sample.txt`)));
      }
    }
  };
}

module.exports = { createTaskEvidence, inspectProcess };
