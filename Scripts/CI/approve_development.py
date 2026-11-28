# Copyright (c) 2026 Nelaric Contributors
"""Request Development CI for the exact fork PR test merge a maintainer reviewed."""

import argparse
import json
import subprocess
import sys

from development import APPROVAL_ENVIRONMENT, APPROVAL_TASK, MAINTAINER_ROLES, REPOSITORY, is_digest, require


def github(path, request=None):
    arguments = ["gh", "api", "--hostname", "github.com", path]
    options = {}
    if request is not None:
        arguments.extend(("--method", "POST", "--input", "-"))
        options["input"] = json.dumps(request)
    result = subprocess.run(arguments, check=True, stdout=subprocess.PIPE, text=True, **options)
    return json.loads(result.stdout)


def approve(number, commit):
    require(number > 0 and is_digest(commit, 40), "Supply a fork PR number and its full reviewed test merge SHA")
    repository = f"repos/{REPOSITORY}"
    actor = github("user")
    permission = github(f"{repository}/collaborators/{actor['login']}/permission")
    require(permission["role_name"] in MAINTAINER_ROLES, "Use a GitHub account with repository maintain access")

    pr = github(f"{repository}/pulls/{number}")
    head, base = pr["head"], pr["base"]
    require(pr["state"] == "open" and base["ref"] == "main" and base["repo"]["full_name"] == REPOSITORY
            and pr["mergeable"] is True and pr["merge_commit_sha"] == commit,
            "The supplied SHA must be the current test merge for an open PR to main")
    require((head["repo"] or {}).get("full_name") != REPOSITORY or pr["user"]["login"] == "dependabot[bot]",
            "PRs from branches in the repository run the engine jobs automatically")
    main = github(f"{repository}/git/ref/heads/main")
    parents = github(f"{repository}/commits/{commit}")["parents"]
    require(base["sha"] == main["object"]["sha"] and [parent["sha"] for parent in parents] == [base["sha"], head["sha"]],
            "The PR head or main has moved since the merge commit was selected")

    deployment = github(f"{repository}/deployments", {
        "ref": commit,
        "task": APPROVAL_TASK,
        "environment": APPROVAL_ENVIRONMENT,
        "payload": {"pull_request": number, "head": head["sha"], "base": base["sha"]},
        "auto_merge": False,
        "required_contexts": [],
        "production_environment": False,
        "transient_environment": True,
    })
    require(deployment["sha"] == deployment["ref"] == commit, "GitHub returned a different deployment commit")
    print(f"Requested Development CI for PR #{number} at {commit}. Approval record: {deployment['id']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pr", type=int, help="Open fork PR number")
    parser.add_argument("candidate", help="Full SHA of the reviewed test merge commit")
    args = parser.parse_args()
    try:
        approve(args.pr, args.candidate)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
