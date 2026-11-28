# Copyright (c) 2026 Nelaric Contributors
"""Run repository checks that require the exact source checkout or PR candidate."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

from development import CONTROL, api, require, validate_live_pr

SOURCE = Path("source").resolve()
STYLE = {
    "//": {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inl", ".cs", ".js"},
    "<!--": {".md", ".html", ".xml", ".csproj"},
    "#": {".py", ".sh", ".cmake", ".yml", ".yaml", ".ps1", ".toml"},
    "@rem": {".bat", ".cmd"},
    ";": {".ini"},
}
NAMES = {"CMakeLists.txt", "Doxyfile", ".editorconfig", ".gitattributes", ".gitignore", ".clang-format", ".csharpierrc"}
VENDORED = ("UnrealModFramework/Plugins/ModRuntime/Tools/ThirdParty/",)


def git_paths(*args):
    files = subprocess.check_output(["git", "-C", str(SOURCE), *args])
    return [name.decode() for name in files.split(b"\0") if name and not name.decode().startswith(VENDORED)]


def validate_copyright():
    candidate = json.loads(os.environ["CI_IDENTITY"])
    base = candidate["base"] or "HEAD^"
    paths = git_paths("diff", "--name-only", "--diff-filter=ACMRT", "-z", base, "HEAD", "--")
    failures = []
    for relative in paths:
        path = SOURCE / relative
        marker = next((prefix for prefix, suffixes in STYLE.items() if path.suffix.lower() in suffixes), None)
        if path.name in NAMES or path.name.lower().endswith("dockerfile"):
            marker = "#"
        if not marker:
            continue
        lines = path.read_text(encoding="utf-8-sig").splitlines()
        if lines and (lines[0].startswith("#!") or (marker == "<!--" and lines[0].startswith("<?xml "))):
            lines = lines[1:]
        ending = " -->" if marker == "<!--" else ""
        expected = rf"{re.escape(marker)} Copyright \(c\) [0-9]{{4}} Nelaric Contributors{ending}"
        if not lines or not re.fullmatch(expected, lines[0]):
            failures.append(relative)
    require(not failures, "Missing or misplaced Nelaric copyright notice:\n" + "\n".join(failures))
    print(f"Checked copyright notices on {len(paths)} candidate path(s).")


def validate_configuration():
    files = git_paths("ls-files", "-z")
    checked = 0
    for relative in files:
        path = SOURCE / relative
        if path.suffix.lower() not in {".json", ".uproject", ".uplugin", ".modexport"}:
            continue
        try:
            json.loads(path.read_text(encoding="utf-8-sig"))
        except (ValueError, OSError) as error:
            raise ValueError(f"{relative}: {error}") from error
        checked += 1
    print(f"Validated {checked} repository JSON and Unreal configuration files.")


def validate_naming():
    sys.path.insert(0, str(CONTROL / "Scripts"))
    from check_git_naming import validate

    candidate = json.loads(os.environ["CI_IDENTITY"])
    validate_live_pr(candidate)
    token = os.environ.get("PUBLIC_READ_TOKEN", "")
    path = f"/repos/{candidate['repository']}/pulls/{candidate['pr']}"
    pr = api(path, token)
    subjects = []
    parents = []
    for page in range(1, 4):
        commits = api(f"{path}/commits?per_page=100&page={page}", token)
        subjects.extend(commit["commit"]["message"].splitlines()[0] for commit in commits)
        parents.extend(len(commit["parents"]) for commit in commits)
        if len(commits) < 100:
            break
    require(len(subjects) == pr["commits"], "GitHub did not return every PR commit")
    errors = validate(pr["title"], pr["head"]["ref"], subjects, parent_counts=parents)
    validate_live_pr(candidate)
    require(not errors, "\n".join(errors))
    print(f"Validated PR title, branch name and {len(subjects)} commit subject(s).")


if __name__ == "__main__":
    checks = {"copyright": validate_copyright, "configuration": validate_configuration, "naming": validate_naming}
    try:
        checks[sys.argv[1]]()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
