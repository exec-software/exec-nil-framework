import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  RevealOutputChannelOn,
  ServerOptions
} from "vscode-languageclient/node";

import { resolveConfiguredTool, toolNotFoundMessage } from "./vscodeToolchain";
import { languageDocumentSelector } from "./languages";

export class NyLanguageClientManager implements vscode.Disposable {
  constructor(private readonly compilerDefinitionsEnabled: (document: vscode.TextDocument) => boolean = () => false) {}
  private client: LanguageClient | undefined;
  private readonly outputChannel = vscode.window.createOutputChannel(
    "NIL Framework Language Server",
    { log: true }
  );
  private missingToolNoticeShown = false;

  public async start(notifyIfMissing = true): Promise<void> {
    if (this.client || !vscode.workspace.isTrusted) {
      return;
    }
    if (!vscode.workspace.getConfiguration("ny").get<boolean>("languageServer.enabled", true)) {
      return;
    }

    const resolution = resolveConfiguredTool("nillsp");
    if (!resolution.executable) {
      if (notifyIfMissing && !this.missingToolNoticeShown) {
        this.missingToolNoticeShown = true;
        const action = await vscode.window.showWarningMessage(
          toolNotFoundMessage("nillsp", resolution),
          "Select SDK",
          "Open Settings"
        );
        if (action === "Select SDK") {
          await vscode.commands.executeCommand("ny.selectSdk");
        } else if (action === "Open Settings") {
          await vscode.commands.executeCommand("workbench.action.openSettings", "@ext:exec-software.ny-language-support");
        }
      }
      return;
    }

    this.missingToolNoticeShown = false;
    const serverOptions: ServerOptions = {
      command: resolution.executable,
      // An executable uses stdio by default. Setting TransportKind.stdio
      // appends --stdio, which is not an accepted nillsp command-line option.
      args: []
    };
    const clientOptions: LanguageClientOptions = {
      documentSelector: languageDocumentSelector,
      outputChannel: this.outputChannel,
      revealOutputChannelOn: RevealOutputChannelOn.Error,
      middleware: {
        provideDefinition: (document, position, token, next) => this.compilerDefinitionsEnabled(document)
          ? undefined : next(document, position, token),
        provideReferences: (document, position, context, token, next) => this.compilerDefinitionsEnabled(document)
          ? undefined : next(document, position, context, token),
        prepareRename: (document, position, token, next) => this.compilerDefinitionsEnabled(document)
          ? undefined : next(document, position, token),
        provideRenameEdits: (document, position, newName, token, next) => this.compilerDefinitionsEnabled(document)
          ? undefined : next(document, position, newName, token),
        handleDiagnostics: (uri, diagnostics, next) => {
          const enabled = vscode.workspace
            .getConfiguration("ny", uri)
            .get<boolean>("languageServer.diagnostics.enabled", false);
          next(uri, enabled ? diagnostics : []);
        }
      }
    };

    const nextClient = new LanguageClient(
      "ny",
      "NIL Framework Language Server",
      serverOptions,
      clientOptions
    );
    this.client = nextClient;
    try {
      await nextClient.start();
    } catch (error) {
      this.client = undefined;
      const message = error instanceof Error ? error.message : String(error);
      this.outputChannel.appendLine(`Failed to start nillsp: ${message}`);
      this.outputChannel.show(true);
      void vscode.window.showErrorMessage(`NIL Framework language server failed to start: ${message}`);
    }
  }

  public async stop(): Promise<void> {
    const activeClient = this.client;
    this.client = undefined;
    if (activeClient) {
      await activeClient.stop();
    }
  }

  public async restart(): Promise<void> {
    await this.stop();
    await this.start(true);
  }

  public showOutput(): void {
    this.outputChannel.show(true);
  }

  public dispose(): void {
    void this.stop();
    this.outputChannel.dispose();
  }
}
