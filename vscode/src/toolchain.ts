import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

export type NyTool = "nilcompiler" | "nilinterpreter" | "nillsp" | "nildebugger";

export interface ResolveToolOptions {
  readonly tool: NyTool;
  readonly explicitPath?: string;
  readonly sdkPath?: string;
  readonly workspaceFolders?: readonly string[];
  readonly environment?: Readonly<Record<string, string | undefined>>;
  readonly platform?: NodeJS.Platform;
  readonly currentDirectory?: string;
  readonly homeDirectory?: string;
  readonly isExecutable?: (candidate: string) => boolean;
}

export interface ToolResolution {
  readonly executable?: string;
  readonly source?: "explicit" | "sdk" | "workspace" | "path";
  readonly attempted: readonly string[];
  readonly error?: string;
}

function pathApiFor(platform: NodeJS.Platform): typeof path.posix | typeof path.win32 {
  return platform === "win32" ? path.win32 : path.posix;
}

function environmentPath(
  environment: Readonly<Record<string, string | undefined>>,
  platform: NodeJS.Platform
): string {
  if (platform === "win32") {
    return environment.Path ?? environment.PATH ?? environment.path ?? "";
  }
  return environment.PATH ?? "";
}

function defaultIsExecutable(candidate: string, platform: NodeJS.Platform): boolean {
  try {
    const stat = fs.statSync(candidate);
    if (!stat.isFile()) {
      return false;
    }
    if (platform !== "win32") {
      fs.accessSync(candidate, fs.constants.X_OK);
    }
    return true;
  } catch {
    return false;
  }
}

export function toolExecutableName(tool: NyTool, platform: NodeJS.Platform): string {
  return platform === "win32" ? `${tool}.exe` : tool;
}

export function expandConfiguredPath(
  value: string,
  workspaceFolder: string | undefined,
  environment: Readonly<Record<string, string | undefined>> = process.env,
  homeDirectory: string = os.homedir()
): string {
  let expanded = value.trim();
  if (workspaceFolder) {
    expanded = expanded.replaceAll("${workspaceFolder}", workspaceFolder);
    expanded = expanded.replaceAll(
      "${workspaceFolderBasename}",
      path.basename(workspaceFolder)
    );
  }
  expanded = expanded.replace(/\$\{env:([^}]+)\}/g, (_whole, name: string) => {
    return environment[name] ?? "";
  });
  if (expanded === "~") {
    return homeDirectory;
  }
  if (expanded.startsWith("~/") || expanded.startsWith("~\\")) {
    return path.join(homeDirectory, expanded.slice(2));
  }
  return expanded;
}

function uniqueCandidates(candidates: readonly string[]): string[] {
  const seen = new Set<string>();
  const unique: string[] = [];
  for (const candidate of candidates) {
    if (!candidate || seen.has(candidate)) {
      continue;
    }
    seen.add(candidate);
    unique.push(candidate);
  }
  return unique;
}

function commandCandidates(
  command: string,
  options: {
    readonly workspaceFolder?: string;
    readonly environment: Readonly<Record<string, string | undefined>>;
    readonly platform: NodeJS.Platform;
    readonly currentDirectory: string;
    readonly homeDirectory: string;
  }
): string[] {
  const pathApi = pathApiFor(options.platform);
  const expanded = expandConfiguredPath(
    command,
    options.workspaceFolder,
    options.environment,
    options.homeDirectory
  );
  const containsSeparator = expanded.includes("/") || expanded.includes("\\");
  const commandNames = (() => {
    if (options.platform !== "win32" || pathApi.extname(expanded)) {
      return [expanded];
    }
    const extensions = (options.environment.PATHEXT ?? ".COM;.EXE;.BAT;.CMD")
      .split(";")
      .map((extension) => extension.trim())
      .filter(Boolean)
      .map((extension) =>
        extension.startsWith(".") ? extension : `.${extension}`
      );
    return uniqueCandidates([expanded, ...extensions.map((extension) => `${expanded}${extension}`)]);
  })();
  if (pathApi.isAbsolute(expanded)) {
    return commandNames.map((name) => pathApi.normalize(name));
  }
  if (containsSeparator) {
    return commandNames.map((name) =>
      pathApi.resolve(options.workspaceFolder ?? options.currentDirectory, name)
    );
  }

  const delimiter = options.platform === "win32" ? ";" : ":";
  return environmentPath(options.environment, options.platform)
    .split(delimiter)
    .filter(Boolean)
    .flatMap((directory) => commandNames.map((name) => pathApi.join(directory, name)));
}

export function resolveNyTool(options: ResolveToolOptions): ToolResolution {
  const platform = options.platform ?? process.platform;
  const pathApi = pathApiFor(platform);
  const environment = options.environment ?? process.env;
  const workspaces = [...(options.workspaceFolders ?? [])];
  const firstWorkspace = workspaces[0];
  const currentDirectory = options.currentDirectory ?? process.cwd();
  const homeDirectory = options.homeDirectory ?? os.homedir();
  const usable = options.isExecutable ?? ((candidate: string) => defaultIsExecutable(candidate, platform));
  const executableName = toolExecutableName(options.tool, platform);

  if (options.explicitPath?.trim()) {
    const attempted = uniqueCandidates(
      commandCandidates(options.explicitPath, {
        workspaceFolder: firstWorkspace,
        environment,
        platform,
        currentDirectory,
        homeDirectory
      })
    );
    const executable = attempted.find(usable);
    return executable
      ? { executable, source: "explicit", attempted }
      : {
          attempted,
          error: `Configured ${options.tool} path does not resolve to an executable.`
        };
  }

  const attempted: string[] = [];
  if (options.sdkPath?.trim()) {
    const expandedSdk = expandConfiguredPath(
      options.sdkPath,
      firstWorkspace,
      environment,
      homeDirectory
    );
    const sdkRoot = pathApi.isAbsolute(expandedSdk)
      ? expandedSdk
      : pathApi.resolve(firstWorkspace ?? currentDirectory, expandedSdk);
    const sdkCandidates = uniqueCandidates([
      pathApi.join(sdkRoot, executableName),
      pathApi.join(sdkRoot, "sdk", executableName),
      pathApi.join(sdkRoot, "build", "cmake", "bin", executableName),
      pathApi.join(sdkRoot, "build", "sdk", executableName),
      pathApi.join(sdkRoot, "bin", executableName),
      pathApi.join(sdkRoot, "sdk", "bin", executableName),
      pathApi.join(sdkRoot, "build", "sdk", "bin", executableName)
    ]);
    attempted.push(...sdkCandidates);
    const executable = sdkCandidates.find(usable);
    if (executable) {
      return { executable, source: "sdk", attempted };
    }
  }

  const workspaceCandidates = uniqueCandidates(
    workspaces.flatMap((workspace) => [
      pathApi.join(workspace, executableName),
      pathApi.join(workspace, "sdk", executableName),
      pathApi.join(workspace, "build", "cmake", "bin", executableName),
      pathApi.join(workspace, "build", "sdk", executableName),
      pathApi.join(workspace, "sdk", "bin", executableName),
      pathApi.join(workspace, "build", "sdk", "bin", executableName),
      pathApi.join(workspace, "bin", executableName)
    ])
  );
  attempted.push(...workspaceCandidates);
  const workspaceExecutable = workspaceCandidates.find(usable);
  if (workspaceExecutable) {
    return { executable: workspaceExecutable, source: "workspace", attempted };
  }

  const pathCandidates = commandCandidates(executableName, {
    workspaceFolder: firstWorkspace,
    environment,
    platform,
    currentDirectory,
    homeDirectory
  });
  attempted.push(...pathCandidates);
  const pathExecutable = pathCandidates.find(usable);
  if (pathExecutable) {
    return { executable: pathExecutable, source: "path", attempted };
  }

  return {
    attempted: uniqueCandidates(attempted),
    error: `${options.tool} was not found in the configured SDK, workspace builds, or PATH.`
  };
}

export function resolveCommandOnPath(
  command: string,
  environment: Readonly<Record<string, string | undefined>> = process.env,
  platform: NodeJS.Platform = process.platform,
  isExecutable?: (candidate: string) => boolean
): string | undefined {
  const usable = isExecutable ?? ((candidate: string) => defaultIsExecutable(candidate, platform));
  return commandCandidates(command, {
    environment,
    platform,
    currentDirectory: process.cwd(),
    homeDirectory: os.homedir()
  }).find(usable);
}
