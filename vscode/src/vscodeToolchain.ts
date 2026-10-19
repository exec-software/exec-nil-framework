import * as vscode from "vscode";

import {
  expandConfiguredPath,
  NyTool,
  resolveNyTool,
  ToolResolution
} from "./toolchain";

const settingForTool: Record<NyTool, string> = {
  nilcompiler: "compiler.path",
  nilinterpreter: "interpreter.path",
  nillsp: "server.path",
  nildebugger: "debugger.path",
  nilbuild: "projectTool.path"
};

function orderedWorkspaceFolders(resource?: vscode.Uri): string[] {
  const folders = vscode.workspace.workspaceFolders ?? [];
  const owningFolder = resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined;
  return [
    ...(owningFolder ? [owningFolder] : []),
    ...folders.filter((folder) => folder !== owningFolder)
  ].map((folder) => folder.uri.fsPath);
}

export function nyConfiguration(resource?: vscode.Uri): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration("ny", resource);
}

export function configuredSdkPath(resource?: vscode.Uri): string | undefined {
  const raw = nyConfiguration(resource).get<string>("sdkPath", "").trim();
  if (!raw) {
    return undefined;
  }
  const folders = orderedWorkspaceFolders(resource);
  return expandConfiguredPath(raw, folders[0]);
}

export function resolveConfiguredTool(tool: NyTool, resource?: vscode.Uri, sdkPath?: string): ToolResolution {
  const configuration = nyConfiguration(resource);
  return resolveNyTool({
    tool,
    explicitPath: configuration.get<string>(settingForTool[tool], ""),
    sdkPath: sdkPath ?? configuration.get<string>("sdkPath", ""),
    workspaceFolders: orderedWorkspaceFolders(resource)
  });
}

export function toolNotFoundMessage(tool: NyTool, result: ToolResolution): string {
  if (result.error) {
    return `${result.error} Set ny.${settingForTool[tool]} or ny.sdkPath.`;
  }
  return `${tool} is unavailable. Set ny.${settingForTool[tool]} or ny.sdkPath.`;
}

export async function selectSdkFolder(): Promise<void> {
  const selected = await vscode.window.showOpenDialog({
    title: "Select NIL Framework SDK or repository folder",
    canSelectFiles: false,
    canSelectFolders: true,
    canSelectMany: false,
    defaultUri: vscode.workspace.workspaceFolders?.[0]?.uri,
    openLabel: "Use NIL Framework SDK"
  });
  const folder = selected?.[0];
  if (!folder) {
    return;
  }
  const target = vscode.workspace.workspaceFolders
    ? vscode.ConfigurationTarget.Workspace
    : vscode.ConfigurationTarget.Global;
  await vscode.workspace.getConfiguration("ny").update("sdkPath", folder.fsPath, target);
}
