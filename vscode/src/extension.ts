import * as vscode from "vscode";
import { randomUUID } from "node:crypto";

import { NyLanguageClientManager } from "./languageClient";
import { NyDebugIntegration } from "./debugger";
import { NyAnalyzer } from "./analyzer";
import { NyTaskDefinition, NyTaskKind, NyTaskProvider } from "./tasks";
import { selectSdkFolder } from "./vscodeToolchain";
import { projectForResource } from "./projectContext";
import { ProjectTarget } from "./projectModel";
import { savedSourcePath } from "./languages";

let languageClientManager: NyLanguageClientManager | undefined;

async function requireTrustedWorkspace(): Promise<boolean> {
  if (vscode.workspace.isTrusted) {
    return true;
  }
  const action = await vscode.window.showWarningMessage(
    "Trust this workspace before running NIL Framework toolchain executables.",
    "Manage Workspace Trust"
  );
  if (action === "Manage Workspace Trust") {
    await vscode.commands.executeCommand("workbench.trust.manage");
  }
  return false;
}

async function currentSource(): Promise<string | undefined> {
  const document = vscode.window.activeTextEditor?.document;
  const source = savedSourcePath(document);
  if (!document || !source) {
    void vscode.window.showErrorMessage("Open a saved Ny (.ny) or Matrix (.matrix, .mat) file in its matching language mode before running this command.");
    return undefined;
  }
  if (document.isDirty && !(await document.save())) {
    void vscode.window.showErrorMessage("Save the current source file before compiling it.");
    return undefined;
  }
  return source;
}

async function executeAndWait(task: vscode.Task): Promise<number | undefined> {
  const execution = await vscode.tasks.executeTask(task);
  return await new Promise<number | undefined>((resolve) => {
    let settled = false;
    const finish = (exitCode: number | undefined): void => {
      if (settled) {
        return;
      }
      settled = true;
      processSubscription.dispose();
      taskSubscription.dispose();
      resolve(exitCode);
    };
    const processSubscription = vscode.tasks.onDidEndTaskProcess((event) => {
      if (event.execution === execution) {
        finish(event.exitCode);
      }
    });
    const taskSubscription = vscode.tasks.onDidEndTask((event) => {
      if (event.execution === execution) {
        finish(undefined);
      }
    });
  });
}

async function runToolTask(
  provider: NyTaskProvider,
  kind: NyTaskKind,
  source?: string,
  overrides: Partial<NyTaskDefinition> = {}
): Promise<number | undefined> {
  if (!(await requireTrustedWorkspace())) {
    return undefined;
  }
  const definition: NyTaskDefinition = {
    type: "ny",
    task: kind,
    ...(source ? { source } : {}),
    ...overrides
  };
  const task = provider.createTask(definition);
  if (!task) {
    const action = await vscode.window.showErrorMessage(
      `Unable to create the NIL Framework ${kind} task. Check the NIL Framework SDK and tool path settings.`,
      "Select SDK",
      "Open Settings"
    );
    if (action === "Select SDK") {
      await selectSdkFolder();
    } else if (action === "Open Settings") {
      await vscode.commands.executeCommand("workbench.action.openSettings", "@ext:exec-software.ny-language-support");
    }
    return undefined;
  }
  return await executeAndWait(task);
}

async function runProjectTask(provider: NyTaskProvider, kind: "project-check" | "project-build" | "project-run", target: ProjectTarget): Promise<number | undefined> {
  if (!(await requireTrustedWorkspace())) return undefined;
  const resource = vscode.window.activeTextEditor?.document.uri;
  let project;
  try { project = projectForResource(resource); }
  catch (error) { void vscode.window.showErrorMessage(`NIL project: ${error instanceof Error ? error.message : String(error)}`); return undefined; }
  if (!project) {
    void vscode.window.showErrorMessage("No nil.project.nildata (or legacy JSON/YAML project) found between the active file and its workspace folder.");
    return undefined;
  }
  // The project driver reads the complete saved import graph. Saving all files
  // avoids silently building a different program from the one in the editor.
  if (!(await vscode.workspace.saveAll(false))) return undefined;
  return runToolTask(provider, kind, resource?.scheme === "file" ? resource.fsPath : undefined,
    { project: project.projectFile, target });
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const provider = new NyTaskProvider();
  const debugIntegration = new NyDebugIntegration();
  const analyzer = new NyAnalyzer();
  languageClientManager = new NyLanguageClientManager(document => analyzer.usesCompilerDefinitions(document),
    (document, position, token) => analyzer.provideCompletionItems(document, position, token));

  context.subscriptions.push(
    languageClientManager,
    analyzer,
    debugIntegration,
    vscode.debug.registerDebugConfigurationProvider("ny", debugIntegration),
    vscode.debug.registerDebugAdapterDescriptorFactory("ny", debugIntegration),
    vscode.debug.onDidTerminateDebugSession((session) => { void debugIntegration.endSession(session); }),
    vscode.tasks.registerTaskProvider("ny", provider),
    vscode.commands.registerCommand("ny.restartLanguageServer", async () => {
      await languageClientManager?.restart();
    }),
    vscode.commands.registerCommand("ny.showLanguageServerOutput", () => {
      languageClientManager?.showOutput();
    }),
    vscode.commands.registerCommand("ny.selectSdk", async () => {
      await selectSdkFolder();
    }),
    vscode.commands.registerCommand("ny.compileCurrentFile", async () => {
      const source = await currentSource();
      if (source) {
        await runToolTask(provider, "compile", source);
      }
    }),
    vscode.commands.registerCommand("ny.compileNativeCurrentFile", async () => {
      const source = await currentSource();
      if (source) {
        await runToolTask(provider, "native", source);
      }
    }),
    ...([ ["ny.profileMemory", "profile-memory"], ["ny.profileNativeMemory", "profile-memory-native"] ] as const)
      .map(([command, kind]) => vscode.commands.registerCommand(command, async () => {
        if (!(await requireTrustedWorkspace())) return undefined;
        const source = await currentSource();
        if (!source) return undefined;
        const directory = vscode.Uri.joinPath(context.storageUri ?? context.globalStorageUri, "memory-profiles");
        await vscode.workspace.fs.createDirectory(directory);
        const report = vscode.Uri.joinPath(directory, `${Date.now()}-${randomUUID()}.json`);
        await runToolTask(provider, kind, source, { output: report.fsPath });
        try {
          await vscode.workspace.fs.stat(report);
          await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(report), { preview: false });
          return report;
        } catch {
          return undefined;
        }
      })),
    vscode.commands.registerCommand("ny.runCurrentFile", async () => {
      const source = await currentSource();
      if (!source) {
        return;
      }
      const compileTask = provider.createTask({ type: "ny", task: "compile", source });
      if (!compileTask) {
        await runToolTask(provider, "compile", source);
        return;
      }
      if (!(await requireTrustedWorkspace())) {
        return;
      }
      const compileExit = await executeAndWait(compileTask);
      if (compileExit !== 0) {
        if (compileExit === undefined) {
          void vscode.window.showWarningMessage("NIL compilation ended without a process exit code; the program was not run.");
        }
        return;
      }
      const output = (compileTask.definition as NyTaskDefinition).output;
      await runToolTask(provider, "run", source, output ? { output } : {});
    }),
    ...([ ["ny.checkProject", "project-check", "vm"], ["ny.checkNativeProject", "project-check", "native"],
      ["ny.buildProject", "project-build", "vm"], ["ny.buildNativeProject", "project-build", "native"],
      ["ny.runProject", "project-run", "vm"], ["ny.runNativeProject", "project-run", "native"] ] as const)
      .map(([command, kind, target]) => vscode.commands.registerCommand(command, () => runProjectTask(provider, kind, target))),
    vscode.commands.registerCommand("ny.buildSdk", async () => {
      await runToolTask(provider, "build-sdk");
    }),
    vscode.commands.registerCommand("ny.runCtest", async () => {
      await runToolTask(provider, "ctest");
    }),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (
        event.affectsConfiguration("ny.languageServer.enabled") ||
        event.affectsConfiguration("ny.languageServer.diagnostics.enabled") ||
        event.affectsConfiguration("ny.server.path") ||
        event.affectsConfiguration("ny.sdkPath")
      ) {
        void languageClientManager?.restart();
      }
    }),
    vscode.workspace.onDidGrantWorkspaceTrust(() => {
      void languageClientManager?.start(true);
    })
  );

  // Activation should not block syntax support while a native server starts.
  void languageClientManager.start(true);
}

export async function deactivate(): Promise<void> {
  await languageClientManager?.stop();
  languageClientManager = undefined;
}
