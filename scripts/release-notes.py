#!/usr/bin/env python3
"""Validate human release notes and optionally prepare a tag/channel publication."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def section(text, heading):
    visible = re.sub(r"<!--.*?(?:-->|$)", "", text, flags=re.DOTALL)
    match = re.search(r"(?ms)^## " + re.escape(heading) + r"\n(.*?)(?=^## |\Z)", visible)
    if not match:
        raise ValueError("missing release-note section: " + heading)
    body = match[1].strip()
    for label in ("Changes", "Upgrade impact"):
        part = re.search(r"(?ms)^### " + label + r"\n(.*?)(?=^### |\Z)", body)
        bullets = re.findall(r"(?m)^- (.+)", part[1]) if part else []
        if not any(any(c.isalpha() for c in bullet) and not re.fullmatch(
                r"https?://\S+|\[[^\]]+\]\([^)]+\)", bullet.strip()) for bullet in bullets):
            raise ValueError(heading + " needs a human " + label + " bullet")
    return body


def check_notes(text, previous="", tag=""):
    current = section(text, "Current source")
    if previous and current == section(previous, "Current source"):
        raise ValueError("Current source notes are unchanged; describe this source release")
    if tag and not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)*(?:-[0-9A-Za-z.-]+)?", tag):
        raise ValueError("invalid release tag")
    return section(text, tag) if tag else current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="comparison commit; CI uses HEAD^")
    parser.add_argument("--tag", default=os.environ.get("GITHUB_REF_NAME", "")
                        if os.environ.get("GITHUB_REF_TYPE") == "tag" else "")
    parser.add_argument("--output", type=Path, help="write publication notes to this owned file")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    previous = ""
    if args.base:
        ref = subprocess.check_output(
            ["git", "rev-parse", "--verify", "--end-of-options", args.base + "^{commit}"],
            cwd=root, text=True).strip()
        exists = subprocess.check_output(
            ["git", "ls-tree", "--name-only", ref, "--", "CHANGELOG.md"], cwd=root, text=True).strip()
        # Notes describe what ships; a change confined to .github/ ships nothing.
        changed = subprocess.check_output(
            ["git", "diff", "--no-renames", "--name-only", ref, "HEAD"], cwd=root, text=True)
        ci_only = all(path.startswith(".github/") for path in changed.splitlines())
        if exists and not ci_only:
            previous = subprocess.check_output(
                ["git", "show", ref + ":CHANGELOG.md"], cwd=root, text=True)
    body = check_notes((root / "CHANGELOG.md").read_text(encoding="utf-8"), previous, args.tag)
    if args.output:
        sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        if args.tag:
            tagged = subprocess.check_output(
                ["git", "rev-parse", "--verify", "refs/tags/" + args.tag + "^{commit}"],
                cwd=root, text=True).strip()
            if tagged != sha:
                raise ValueError("the publication tag does not select this source commit")
        title = args.tag or ("source " + sha[:12])
        source = "https://github.com/Busnes-app/KyPost-for-Linux/commit/" + sha
        history = "https://github.com/Busnes-app/KyPost-for-Linux/blob/" + sha + "/CHANGELOG.md"
        args.output.write_text("# KyPost " + title + "\n\nSource: " + source +
                               "\n\n[Full release history](" + history + ")\n\n" + body + "\n",
                               encoding="utf-8")
    print("Release notes verified: " + (args.tag or "Current source"))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
