#!/usr/bin/env python3
"""Offline coverage/local inline-link checks; no hardware or network access.

Checks tracked/unignored candidates, not ignored build/log/graph output.
External URLs and heading fragments are not fetched/validated.
"""
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def main():
    names = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
    ).decode().split("\0")
    published = {name for name in names if name}
    errors = []
    examples = sorted(p for p in ROOT.iterdir() if p.is_dir() and
                      re.fullmatch(r"\d{3}_.+", p.name))
    index = (ROOT / "README.md").read_text()
    for chapter in examples:
        for required in ("README.md", "build.sh"):
            if f"{chapter.name}/{required}" not in published:
                errors.append(f"{chapter.name}: missing publishable {required}")
        if f"({chapter.name}/README.md)" not in index:
            errors.append(f"README.md: no README link for {chapter.name}")
        if chapter.name != "001_Hello_World" and not any(
            f"{chapter.name}/{test.name}" in published
            for test in chapter.glob("test*.py")
        ):
            errors.append(f"{chapter.name}: missing test entry point")
    markdown = sorted(n for n in published if n.endswith(".md"))
    checked = 0
    for name in markdown:
        path = ROOT / name
        if not path.is_file():
            errors.append(f"{name}: indexed file is missing")
            continue
        for match in re.finditer(r"!?\[[^\]]*\]\((<[^>]+>|[^\s)]+)(?:\s+\"[^\"]*\")?\)", path.read_text()):
            target = match.group(1).strip("<>")
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path:
                continue
            checked += 1
            resolved = (path.parent / unquote(url.path)).resolve()
            try:
                relative = resolved.relative_to(ROOT).as_posix()
            except ValueError:
                errors.append(f"{name}: link leaves repository: {target}")
                continue
            included = relative in published or (resolved.is_dir() and
                        any(n.startswith(relative + "/") for n in published))
            if not resolved.exists() or not included:
                errors.append(f"{name}: missing/ignored link destination: {target}")
    if errors:
        print("\n".join("FAIL " + e for e in errors), file=sys.stderr)
        return 1
    print(f"PASS docs: {len(examples)} examples, {len(markdown)} Markdown files, "
          f"{checked} local links; external URLs/anchors not checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
