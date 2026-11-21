import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import path from "node:path";

const require = createRequire(import.meta.url);
const clientVersion = "10.1.1";
const matcherVersion = "11.0.9";
const consumers = ["lib/common/fileOperations.js", "lib/common/utils/globPattern.js"];
const oldGuard = "if (!matcher.makeRe()) {";

// Minimatch 11 deliberately no longer creates regular expressions. These two
// upstream callers only use makeRe() as a boolean validity check before match().
// Empty/comment patterns have an empty parsed set and must remain rejected.
// Keep that check using the documented set property, without fabricating a RegExp API
// or changing the matcher prototype. Constructor errors remain errors.
export function adaptLanguageClientGlobGuard(source) {
  if (source.split(oldGuard).length !== 2 || (source.match(/\.makeRe\s*\(/g) || []).length !== 1) {
    throw new Error("Language client glob validation changed; review its minimatch compatibility before bundling");
  }
  return source.replace(oldGuard, "if (matcher.set.length === 0) {");
}

// Apply the reviewed adaptation only to the exact pinned consumer files in the
// production bundle. Leave installed packages untouched, and fail the build if
// dependency updates change either API or remove one of the expected consumers.
export function languageClientGlobCompatibility() {
  const clientRoot = path.resolve(path.dirname(require.resolve("vscode-languageclient/node")), "../..");
  const clientManifest = path.join(clientRoot, "package.json");
  const files = new Set(consumers.map(file => path.join(clientRoot, file)));
  const seen = new Set();
  return {
    name: "language-client-minimatch-11",
    setup(build) {
      build.onStart(async () => {
        seen.clear();
        const client = JSON.parse(await readFile(clientManifest, "utf8"));
        const fromClient = createRequire(clientManifest);
        const matcher = fromClient("minimatch/package.json");
        if (client.version !== clientVersion || matcher.version !== matcherVersion) {
          throw new Error("Language client/minimatch versions changed; review the glob compatibility adaptation");
        }
      });
      build.onLoad({ filter: /[/\\]vscode-languageclient[/\\]lib[/\\]common[/\\].*\.js$/ }, async args => {
        if (!files.has(args.path)) return;
        const contents = adaptLanguageClientGlobGuard(await readFile(args.path, "utf8"));
        seen.add(args.path);
        return { contents, loader: "js", resolveDir: path.dirname(args.path) };
      });
      build.onEnd(result => {
        if (result.errors.length === 0 && seen.size !== files.size) {
          return { errors: [{ text: "The extension must bundle both reviewed language-client glob consumers" }] };
        }
      });
    }
  };
}
