import * as path from "node:path";
import * as vscode from "vscode";
import { findProjectFile, NilProject, projectFilename, readProject } from "./projectModel";
import { expandConfiguredPath } from "./toolchain";

export function projectForResource(resource?: vscode.Uri, explicitProject?: string): NilProject | undefined {
  const folder = (resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined) ?? vscode.workspace.workspaceFolders?.[0];
  if (explicitProject) {
    const expanded = expandConfiguredPath(explicitProject, folder?.uri.fsPath);
    if (!path.isAbsolute(expanded) && !folder) throw new Error("A relative project path requires a workspace folder.");
    return readProject(path.resolve(folder?.uri.fsPath ?? process.cwd(), expanded));
  }
  if (!folder) return undefined;
  const source = resource?.scheme === "file" ? resource.fsPath : path.join(folder.uri.fsPath, projectFilename);
  const file = findProjectFile(source, folder.uri.fsPath);
  return file ? readProject(file) : undefined;
}
