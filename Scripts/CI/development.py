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
UMFCI_REPOSITORY = "Nelaric/UMFCI"
APPROVAL_TAG_PREFIX = "development-ci-"
ENGINE_VERSION = "5.6.1"
ENGINE_CONFIGURATION = "Development"
PLATFORMS = {
    "windows-x64": ("Windows x86_64", "windows-2022", "Windows", "x64", "Win64"),
    "windows-arm64": ("Windows ARM64", "windows-11-arm", "Windows", "arm64", "Win64"),
    "linux-x64": ("Linux x86_64", "ubuntu-24.04", "Linux", "x64", "Linux"),
    "linux-arm64": ("Linux ARM64", "ubuntu-24.04-arm", "Linux", "arm64", "LinuxArm64"),
    "macos-x64": ("MacOS x86_64", "macos-15-intel", "Darwin", "x64", "Mac"),
    "macos-arm64": ("MacOS ARM64", "macos-15", "Darwin", "arm64", "Mac"),
}
EDITORS = {key: key.replace("windows-arm64", "windows-x64").replace("linux-arm64", "linux-x64") for key in PLATFORMS}
JOBS = {"code-quality": "Code Quality", "native-tools": "Native Tools"}
JOBS.update({f"editor-{key}": f"Editor / {PLATFORMS[key][0]}" for key in dict.fromkeys(EDITORS.values())})
JOBS.update({f"prepare-game-{key}": f"Game Prepare / {value[0]}" for key, value in PLATFORMS.items()})
JOBS.update({f"game-{key}": f"Game / {value[0]}" for key, value in PLATFORMS.items()})

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


def open_api(path, token="", body=None, method=None, *, accept="application/vnd.github+json", timeout=30):
    headers = {
        "Accept": accept,
        "User-Agent": "umf-development-ci",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    encoded = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        encoded = json.dumps(body).encode()
    request = Request(os.environ["GITHUB_API_URL"] + path, headers=headers, data=encoded, method=method)
    if token:
        request.add_unredirected_header("Authorization", f"Bearer {token}")
    return urlopen(request, timeout=timeout)


def api(path, token="", body=None, method=None):
    with open_api(path, token, body, method) as response:
        return json.load(response) if response.status != 204 else None


def event():
    return json_object(os.environ["GITHUB_EVENT_PATH"])


def approval_ref(pr_number):
    return f"refs/tags/{APPROVAL_TAG_PREFIX}{pr_number}"


def approved_pr_number(payload):
    if payload["repository"]["full_name"] != REPOSITORY:
        return 0
    ref = os.environ["GITHUB_REF"]
    match = re.fullmatch(rf"refs/tags/{APPROVAL_TAG_PREFIX}([1-9][0-9]*)", ref)
    if not match:
        return 0
    event_name = os.environ["GITHUB_EVENT_NAME"]
    if event_name == "workflow_dispatch":
        return int(match[1])
    if (event_name == "push" and payload.get("ref") == ref and not payload.get("deleted")
            and payload.get("after") == os.environ["GITHUB_SHA"]):
        return int(match[1])
    return 0


def engine_run(payload=None):
    return bool(approved_pr_number(event() if payload is None else payload))


def validate_live_pr(candidate, *, check_base=False, pr=None):
    repo = candidate["repository"]
    token = os.environ.get("PUBLIC_READ_TOKEN", "")
    if pr is None:
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


def validate_approval(candidate):
    ref = approval_ref(candidate["pr"])
    require(candidate["repository"] == REPOSITORY and candidate["pr"] > 0
            and os.environ["GITHUB_REF"] == ref and os.environ["GITHUB_SHA"] == candidate["commit"],
            "The workflow ref and commit must match the Development approval")
    token = os.environ.get("PUBLIC_READ_TOKEN", "")
    target = api(f"/repos/{REPOSITORY}/git/ref/{ref.removeprefix('refs/')}", token)["object"]
    require(target["type"] == "commit" and target["sha"] == candidate["commit"],
            "The Development approval tag has moved or was replaced")


def resolve_candidate():
    payload = event()
    repo = os.environ["GITHUB_REPOSITORY"]
    commit = os.environ["GITHUB_SHA"]
    require(is_digest(commit, 40), "Expected an exact candidate commit SHA")
    approved_pr = approved_pr_number(payload)
    candidate = {"repository": repo, "commit": commit, "pr": 0, "base": "", "head": "", "engine": bool(approved_pr)}
    live_pr = None
    if approved_pr:
        live_pr = api(f"/repos/{repo}/pulls/{approved_pr}", os.environ.get("PUBLIC_READ_TOKEN", ""))
        candidate.update(pr=approved_pr, base=live_pr["base"]["sha"], head=live_pr["head"]["sha"])
        validate_approval(candidate)
    elif os.environ["GITHUB_EVENT_NAME"] == "pull_request":
        pr = payload["pull_request"]
        candidate.update(pr=payload["number"], base=pr["base"]["sha"], head=pr["head"]["sha"])
    if candidate["pr"]:
        require(is_digest(candidate["head"], 40) and is_digest(candidate["base"], 40), "Invalid PR head or base SHA")
        validate_live_pr(candidate, check_base=True, pr=live_pr)
    output("source", candidate["repository"])
    output("commit", candidate["commit"])
    output("identity", json.dumps(candidate, separators=(",", ":")))
    output("pr", candidate["pr"])
    output("engine", str(candidate["engine"]).lower())


def verify_checkout(source, candidate):
    def git(*args):
        return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()

    require(git("rev-parse", "HEAD") == candidate["commit"], "The checkout differs from the reviewed candidate")
    if candidate["pr"]:
        parents = git("show", "-s", "--format=%P", "HEAD").split()
        require(parents == [candidate["base"], candidate["head"]], "PR merge parents differ from the reviewed base and head")


def engine_release_tag():
    tag = os.environ.get("UMFCI_RELEASE_TAG", "")
    require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", tag) and tag.lower() != "latest",
            "Set UMFCI_RELEASE_TAG to an explicit UMFCI release tag")
    return tag


def build_commands():
    require(COMMAND_FILE.is_file(), "The Development build command file is missing")
    document = json_object(COMMAND_FILE)
    require(document["schemaVersion"] == 1 and set(document["commands"]) == COMMANDS,
            "The command configuration must provide every Development CI phase")
    for name, argv in document["commands"].items():
        require(
            isinstance(argv, list) and argv and all(isinstance(arg, str) and arg and "\x00" not in arg for arg in argv),
            f"{name}: expected a nonempty argument vector for an implemented command",
        )
    return document["commands"]


def validate_configuration():
    engine_release_tag()
    if engine_run():
        build_commands()


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


def artifact_sizes(token):
    repo = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    sizes = {}
    for page in range(1, 12):
        artifacts = api(f"/repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100&page={page}", token)["artifacts"]
        sizes.update({
            str(artifact["id"]): artifact["size_in_bytes"] for artifact in artifacts
            if not artifact["expired"] and artifact["size_in_bytes"] > 0
        })
        if len(artifacts) < 100:
            break
    return sizes


def required_timings(job):
    if job.startswith("editor-"):
        return {"Engine Installation", "Toolchain Preparation", "Native Tool Compilation", "Metadata Export",
                "Editor Compilation", "Automation Tests", "Metadata Upload"}
    if job.startswith("prepare-game-"):
        return {"Engine Installation", "Toolchain Preparation", "Native Tool Compilation", "Metadata Independent Build",
                "Checkpoint Serialization", "Checkpoint Upload"}
    return {"Engine Installation", "Toolchain Preparation", "Checkpoint Download", "Checkpoint Restoration",
            "Metadata Download", "Metadata Import", "Metadata Dependent Build", "Final Linking"}


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
    token = os.environ["ACTIONS_READ_TOKEN"]
    lines = [
        "## Development Result", "",
        f"- Source Commit `{candidate.get('commit', 'Unavailable')}`",
        f"- PR Head `{candidate.get('head', 'Unavailable')}`",
        f"- Base Commit `{candidate.get('base', 'Unavailable')}`",
        f"- Engine Version `{ENGINE_VERSION}`",
        f"- UMFCI Release `{os.environ.get('UMFCI_RELEASE_TAG', 'Unavailable')}`", "",
        "| Job | Result | Job Time (s) | Automation Tests | Metadata | Checkpoint | Artifact Bytes | Reused | Reexecuted |",
        "| --- | --- | ---: | ---: | --- | --- | ---: | ---: | ---: |",
    ]
    issues = []
    timings = []
    mobility = []
    consumed_artifacts = []
    try:
        durations = job_durations(token)
    except (OSError, KeyError, ValueError) as error:
        durations = {}
        issues.append(f"Cannot read job timings ({error})")
    try:
        sizes = artifact_sizes(token)
    except (OSError, KeyError, ValueError) as error:
        sizes = {}
        issues.append(f"Cannot read artifact sizes ({error})")
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
                    kind = "Metadata"
                elif job.startswith("prepare-game-"):
                    require(metrics["checkpoint"] == "valid", "Invalid checkpoint")
                    checkpoint = "Valid"
                    mobility.extend((name, reason, number) for reason, number in metrics["nonportable_actions"].items())
                    kind = "Checkpoint"
                else:
                    reused, replayed = metrics["reused_actions"], metrics["reexecuted_actions"]
                    require(
                        metrics["metadata"] == metrics["checkpoint"] == "valid"
                        and type(reused) is int and reused >= 0 and type(replayed) is int and replayed >= 0,
                        "Missing successful resume evidence",
                    )
                    metadata = checkpoint = "Valid"
                    kind = None
                if kind and state == "success":
                    artifact_id = values.get("artifact-id", "")
                    require(artifact_id in sizes, f"Missing or expired {kind} artifact")
                    size = sizes[artifact_id]
                    consumed_artifacts.append(artifact_id)
            except (KeyError, TypeError, ValueError, OSError, subprocess.CalledProcessError) as error:
                if state == "success":
                    issues.append(f"{name}: {error}")
                if job.startswith("editor-") and metadata == "—":
                    metadata = "Missing or Invalid"
                if job.startswith(("prepare-game-", "game-")) and checkpoint == "—":
                    checkpoint = "Missing or Invalid"
        duration = f"{durations[name]:.2f}" if name in durations else "—"
        lines.append(f"| {name} | {state} | {duration} | {tests} | {metadata} | {checkpoint} | {size} | {reused} | {replayed} |")
    if not candidate.get("engine"):
        issues.append("Authorized engine validation is required for this candidate")
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
    summary(lines)
    if not issues:
        output("artifact-ids", " ".join(consumed_artifacts))
    output("passed", "false" if issues else "true")


def verify_current_candidate():
    require(engine_run(), "A Development result requires an authorized engine build")
    candidate = json.loads(os.environ["CI_IDENTITY"])
    require(candidate["engine"] and candidate["repository"] == REPOSITORY, "Missing authorized PR candidate")
    try:
        validate_live_pr(candidate, check_base=True)
        validate_approval(candidate)
    except (OSError, ValueError, KeyError) as error:
        summary([f"- Candidate is stale or unverifiable ({str(error).replace(chr(10), ' ')})"])
        raise


def cleanup():
    require(engine_run(), "Artifact cleanup requires an authorized Development build")
    repo = os.environ["GITHUB_REPOSITORY"]
    token = os.environ["ACTIONS_TOKEN"]
    for artifact_id in os.environ["ARTIFACT_IDS"].split():
        try:
            api(f"/repos/{repo}/actions/artifacts/{artifact_id}", token, method="DELETE")
        except OSError as error:
            print(f"::warning::Could not remove artifact {artifact_id} ({error})")


def main():
    actions = {
        "resolve": resolve_candidate,
        "verify": lambda: verify_checkout(Path("source"), json.loads(os.environ["CI_IDENTITY"])),
        "validate-config": validate_configuration,
        "aggregate": aggregate,
        "verify-current-candidate": verify_current_candidate,
        "cleanup": cleanup,
    }
    actions[sys.argv[1]]()


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
