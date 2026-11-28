# Copyright (c) 2026 Nelaric Contributors
"""Resolve the GitHub candidate and enforce the Development CI result."""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen

CONTROL = Path(__file__).resolve().parents[2]
REPOSITORY = "Nelaric/UnrealModFramework"
PLATFORMS = {
    "windows-x64": ("Windows X64", "windows-2022", "Windows", "x64", "Win64"),
    "windows-arm64": ("Windows ARM64", "windows-11-arm", "Windows", "arm64", "Win64"),
    "linux-x64": ("Linux X64", "ubuntu-24.04", "Linux", "x64", "Linux"),
    "linux-arm64": ("Linux ARM64", "ubuntu-24.04-arm", "Linux", "arm64", "LinuxArm64"),
    "macos-x64": ("MacOS X64", "macos-15-intel", "Darwin", "x64", "Mac"),
    "macos-arm64": ("MacOS ARM64", "macos-15", "Darwin", "arm64", "Mac"),
}
EDITORS = {key: key.replace("windows-arm64", "windows-x64").replace("linux-arm64", "linux-x64") for key in PLATFORMS}
JOBS = {"code-quality": "Code Quality", "native-tools": "Native Tools"}
JOBS.update({f"editor-{key}": f"Editor / {PLATFORMS[key][0]}" for key in dict.fromkeys(EDITORS.values())})
JOBS.update({f"prepare-game-{key}": f"Game Prepare / {value[0]}" for key, value in PLATFORMS.items()})
JOBS.update({f"game-{key}": f"Game / {value[0]}" for key, value in PLATFORMS.items()})

LOCK = CONTROL / ".github/engine-lock.json"
COMMAND_FILE = CONTROL / ".github/development-commands.json"
COMMANDS = {
    "build-native-tools", "prepare-libclang", "prepare-editor-bootstrap", "export-metadata", "validate-metadata",
    "build-editor", "prepare-game", "save-checkpoint", "validate-checkpoint", "restore-checkpoint",
    "import-metadata", "resume-game", "verify-game", "test-native-development",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def json_object(path):
    with Path(path).open(encoding="utf-8-sig") as stream:
        value = json.load(stream)
    require(isinstance(value, dict), f"{path}: expected a JSON object")
    return value


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def is_digest(value, length=64):
    return isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{length}}}", value) is not None


def output(name, value):
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as stream:
        stream.write(f"{name}={value}\n")


def summary(lines):
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as stream:
        stream.write("\n".join(lines) + "\n")


def api(path, token="", body=None, method=None):
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "umf-development-ci",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    encoded = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        encoded = json.dumps(body).encode()
    request = Request(os.environ["GITHUB_API_URL"] + path, headers=headers, data=encoded, method=method)
    with urlopen(request, timeout=30) as response:
        return json.load(response) if response.status != 204 else None


def event():
    return json_object(os.environ["GITHUB_EVENT_PATH"])


def private_dispatch():
    repository = event()["repository"]
    return (
        os.environ["GITHUB_EVENT_NAME"] == "workflow_dispatch"
        and repository.get("private") is True
        and repository["owner"]["login"].lower() == "nelaric"
        and os.environ["GITHUB_REF"] == f"refs/heads/{repository['default_branch']}"
    )


def validate_live_pr(candidate, *, check_base=False):
    repo = candidate["repository"]
    token = os.environ.get("PUBLIC_READ_TOKEN", "")
    pr = api(f"/repos/{repo}/pulls/{candidate['pr']}", token)
    require(
        pr["state"] == "open" and pr["base"]["ref"] == "main" and pr["base"]["repo"]["full_name"] == repo
        and pr["head"]["sha"] == candidate["head"] and pr["base"]["sha"] == candidate["base"],
        "The reviewed PR head or main base has changed",
    )
    require(pr["merge_commit_sha"] == candidate["commit"], "The reviewed test merge commit has changed")
    if check_base:
        base = api(f"/repos/{repo}/git/ref/heads/main", token)
        require(base["object"]["sha"] == candidate["base"], "Main has moved since the candidate was reviewed")


def resolve_candidate():
    payload = event()
    repo = os.environ["GITHUB_REPOSITORY"]
    commit = os.environ["GITHUB_SHA"]
    candidate = {"repository": repo, "commit": commit, "pr": 0, "base": "", "head": "", "private": False}
    if os.environ["GITHUB_EVENT_NAME"] == "pull_request":
        pr = payload["pull_request"]
        candidate.update(pr=payload["number"], base=pr["base"]["sha"], head=pr["head"]["sha"])
    elif payload["repository"]["private"]:
        require(private_dispatch(), "Engine verification requires a private Nelaric default-branch dispatch")
        args = payload.get("inputs") or {}
        require(re.fullmatch(r"[1-9][0-9]*", args.get("pr_number", "")), "Expected an open public PR number")
        candidate.update(
            repository=REPOSITORY, pr=int(args["pr_number"]), base=args.get("base_sha"),
            head=args.get("head_sha"), commit=args.get("candidate_sha"), private=True,
        )
    require(is_digest(candidate["commit"], 40), "Expected an exact candidate commit SHA")
    if candidate["pr"]:
        require(is_digest(candidate["head"], 40) and is_digest(candidate["base"], 40), "Invalid PR head or base SHA")
        validate_live_pr(candidate, check_base=True)
    output("source", candidate["repository"])
    output("commit", candidate["commit"])
    output("identity", json.dumps(candidate, separators=(",", ":")))


def verify_checkout(source, candidate):
    def git(*args):
        return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()

    require(git("rev-parse", "HEAD") == candidate["commit"], "The checkout differs from the reviewed candidate")
    if candidate["pr"]:
        parents = git("show", "-s", "--format=%P", "HEAD").split()
        require(parents == [candidate["base"], candidate["head"]], "PR merge parents differ from the reviewed base and head")


def engine_lock(full=False):
    lock = json_object(LOCK)
    require(lock["schemaVersion"] == 1 and lock["repository"] == "Nelaric/UMFCI", "Unknown engine lock format")
    require(
        lock["ueVersion"] == "5.6.1" and lock["configuration"] == "Development"
        and type(lock["maxInstalledBytes"]) is int and 0 < lock["maxInstalledBytes"] <= 5_000_000_000,
        "Invalid Development SDK version, configuration or size limit",
    )
    targets = {f"{key}-game" for key in PLATFORMS} | {f"{key}-editor" for key in EDITORS.values()}
    require(set(lock["targets"]) == targets, "Engine lock must include the ten Development SDK targets")
    for target, entry in lock["targets"].items():
        key, profile = target.rsplit("-", 1)
        _, runner, _, arch, platform = PLATFORMS[key]
        require(
            (entry["runner"], entry["architecture"], entry["ubtPlatform"], entry["profile"])
            == (runner, arch, platform, profile.capitalize())
            and entry["ubtArchitecture"] == arch,
            f"{target}: runner, UBT target or architecture differs from the lock",
        )
        populated = (entry["release"], entry["recipe"], entry["manifestSha256"])
        if full or any(value is not None for value in populated):
            require(
                isinstance(entry["release"], str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", entry["release"])
                and entry["release"].lower() != "latest" and isinstance(entry["recipe"], str) and entry["recipe"]
                and is_digest(entry["manifestSha256"]) and lock["toolchains"].get(entry["toolchain"]),
                f"{target}: an accepted immutable SDK release and toolchain must be locked",
            )
    if full or any(entry["release"] for entry in lock["targets"].values()):
        require(
            is_digest(lock["installerCommit"], 40) and is_digest(lock["upstreamCommit"], 40)
            and isinstance(lock["dotnetSdk"], str) and re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", lock["dotnetSdk"]),
            "Pin the UMFCI installer, upstream engine commit and exact .NET SDK",
        )
    return lock


def build_commands():
    require(COMMAND_FILE.is_file(), "The protected Development build command file is missing")
    document = json_object(COMMAND_FILE)
    require(document["schemaVersion"] == 1 and set(document["commands"]) == COMMANDS,
            "The private command configuration must provide every Development CI phase")
    for name, argv in document["commands"].items():
        require(
            isinstance(argv, list) and argv and all(isinstance(arg, str) and arg and "\x00" not in arg for arg in argv),
            f"{name}: expected a nonempty argument vector for an implemented command",
        )
    return document["commands"]


def validate_configuration():
    if not private_dispatch():
        engine_lock()
        return
    issues = []
    for check in (lambda: engine_lock(full=True), build_commands):
        try:
            check()
        except (KeyError, OSError, TypeError, ValueError) as error:
            issues.append(str(error))
    require(not issues, "\n".join(issues))


def artifact_info(kind, target, commit, artifact_id, name, token, *, check_age=True):
    require(re.fullmatch(r"[1-9][0-9]*", str(artifact_id)), f"{kind}: missing artifact ID for {target}")
    match = re.fullmatch(rf"{kind}-{target}-{commit}-([1-9][0-9]*)", name)
    require(match and int(match[1]) <= int(os.environ["GITHUB_RUN_ATTEMPT"]), f"{kind}: wrong producer artifact")
    owner = os.environ["GITHUB_REPOSITORY"]
    artifact = api(f"/repos/{owner}/actions/artifacts/{artifact_id}", token)
    require(
        artifact["name"] == name and not artifact["expired"]
        and str(artifact["workflow_run"]["id"]) == os.environ["GITHUB_RUN_ID"]
        and artifact["size_in_bytes"] > 0,
        f"{kind}: missing, expired or outside this run",
    )
    if check_age:
        age = (datetime.now(timezone.utc) - timestamp(artifact["created_at"])).total_seconds()
        require(0 <= age < 3600, f"{kind}: its one-hour consumption window expired. Re-run all jobs.")
    return artifact, int(match[1])


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def numeric(value):
    return type(value) in (float, int) and 0 <= value < float("inf")


def job_durations(token):
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    latest = {}
    for page in range(1, 12):
        jobs = api(f"/repos/{repo}/actions/runs/{run_id}/jobs?filter=all&per_page=100&page={page}", token)["jobs"]
        for job in jobs:
            if job["name"] not in latest or job["id"] > latest[job["name"]]["id"]:
                latest[job["name"]] = job
        if len(jobs) < 100:
            break
    return {
        name: (timestamp(job["completed_at"]) - timestamp(job["started_at"])).total_seconds()
        for name, job in latest.items() if job["started_at"] and job["completed_at"]
    }


def required_timings(job):
    if job.startswith("editor-"):
        return {"Engine Installation", "Toolchain Preparation", "Native Tool Compilation", "Metadata Export",
                "Editor Compilation", "Automation Tests", "Metadata Upload"}
    if job.startswith("prepare-game-"):
        return {"Engine Installation", "Toolchain Preparation", "Native Tool Compilation", "Metadata Independent Build",
                "Checkpoint Serialization", "Checkpoint Upload"}
    return {"Checkpoint Download", "Checkpoint Restoration", "Metadata Download", "Metadata Import",
            "Metadata Dependent Build", "Final Linking"}


def critical_path(durations):
    platform_times = [
        max(durations[f"Editor / {PLATFORMS[EDITORS[key]][0]}"], durations[f"Game Prepare / {label}"])
        + durations[f"Game / {label}"]
        for key, (label, _, _, _, _) in PLATFORMS.items()
    ]
    return durations["Code Quality"] + max(durations["Native Tools"], *platform_times)


def aggregate():
    started = datetime.now(timezone.utc)
    needs = json.loads(os.environ["NEEDS_JSON"])
    candidate = json.loads(os.environ.get("CI_IDENTITY") or "{}")
    version = json_object(LOCK).get("ueVersion", "Unavailable")
    token = os.environ["ARTIFACT_TOKEN"]
    lines = [
        "## Development Result", "",
        f"- Source Commit `{candidate.get('commit', 'Unavailable')}`",
        f"- PR Head `{candidate.get('head', 'Unavailable')}`",
        f"- Base Commit `{candidate.get('base', 'Unavailable')}`",
        f"- Engine Version `{version}`", "",
        "| Job | Result | Job Time (s) | Automation Tests | Metadata | Checkpoint | Artifact Bytes | Reused | Reexecuted |",
        "| --- | --- | ---: | ---: | --- | --- | ---: | ---: | ---: |",
    ]
    issues = []
    timings = []
    mobility = []
    try:
        durations = job_durations(token)
    except (OSError, KeyError, ValueError) as error:
        durations = {}
        issues.append(f"Cannot read job timings ({error})")
    for job, name in JOBS.items():
        item = needs.get(job, {})
        state = item.get("result", "missing")
        values = item.get("outputs", {})
        metrics = {}
        metadata = checkpoint = size = tests = reused = replayed = "—"
        if state != "success":
            issues.append(f"{name}: {state}")
        elif name not in durations:
            issues.append(f"{name}: missing build timing")
        if job.startswith(("editor-", "prepare-game-", "game-")):
            try:
                metrics = json.loads(values.get("metrics") or "{}")
                times = metrics["timings"]
                require(all(numeric(value) for value in times.values()), "Invalid phase timings")
                timings.extend((name, phase, seconds) for phase, seconds in times.items())
                require(required_timings(job) <= times.keys(), "Missing measured build phases")
                if job.startswith("editor-"):
                    tests = metrics["automation_tests"]
                    require(type(tests) is int and tests > 0 and metrics["metadata"] == "valid", "Missing test or metadata evidence")
                    metadata = "Valid"
                    kind, target = "metadata", job.removeprefix("editor-") + "-editor"
                elif job.startswith("prepare-game-"):
                    require(metrics["checkpoint"] == "valid", "Invalid checkpoint")
                    checkpoint = "Valid"
                    mobility.extend((name, reason, number) for reason, number in metrics["nonportable_actions"].items())
                    kind, target = "checkpoint", job.removeprefix("prepare-game-") + "-game"
                else:
                    reused, replayed = metrics["reused_actions"], metrics["reexecuted_actions"]
                    require(
                        metrics["metadata"] == metrics["checkpoint"] == "valid"
                        and type(reused) is int and reused >= 0 and type(replayed) is int and replayed >= 0,
                        "Missing successful resume evidence",
                    )
                    metadata = checkpoint = "Valid"
                    kind = None
                if kind:
                    info, _ = artifact_info(
                        kind, target, candidate["commit"], values.get("artifact-id", ""),
                        values.get("artifact-name", ""), token, check_age=False,
                    )
                    size = str(info["size_in_bytes"])
            except (KeyError, TypeError, ValueError, OSError) as error:
                if state == "success":
                    issues.append(f"{name}: {error}")
                if job.startswith("editor-") and metadata == "—":
                    metadata = "Missing or Invalid"
                if job.startswith(("prepare-game-", "game-")) and checkpoint == "—":
                    checkpoint = "Missing or Invalid"
        duration = f"{durations[name]:.2f}" if name in durations else "—"
        lines.append(f"| {name} | {state} | {duration} | {tests} | {metadata} | {checkpoint} | {size} | {reused} | {replayed} |")
    if candidate.get("private"):
        try:
            validate_live_pr(candidate, check_base=True)
        except (OSError, ValueError, KeyError) as error:
            issues.append(f"Candidate is stale or unverifiable ({error})")
    else:
        issues.append("Authorized candidate identity is missing")
    lines.extend(["", "### Build Timing", "", "| Job | Phase | Seconds |", "| --- | --- | ---: |"])
    for name, phase, seconds in timings:
        lines.append(f"| {name} | {phase} | {seconds:.2f} |")
    elapsed = (datetime.now(timezone.utc) - started).total_seconds()
    lines.extend([f"| Development Result | Result Aggregation | {elapsed:.2f} |", ""])
    if all(name in durations for name in JOBS.values()):
        lines.extend([f"**Build Critical Path Excluding Queue Time** {critical_path(durations) + elapsed:.2f} seconds", ""])
    if mobility:
        lines.extend(["### Nonportable Completed Actions", "", "| Job | Reason | Actions |", "| --- | --- | ---: |"])
        for name, reason, number in mobility:
            safe_reason = reason.replace("|", "&#124;").replace("\n", " ")
            lines.append(f"| {name} | {safe_reason} | {number} |")
        lines.append("")
    if issues:
        lines.extend(f"- {issue.replace(chr(10), ' ')}" for issue in issues)
    lines.extend(["", f"**Final Result** {'Failure' if issues else 'Success'}"])
    summary(lines)
    output("passed", "false" if issues else "true")


def publish():
    require(private_dispatch(), "Status publication requires an authorized private dispatch")
    candidate = json.loads(os.environ["CI_IDENTITY"])
    require(candidate["private"] and candidate["repository"] == REPOSITORY, "Wrong public status repository")
    validate_live_pr(candidate, check_base=True)
    success = os.environ["PASSED"] == "true"
    api(f"/repos/{REPOSITORY}/statuses/{candidate['commit']}", os.environ["STATUS_TOKEN"], {
        "state": "success" if success else "failure",
        "context": "ci/development",
        "description": "All Development CI jobs passed" if success else "Required Development CI validation failed",
    })


def cleanup():
    require(private_dispatch(), "Artifact cleanup requires the private Development run")
    candidate = json.loads(os.environ["CI_IDENTITY"])
    needs = json.loads(os.environ["NEEDS_JSON"])
    token = os.environ["ARTIFACT_TOKEN"]
    repo = os.environ["GITHUB_REPOSITORY"]
    for job, item in needs.items():
        values = item["outputs"]
        artifact_id = values.get("artifact-id")
        if job.startswith("editor-"):
            kind, target = "metadata", job.removeprefix("editor-") + "-editor"
        elif job.startswith("prepare-game-"):
            kind, target = "checkpoint", job.removeprefix("prepare-game-") + "-game"
        else:
            continue
        try:
            artifact_info(kind, target, candidate["commit"], artifact_id, values.get("artifact-name", ""),
                          token, check_age=False)
            api(f"/repos/{repo}/actions/artifacts/{artifact_id}", token, method="DELETE")
        except (OSError, KeyError, ValueError):
            print(f"::warning::Could not remove {kind} for {target}. GitHub's one-day fallback retention applies.")


def main():
    actions = {
        "resolve": resolve_candidate,
        "verify": lambda: verify_checkout(Path("source"), json.loads(os.environ["CI_IDENTITY"])),
        "validate-config": validate_configuration,
        "aggregate": aggregate,
        "publish": publish,
        "cleanup": cleanup,
    }
    actions[sys.argv[1]]()


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
