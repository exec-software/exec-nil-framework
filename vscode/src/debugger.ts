import { execFile } from "node:child_process";
import * as fs from "node:fs/promises";
import * as os from "node:os";
import * as path from "node:path";
import { promisify } from "node:util";
import * as vscode from "vscode";

import { compileInvocation } from "./taskModel";
import { debugProgramKind, savedSourcePath, sourceLabel } from "./languages";
import { configuredSdkPath, nyConfiguration, resolveConfiguredTool, toolNotFoundMessage } from "./vscodeToolchain";

const executeFile = promisify(execFile);

export class NyDebugIntegration implements vscode.DebugConfigurationProvider,
  vscode.DebugAdapterDescriptorFactory, vscode.Disposable {
  private readonly directories = new Set<string>();

  resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    configuration: vscode.DebugConfiguration
  ): vscode.DebugConfiguration | undefined {
    if (!vscode.workspace.isTrusted) {
      void vscode.window.showErrorMessage("Trust this workspace before launching the NIL debugger.");
      return undefined;
    }
    const source = savedSourcePath(vscode.window.activeTextEditor?.document);
    if (!configuration.type && source) {
      configuration = { type: "ny", request: "launch", name: `Debug ${sourceLabel(source)} file`, program: source, stopOnEntry: true };
    }
    return configuration;
  }

  async resolveDebugConfigurationWithSubstitutedVariables(
    folder: vscode.WorkspaceFolder | undefined,
    configuration: vscode.DebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    if (!vscode.workspace.isTrusted) {
      void vscode.window.showErrorMessage("Trust this workspace before launching the NIL debugger.");
      return undefined;
    }
    let directory: string | undefined;
    try {
      if (configuration.request !== "launch" || typeof configuration.program !== "string" || !configuration.program) {
        throw new Error("NIL debugging requires a launch configuration with Ny (.ny), Matrix (.matrix, .mat), or a --debug-info .nil program.");
      }
      const program = path.resolve(folder?.uri.fsPath ?? process.cwd(), configuration.program);
      const programKind = debugProgramKind(program);
      if (!programKind) {
        throw new Error("NIL source debugging accepts .ny, .matrix, .mat, and .nil files, not native executables.");
      }
      if (configuration.args !== undefined && (!Array.isArray(configuration.args) ||
          !configuration.args.every((argument: unknown) => typeof argument === "string"))) {
        throw new Error("NIL debugger launch args must be an array of strings.");
      }
      const resource = vscode.Uri.file(program);
      const debuggerTool = resolveConfiguredTool("nildebugger", resource);
      if (!debuggerTool.executable) throw new Error(toolNotFoundMessage("nildebugger", debuggerTool));
      configuration.program = program;
      configuration.cwd ??= folder?.uri.fsPath ?? path.dirname(program);
      configuration.__nyDebugger = debuggerTool.executable;
      if (programKind === "source") {
        const document = vscode.workspace.textDocuments.find((item) => item.uri.fsPath === program);
        if (document?.isDirty && !(await document.save())) throw new Error("Save the source file before debugging.");
        const compiler = resolveConfiguredTool("nilcompiler", resource);
        if (!compiler.executable) throw new Error(toolNotFoundMessage("nilcompiler", compiler));
        directory = await fs.mkdtemp(path.join(os.tmpdir(), "ny-debug-"));
        this.directories.add(directory);
        const output = path.join(directory, "program.nil");
        const invocation = compileInvocation({
          compiler: compiler.executable,
          source: program,
          output,
          mode: "nil",
          sdkPath: configuredSdkPath(resource),
          noStdlib: nyConfiguration(resource).get<boolean>("compiler.noStdlib", false),
          debugInfo: true
        });
        // Argument vectors only: source filenames and SDK settings never pass
        // through a shell. A failed compile never launches stale bytecode.
        await executeFile(invocation.command, [...invocation.args], {
          cwd: configuration.cwd,
          timeout: 120_000,
          maxBuffer: 4 * 1024 * 1024,
          windowsHide: true
        });
        configuration.program = output;
        configuration.__nyDebugDirectory = directory;
      }
      return configuration;
    } catch (error) {
      if (directory) await this.cleanup(directory);
      void vscode.window.showErrorMessage(`NIL debug launch failed: ${String(error).slice(0, 4000)}`);
      return undefined;
    }
  }

  createDebugAdapterDescriptor(session: vscode.DebugSession): vscode.DebugAdapterDescriptor {
    if (!vscode.workspace.isTrusted) throw new Error("NIL debugging requires a trusted workspace.");
    const executable = session.configuration.__nyDebugger;
    if (typeof executable !== "string" || !executable) throw new Error("NIL debugger executable was not resolved.");
    return new vscode.DebugAdapterExecutable(executable, ["--dap"]);
  }

  async endSession(session: vscode.DebugSession): Promise<void> {
    const directory: unknown = session.configuration.__nyDebugDirectory;
    if (session.type === "ny" && typeof directory === "string") await this.cleanup(directory);
  }

  private async cleanup(directory: string): Promise<void> {
    // Only this instance's mkdtemp results may be removed. Never trust a path
    // supplied by launch.json or another debugger session.
    if (!this.directories.delete(directory)) return;
    await fs.rm(directory, { recursive: true, force: true }).catch(() => undefined);
  }

  dispose(): void {
    for (const directory of this.directories) void this.cleanup(directory);
  }
}
