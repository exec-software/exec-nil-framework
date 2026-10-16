#!/usr/bin/env python3
"""Build the NIL Framework Markdown documentation as a self-contained HTML SDK manual."""

from __future__ import annotations

import argparse
import html
import os
import re
import shutil
import subprocess
from pathlib import Path
from urllib.parse import unquote, urlsplit


STYLE = """
:root {
  color-scheme: light dark;
  --background: #11141a;
  --surface: #191e27;
  --text: #edf1f7;
  --muted: #aeb8c7;
  --accent: #7dd3fc;
  --border: #354052;
}
* { box-sizing: border-box; }
body {
  background: var(--background);
  color: var(--text);
  font: 17px/1.62 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  margin: 0 auto;
  max-width: 78rem;
  padding: 2.5rem clamp(1.25rem, 4vw, 4rem) 5rem;
}
h1, h2, h3, h4 { line-height: 1.2; margin-top: 1.8em; }
h1 { border-bottom: 1px solid var(--border); padding-bottom: .45rem; }
a { color: var(--accent); }
code, pre { font-family: ui-monospace, SFMono-Regular, Consolas, monospace; }
code { background: var(--surface); border-radius: .3rem; padding: .1rem .3rem; }
pre { background: var(--surface); border: 1px solid var(--border); border-radius: .5rem; overflow: auto; padding: 1rem; }
pre code { padding: 0; }
blockquote { border-left: .25rem solid var(--border); color: var(--muted); margin-left: 0; padding-left: 1rem; }
table { border-collapse: collapse; display: block; overflow-x: auto; }
th, td { border: 1px solid var(--border); padding: .5rem .7rem; text-align: left; vertical-align: top; }
img { height: auto; max-width: 100%; }
""".strip()


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--revision", default="master")
    parser.add_argument("--pandoc", default="pandoc")
    return parser.parse_args()


def title_for(source: Path) -> str:
    for line in source.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return source.stem.replace("-", " ").title()


def output_for(source: Path, repo_root: Path, docs_root: Path, output: Path) -> Path:
    if source == repo_root / "README.md":
        return output / "project-overview.html"
    relative = source.relative_to(docs_root)
    if relative.name == "README.md":
        relative = relative.with_name("index.html")
    else:
        relative = relative.with_suffix(".html")
    return output / relative


def rewrite_links(
    document: str,
    source: Path,
    destination: Path,
    repo_root: Path,
    docs_root: Path,
    output_root: Path,
    revision: str,
) -> str:
    pattern = re.compile(r'href="([^"]+)"')

    def replace(match: re.Match[str]) -> str:
        raw = html.unescape(match.group(1))
        split = urlsplit(raw)
        if split.scheme or split.netloc or not split.path:
            return match.group(0)

        target = (source.parent / unquote(split.path)).resolve()
        mapped: Path | None = None
        if target == repo_root / "README.md":
            mapped = output_root / "project-overview.html"
        else:
            try:
                target.relative_to(docs_root)
                if target.suffix == ".md" and target.is_file():
                    mapped = output_for(target, repo_root, docs_root, output_root)
            except ValueError:
                pass

        suffix = ""
        if split.query:
            suffix += "?" + split.query
        if split.fragment:
            suffix += "#" + split.fragment

        if mapped is not None:
            rewritten = os.path.relpath(mapped, destination.parent).replace(os.sep, "/")
            return 'href="{}{}"'.format(html.escape(rewritten, quote=True), suffix)

        try:
            repo_relative = target.relative_to(repo_root).as_posix()
        except ValueError:
            return match.group(0)
        github = (
            "https://github.com/exec-software/exec-nil-framework/blob/"
            + revision
            + "/"
            + repo_relative
            + suffix
        )
        return 'href="{}"'.format(html.escape(github, quote=True))

    return pattern.sub(replace, document)


def main() -> int:
    args = arguments()
    repo_root = args.repo_root.resolve(strict=True)
    docs_root = repo_root / "docs"
    output_argument = args.output.absolute()
    if output_argument.is_symlink():
        raise SystemExit("documentation output must not be a symlink")
    output_root = output_argument.resolve(strict=False)
    if output_root == Path(output_root.anchor) or output_root == repo_root:
        raise SystemExit("refusing unsafe documentation output: {}".format(output_root))
    try:
        repo_root.relative_to(output_root)
    except ValueError:
        pass
    else:
        raise SystemExit(
            "refusing documentation output that contains the repository: {}".format(
                output_root
            )
        )
    pandoc = shutil.which(args.pandoc)
    if pandoc is None:
        raise SystemExit("pandoc is required to build the SDK documentation")
    if not docs_root.is_dir():
        raise SystemExit("documentation source directory is missing: {}".format(docs_root))

    if output_root.exists():
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True)

    sources = [repo_root / "README.md", *sorted(docs_root.rglob("*.md"))]
    for source in sources:
        destination = output_for(source, repo_root, docs_root, output_root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        process = subprocess.run(
            [
                pandoc,
                "--from=gfm",
                "--to=html5",
                "--standalone",
                "--toc",
                "--metadata=title:{}".format(title_for(source)),
                str(source),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        if process.returncode != 0:
            raise SystemExit(
                "pandoc failed for {}:\n{}".format(source, process.stderr.rstrip())
            )
        document = process.stdout.replace(
            "</head>", "<style>\n{}\n</style>\n</head>".format(STYLE), 1
        )
        document = rewrite_links(
            document,
            source,
            destination,
            repo_root,
            docs_root,
            output_root,
            args.revision,
        )
        destination.write_text(document, encoding="utf-8")

    # Keep redistribution notices and the reviewed dependency contract with the
    # extracted SDK, not only beside the downloadable archive.
    for name in ("THIRD_PARTY_NOTICES.txt", "dependency-policy.json"):
        source = repo_root / "release" / name
        if not source.is_file() or source.is_symlink():
            raise SystemExit("release documentation input is missing or unsafe: {}".format(source))
        destination = output_root / "release" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    print("built {} SDK documentation pages".format(len(sources)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
