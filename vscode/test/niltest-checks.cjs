const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

exports.run = async ({ check, workspace }) => {
  await check("niltest: compiler-discovered Matrix cases reach Test Explorer", async () => {
    await vscode.workspace.saveAll(false);
    // Discovery reads a saved import graph. An unrelated unsaved scratch
    // document must not prevent it, including when this check runs alone.
    const scratch = await vscode.workspace.openTextDocument({ language: "matrix", content:
      "book unrelated.scratch\nvalue draft is 7\n" });
    assert.ok(scratch.isUntitled && scratch.isDirty, "Regression fixture must remain an unsaved scratch buffer.");
    const file = path.join(workspace, "colocated-tests.matrix");
    await fs.writeFile(file, `book testing.editor
note suite "editor checks"
first begin test
    expect yes
end test
note suite "editor checks"
second begin test
    expect 2 equals 2
end test
`);
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(file));
    const cases = await vscode.commands.executeCommand("ny.discoverTests");
    assert.ok(Array.isArray(cases), "Test discovery command must return its compiler-discovered cases.");
    assert.equal(cases.length, 2);
    assert.equal(new Set(cases.map(item => item.id)).size, 2);
    for (const item of cases) {
      assert.equal(item.file, file);
      assert.equal(item.suite, "editor checks");
      assert.ok(item.line > 0);
    }
    return { ignoredUntitledScratch: true, tests: cases.map(item => ({ name: item.name, line: item.line })) };
  });
};
