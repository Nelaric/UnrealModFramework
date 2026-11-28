# Copyright (c) 2026 Nelaric Contributors
"""Start Development CI for the exact test merge commit a maintainer reviewed."""

from __future__ import annotations

import argparse
import subprocess
import sys

from development import REPOSITORY, approval_ref, is_digest, require

REMOTE = f"https://github.com/{REPOSITORY}.git"
MAIN_REF = "refs/heads/main"


def approve(pr, commit):
    require(pr > 0 and is_digest(commit, 40), "Supply a PR number and its full reviewed test merge SHA")
    merge = f"refs/pull/{pr}/merge"
    head = f"refs/pull/{pr}/head"
    tag = approval_ref(pr)

    subprocess.run(["git", "fetch", "--no-tags", "--no-write-fetch-head", REMOTE, merge], check=True)
    listing = subprocess.check_output(["git", "ls-remote", "--refs", REMOTE, MAIN_REF, head, merge, tag], text=True)
    refs = {ref: sha for sha, ref in (line.split() for line in listing.splitlines())}
    require(refs.get(merge) == commit, "The supplied SHA is no longer the PR's test merge commit")
    parents = subprocess.check_output(["git", "show", "-s", "--format=%P", commit], text=True).split()
    require(parents == [refs.get(MAIN_REF), refs.get(head)], "The PR head or main has changed since review")
    previous = refs.get(tag, "")
    if previous == commit:
        print(f"PR #{pr} already has Development approval at {commit}.")
        return
    subprocess.run(["git", "push", f"--force-with-lease={tag}:{previous}", REMOTE, f"{commit}:{tag}"], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr", type=int, help="Open PR number")
    parser.add_argument("candidate", help="Full SHA of the reviewed test merge commit")
    args = parser.parse_args()
    approve(args.pr, args.candidate)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
