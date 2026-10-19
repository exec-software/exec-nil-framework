import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

import {
  compileInvocation,
  CompileMode,
  defaultArtifactPath,
  interpreterInvocation
} from "./taskModel";
import { projectForResource } from "./projectContext";
import { projectInvocation, ProjectTarget } from "./projectModel";
import { expandConfiguredPath, resolveCommandOnPath } from "./toolchain";
import { compilerProblemMatchers, savedSourcePath, sourceLabel, taskSourcePath } from "./languages";
import {
  configuredSdkPath,
  nyConfiguration,
  resolveConfiguredTool
} from "./vscodeToolchain";

export type NyTaskKind = "compile" | "run" | "native" | "project-check" | "project-build" | "project-run" | "build-sdk" | "ctest";

export interface NyTaskDefinition extends vscode.TaskDefinition {
  readonly type: "ny";
  readonly task: NyTaskKind;
  readonly source?: string;
  readonly project?: string;
  readonly target?: ProjectTarget;
  readonly output?: string;
  readonly sdkPath?: string;
  readonly noStdlib?: boolean;
  readonly args?: readonly string[];
  readonly programArgs?: readonly string[];
  readonly buildDirectory?: string;
}

function taskScope(resource?: vscode.Uri): vscode.WorkspaceFolder | vscode.TaskScope {
  return (
    (resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined) ??
    vscode.workspace.workspaceFolders?.[0] ??
    vscode.TaskScope.Workspace
  );
}

function workspaceRoot(resource?: vscode.Uri): string | undefined {
  return (
    (resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined)?.uri.fsPath ??
    vscode.workspace.workspaceFolders?.[0]?.uri.fsPath
  );
}

function resourceForSource(source: string | undefined): vscode.Uri | undefined {
  if (!source || source.includes("${")) {
    return vscode.window.activeTextEditor?.document.uri;
  }
  return vscode.Uri.file(source);
}

function configuredOutputDirectory(resource?: vscode.Uri): string | undefined {
  const raw = nyConfiguration(resource).get<string>("outputDirectory", "").trim();
  if (!raw) {
    return undefined;
  }
  return expandConfiguredPath(raw, workspaceRoot(resource));
}

function presentation(task: vscode.Task): vscode.Task {
  task.presentationOptions = {
    reveal: vscode.TaskRevealKind.Always,
    panel: vscode.TaskPanelKind.Dedicated,
    clear: true
  };
  task.runOptions = { reevaluateOnRerun: true };
  return task;
}

export class NyTaskProvider implements vscode.TaskProvider {
  public provideTasks(): vscode.Task[] {
    if (!vscode.workspace.isTrusted) {
      return [];
    }
    const tasks: vscode.Task[] = [];
    const activeDocument = vscode.window.activeTextEditor?.document;
    const source = savedSourcePath(activeDocument);

    if (source) {
      for (const kind of ["compile", "run", "native"] as const) {
        const task = this.createTask({ type: "ny", task: kind, source });
        if (task) {
          tasks.push(task);
        }
      }
    }
    for (const target of ["vm", "native"] as const) {
      for (const kind of ["project-check", "project-build", "project-run"] as const) {
        const task = this.createTask({ type: "ny", task: kind, target, source });
        if (task) tasks.push(task);
      }
    }
    for (const kind of ["build-sdk", "ctest"] as const) {
      const task = this.createTask({ type: "ny", task: kind });
      if (task) {
        tasks.push(task);
      }
    }
    return tasks;
  }

  public resolveTask(task: vscode.Task): vscode.Task | undefined {
    return this.createTask(task.definition as NyTaskDefinition);
  }

  public createTask(definition: NyTaskDefinition): vscode.Task | undefined {
    if (!vscode.workspace.isTrusted) {
      return undefined;
    }
    if (definition.task.startsWith("project-")) return this.createProjectTask(definition);
    if (definition.task === "build-sdk") {
      return this.createBuildSdkTask(definition);
    }
    if (definition.task === "ctest") {
      return this.createCtestTask(definition);
    }

    const source = taskSourcePath(definition.source, vscode.window.activeTextEditor?.document);
    // An explicit NIL artifact can be run without an editor. Only compilation
    // and deriving a default output require a source document.
    if (!source && !(definition.task === "run" && definition.output && !definition.source)) return undefined;
    const resource = resourceForSource(source ?? definition.output);
    const mode: CompileMode = definition.task === "native" ? "native" : "nil";
    const output =
      definition.output ??
      defaultArtifactPath(source!, mode, configuredOutputDirectory(resource));
    const scope = taskScope(resource);
    const contextPath = source ?? output;
    const cwd = workspaceRoot(resource) ?? (contextPath.includes("${") ? undefined : path.dirname(contextPath));

    if (definition.task === "run") {
      const interpreter = resolveConfiguredTool("nilinterpreter", resource);
      if (!interpreter.executable) {
        return undefined;
      }
      const invocation = interpreterInvocation(
        interpreter.executable,
        output,
        definition.args,
        definition.programArgs
      );
      const task = new vscode.Task(
        { ...definition, source, output },
        scope,
        `Run ${path.basename(output)} with nilinterpreter`,
        "NIL Framework",
        new vscode.ShellExecution(invocation.command, [...invocation.args], { cwd })
      );
      task.detail = "Run an existing NIL artifact with nilinterpreter. Compile tasks create the artifact.";
      return presentation(task);
    }

    if (!source) return undefined;

    const compiler = resolveConfiguredTool("nilcompiler", resource);
    if (!compiler.executable) {
      return undefined;
    }
    const configuration = nyConfiguration(resource);
    const sdkPath = definition.sdkPath
      ? expandConfiguredPath(definition.sdkPath, workspaceRoot(resource))
      : configuredSdkPath(resource);
    const baseArgs = configuration.get<readonly string[]>("compiler.extraArgs", []);
    const nativeArgs =
      definition.task === "native"
        ? configuration.get<readonly string[]>("native.extraArgs", [])
        : [];
    const invocation = compileInvocation({
      compiler: compiler.executable,
      source,
      output,
      mode,
      sdkPath,
      noStdlib: definition.noStdlib ?? configuration.get<boolean>("compiler.noStdlib", false),
      extraArgs: [...baseArgs, ...nativeArgs, ...(definition.args ?? [])]
    });
    const language = sourceLabel(source);
    const title = definition.task === "native" ? `Compile ${language} (native)` : `Compile ${language} to NIL`;
    const task = new vscode.Task(
      { ...definition, source, output, sdkPath },
      scope,
      `${title}: ${path.basename(source)}`,
      "NIL Framework",
      new vscode.ShellExecution(invocation.command, [...invocation.args], { cwd }),
      compilerProblemMatchers
    );
    task.group = vscode.TaskGroup.Build;
    task.detail =
      definition.task === "native"
        ? "Compile through the optional LLVM backend."
        : "Compile to a separate NIL artifact with --no-bundle.";
    return presentation(task);
  }

  private createProjectTask(definition: NyTaskDefinition): vscode.Task | undefined {
    const resource = resourceForSource(definition.source);
    let project;
    try { project = projectForResource(resource, definition.project); }
    catch (error) {
      void vscode.window.showErrorMessage(`NIL project: ${error instanceof Error ? error.message : String(error)}`);
      return undefined;
    }
    if (!project) return undefined;
    const configuredSdk = configuredSdkPath(resource);
    const sdk = definition.sdkPath
      ? path.resolve(project.directory, expandConfiguredPath(definition.sdkPath, workspaceRoot(resource)))
      : project.sdkPath ?? (configuredSdk ? path.resolve(workspaceRoot(resource) ?? project.directory, configuredSdk) : undefined);
    const builder = resolveConfiguredTool("nilbuild", resource, sdk);
    const compiler = resolveConfiguredTool("nilcompiler", resource, sdk);
    const interpreter = resolveConfiguredTool("nilinterpreter", resource, sdk);
    const target = definition.target ?? "vm";
    if (target !== "vm" && target !== "native") return undefined;
    if (!builder.executable || !compiler.executable || (target === "vm" && definition.task === "project-run" && !interpreter.executable)) return undefined;
    const action = definition.task === "project-check" ? "check" : definition.task === "project-build" ? "build" : "run";
    const invocation = projectInvocation({ builder: builder.executable, compiler: compiler.executable,
      interpreter: interpreter.executable, sdkPath: sdk, projectFile: project.projectFile,
      action, target, programArgs: definition.programArgs });
    const task = new vscode.Task({ ...definition, project: project.projectFile, target }, taskScope(resource),
      `${action.charAt(0).toUpperCase()}${action.slice(1)} ${project.name} (${target})`, "NIL Framework",
      new vscode.ProcessExecution(invocation.command, [...invocation.args], { cwd: project.directory }),
      ["$nilproject", ...compilerProblemMatchers]);
    if (action === "build") task.group = vscode.TaskGroup.Build;
    if (action === "check") task.group = vscode.TaskGroup.Test;
    task.detail = `${project.projectFile} — ${action === "run" ? "check, build, and run" : action} with nilbuild.`;
    return presentation(task);
  }

  private createBuildSdkTask(definition: NyTaskDefinition): vscode.Task | undefined {
    const root = workspaceRoot();
    if (!root) {
      return undefined;
    }
    const script = path.join(root, "scripts", "build-sdk.sh");
    if (!fs.existsSync(script)) {
      return undefined;
    }
    const execution =
      process.platform === "win32"
        ? new vscode.ShellExecution("bash", [script], { cwd: root })
        : new vscode.ShellExecution(script, [], { cwd: root });
    const task = new vscode.Task(
      definition,
      taskScope(),
      "Build and install NIL Framework SDK",
      "NIL Framework",
      execution
    );
    task.group = vscode.TaskGroup.Build;
    task.detail = "Run scripts/build-sdk.sh for the current NIL Framework repository.";
    return presentation(task);
  }

  private createCtestTask(definition: NyTaskDefinition): vscode.Task | undefined {
    const root = workspaceRoot();
    if (!root) {
      return undefined;
    }
    const rawBuildDirectory =
      definition.buildDirectory ?? nyConfiguration().get<string>("buildDirectory", "build/cmake");
    const expanded = expandConfiguredPath(rawBuildDirectory, root);
    const buildDirectory = path.isAbsolute(expanded) ? expanded : path.resolve(root, expanded);
    const ctest = resolveCommandOnPath("ctest") ?? "ctest";
    const task = new vscode.Task(
      { ...definition, buildDirectory },
      taskScope(),
      "Run NIL Framework CTest suite",
      "NIL Framework",
      new vscode.ShellExecution(
        ctest,
        ["--test-dir", buildDirectory, "--output-on-failure", ...(definition.args ?? [])],
        { cwd: root }
      )
    );
    task.group = vscode.TaskGroup.Test;
    task.detail = "Run the repository's configured CTest suite.";
    return presentation(task);
  }
}
