import * as esbuild from "esbuild";

const watching = process.argv.includes("--watch");
const production = process.argv.includes("--production");

const context = await esbuild.context({
  entryPoints: ["src/extension.ts"],
  bundle: true,
  outfile: "dist/extension.js",
  external: ["vscode"],
  format: "cjs",
  platform: "node",
  target: "node20",
  sourcemap: production ? false : "inline",
  minify: false,
  logLevel: "info"
});

if (watching) {
  await context.watch();
  console.log("Watching the NIL Framework VS Code extension sources...");
} else {
  await context.rebuild();
  await context.dispose();
}
