import * as path from "node:path";

export type CompileMode = "nil" | "native";

export interface CompileInvocationOptions {
  readonly compiler: string;
  readonly source: string;
  readonly output: string;
  readonly mode: CompileMode;
  readonly sdkPath?: string;
  readonly noStdlib?: boolean;
  readonly debugInfo?: boolean;
  readonly extraArgs?: readonly string[];
}

export interface ToolInvocation {
  readonly command: string;
  readonly args: readonly string[];
}

export interface SdkBuildInvocation extends ToolInvocation {
  readonly script: string;
  readonly buildDirectory?: string;
}

export function sdkBuildInvocation(
  workspace: string,
  buildDirectory: string,
  platform: NodeJS.Platform = process.platform,
  powershell: string = "powershell.exe"
): SdkBuildInvocation {
  const pathApi = platform === "win32" ? path.win32 : path.posix;
  const script = pathApi.join(workspace, "scripts", platform === "win32" ? "build-sdk.ps1" : "build-sdk.sh");
  if (platform !== "win32") {
    // The existing POSIX workflow configures its own build and installs an SDK;
    // ny.buildDirectory must not silently change its environment-based contract.
    return { command: script, args: [], script };
  }
  const configuredBuild = pathApi.resolve(workspace, buildDirectory);
  return {
    command: powershell,
    // -File accepts literal script arguments. Never build a -Command string or
    // forward general task args (which could enable dirty-source overrides).
    args: ["-NoLogo", "-NoProfile", "-NonInteractive", "-File", script, "-BuildDirectory", configuredBuild],
    script,
    buildDirectory: configuredBuild
  };
}

export function compileInvocation(options: CompileInvocationOptions): ToolInvocation {
  const args = [...(options.extraArgs ?? [])];
  if (options.mode === "native") {
    args.push("--native");
  } else {
    // Bundling is the compiler default. Editor tasks need a deterministic NIL
    // artifact that can be handed to nilinterpreter.
    args.push("--no-bundle");
  }
  if (options.noStdlib) {
    args.push("--no-stdlib");
  }
  if (options.debugInfo) {
    args.push("--debug-info");
  }
  if (options.sdkPath) {
    args.push("--sdk-path", options.sdkPath);
  }
  args.push("--output", options.output, options.source);
  return { command: options.compiler, args };
}

export function interpreterInvocation(
  interpreter: string,
  nilPath: string,
  interpreterArgs: readonly string[] = [],
  programArgs: readonly string[] = []
): ToolInvocation {
  return {
    command: interpreter,
    args: [
      ...interpreterArgs,
      nilPath,
      ...(programArgs.length > 0 ? ["--", ...programArgs] : [])
    ]
  };
}

export function defaultArtifactPath(
  source: string,
  mode: CompileMode,
  outputDirectory: string | undefined,
  platform: NodeJS.Platform = process.platform
): string {
  if (source === "${file}" || source.includes("${fileBasenameNoExtension}")) {
    const extension = mode === "nil" ? ".nil" : platform === "win32" ? ".exe" : "";
    const directory = outputDirectory?.trim() || "${fileDirname}";
    return `${directory}/${"${fileBasenameNoExtension}"}${extension}`;
  }

  const pathApi = platform === "win32" ? path.win32 : path.posix;
  const parsed = pathApi.parse(source);
  const directory = outputDirectory?.trim()
    ? pathApi.isAbsolute(outputDirectory)
      ? outputDirectory
      : pathApi.resolve(parsed.dir, outputDirectory)
    : parsed.dir;
  const extension = mode === "nil" ? ".nil" : platform === "win32" ? ".exe" : "";
  return pathApi.join(directory, `${parsed.name}${extension}`);
}
