const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

exports.run = async ({ check, workspace }) => {
  for (const language of ["ny", "matrix"]) {
    const source = path.join(workspace, `memory profile.${language}`);
    await fs.writeFile(source, language === "ny" ?
      'fn main() -> i32\n let values = [41, 42]\n println(values[1])\n return 0\nend fn\n' :
      'book editor.memory\nmain returns override integer begin paragraph\n value items is [41, 42]\n say items at 1\n give 0\nend paragraph\n');
    const document = await vscode.workspace.openTextDocument(source);
    for (const route of ["vm", "native"]) {
      await check(`${language}: ${route} memory command saves and opens an attributed profile`, async () => {
        await vscode.window.showTextDocument(document, { preview: false });
        const uri = await vscode.commands.executeCommand(route === "vm" ? "ny.profileMemory" : "ny.profileNativeMemory");
        assert.ok(uri instanceof vscode.Uri, "Profiling should return its saved report URI");
        assert.equal(vscode.window.activeTextEditor.document.uri.toString(), uri.toString());
        const report = JSON.parse(await fs.readFile(uri.fsPath, "utf8"));
        assert.equal(report.schema, "aperture.memory.v1");
        assert.equal(report.route, route);
        assert.equal(report.completed, true);
        assert.deepEqual(report.errors, []);
        assert.ok(report.totals.allocated_bytes > 0);
        assert.ok(report.totals.allocated_objects > 0);
        assert.ok(report.sites.some(site => site.file === source && site.line > 0 && site.function.includes("main")));
        assert.equal(report.totals.allocated_bytes - report.totals.freed_bytes, report.totals.live_bytes);
        return { language, route, allocatedObjects: report.totals.allocated_objects };
      });
    }
  }
};
