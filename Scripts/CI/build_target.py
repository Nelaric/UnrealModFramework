# Copyright (c) 2026 Nelaric Contributors
"""Run authorized engine phases and transfer their native artifacts within one run."""

from __future__ import annotations

import hashlib
import io
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path

from development import (
    COMMAND_FILE, CONTROL, EDITORS, LOCK, PLATFORMS, build_commands, engine_lock, engine_run,
    is_digest, json_object, numeric, output, require, sha256, validate_live_pr, verify_checkout,
)

LABELS = {
    "install": "Engine Installation",
    "toolchain": "Toolchain Preparation",
    "build-native-tools": "Native Tool Compilation",
    "prepare-libclang": "LibClang Preparation",
    "prepare-editor-bootstrap": "Editor Bootstrap",
    "export-metadata": "Metadata Export",
    "build-editor": "Editor Compilation",
    "test-editor": "Automation Tests",
    "prepare-game": "Metadata Independent Build",
    "save-checkpoint": "Checkpoint Serialization",
    "seal-checkpoint": "Checkpoint Serialization",
    "restore-checkpoint": "Checkpoint Restoration",
    "import-metadata": "Metadata Import",
}


def write_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")


def count(value):
    return type(value) is int and value >= 0


def verify_reuse(checkpoint, game):
    reused = game["reused_actions"]
    replayed = game["reexecuted_actions"]
    nonportable = sum(checkpoint["nonportable_actions"].values())
    require(
        count(reused) and count(replayed)
        and reused == checkpoint["completed_actions"] - nonportable and replayed <= nonportable
        and type(game["reexecuted_portable_actions"]) is int and game["reexecuted_portable_actions"] == 0
        and type(game["pending_actions"]) is int and game["pending_actions"] == 0,
        "Game resume must reuse every valid portable action and finish all pending actions",
    )


def handoff_evidence(directory, kind, expected):
    record = json_object(directory / "envelope.json")
    require(record["identity"] == expected, f"{kind}: handoff identity mismatch")
    require(0 <= time.time() - record["sealed_at"] < 3600,
            f"{kind}: its one-hour consumption window expired. Re-run all jobs.")
    return record["evidence"]


class Target:
    def __init__(self):
        require(engine_run(), "Engine and artifact operations require an authorized Development run")
        self.candidate = json.loads(os.environ["CI_IDENTITY"])
        require(self.candidate["engine"], "Candidate was not authorized for an engine build")
        self.slug = os.environ["CI_TARGET"]
        self.key, self.profile = self.slug.rsplit("-", 1)
        self.editor = EDITORS[self.key] + "-editor"
        self.lock = engine_lock(full=True)
        self.entry = self.lock["targets"][self.slug]
        self.lock_hash = sha256(LOCK)
        self.command_hash = sha256(COMMAND_FILE)
        self.commands = build_commands()
        self.source = Path("source").resolve()
        self.installer = Path("umfci").resolve()
        self.work = Path(os.environ["RUNNER_TEMP"]).resolve() / "umf-development"
        self.sdk = self.work / "sdk"
        self.evidence = self.work / "evidence"
        self.report = self.work / "automation"
        self.metrics = self.work / "metrics.json"

    def configure(self):
        verify_checkout(self.source, self.candidate)
        validate_live_pr(self.candidate)
        _, runner, host, arch, _ = PLATFORMS[self.key]
        native = {"amd64": "x64", "x86_64": "x64", "aarch64": "arm64", "arm64": "arm64"}
        require(
            runner == os.environ["CI_RUNNER"] and host == platform.system()
            and arch == native.get(platform.machine().lower()) and arch == os.environ["RUNNER_ARCH"].lower(),
            "Runner OS, label or process architecture differs from the locked native target",
        )
        self.work.mkdir()
        for name in ("evidence", "automation", "metadata", "checkpoint"):
            (self.work / name).mkdir()
        write_json(self.metrics, {"timings": {}})
        output("installer", self.lock["installerCommit"])
        output("dotnet", self.lock["dotnetSdk"])
        for kind in ("metadata", "checkpoint"):
            output(f"{kind}-path", (self.work / kind).as_posix())

    def identity(self, target):
        return {
            "target": target,
            "source_commit": self.candidate["commit"],
            "engine_lock_sha256": self.lock_hash,
            "engine_version": self.lock["ueVersion"],
            "configuration": self.lock["configuration"],
            "architecture": self.lock["targets"][target]["architecture"],
        }

    def transport_identity(self, target):
        return {
            **self.identity(target),
            "engine_manifest_sha256": self.lock["targets"][target]["manifestSha256"],
            "command_sha256": self.command_hash,
        }

    def record(self, path, target):
        value = json_object(path)
        require(all(value[key] == expected for key, expected in self.identity(target).items()),
                f"{path.name}: build identity mismatch")
        require(is_digest(value["input_sha256"]), f"{path.name}: missing build input digest")
        return value

    def require_toolchain(self, reported):
        for key, value in self.lock["toolchains"][self.entry["toolchain"]].items():
            actual = reported.get(key)
            if key == "allowedCompilers":
                require(reported.get("compiler") in value, "Windows compiler is outside the locked toolchain")
            else:
                require(actual == value, f"SDK toolchain mismatch: {key}")

    def installer_target(self):
        revision = subprocess.check_output(["git", "-C", str(self.installer), "rev-parse", "HEAD"], text=True).strip()
        require(revision == self.lock["installerCommit"], "UMFCI checkout differs from the immutable installer lock")
        sys.path.insert(0, str(self.installer / "Scripts"))
        from targets import select

        target = select(self.slug)
        target.require_native_host()
        expected = {
            "platform": target.platform, "architecture": self.entry["architecture"], "profile": self.entry["profile"],
            "configuration": self.lock["configuration"],
        }
        require(target.as_dict() == expected and target.tag(self.entry["recipe"]) == self.entry["release"],
                "UMFCI target does not derive the locked release tag")
        return target, expected

    def install(self):
        target, expected = self.installer_target()
        from install import PrivateRelease, install, metadata

        release = PrivateRelease(self.lock["repository"], self.entry["release"])
        require(release.release.get("immutable") is True and not release.release.get("draft")
                and not release.release.get("prerelease"), "The locked SDK release is not immutable and published")
        data = metadata(release.open, "manifest.json")
        require(hashlib.sha256(data).hexdigest() == self.entry["manifestSha256"], "SDK manifest digest mismatch")
        manifest = json.loads(data)
        require(
            (manifest["ue_version"], manifest["epic_upstream_commit"], manifest["dotnet_sdk_version"], manifest["target"])
            == (self.lock["ueVersion"], self.lock["upstreamCommit"], self.lock["dotnetSdk"], expected)
            and type(manifest["installed_bytes"]) is int
            and 0 < manifest["installed_bytes"] <= self.lock["maxInstalledBytes"],
            "SDK manifest identity or installed size differs from the lock",
        )
        self.require_toolchain(manifest["toolchain"])
        os.environ.pop("GH_TOKEN", None)
        os.environ.pop("GITHUB_TOKEN", None)

        def open_asset(name):
            return io.BytesIO(data) if name == "manifest.json" else release.open(name)

        install(open_asset, target, self.entry["recipe"], self.sdk)
        write_json(self.work / "sdk-toolchain.json", manifest["toolchain"])
        self.verify_sdk()

    def verify_sdk(self):
        version = json_object(self.sdk / "Engine/Build/Build.version")
        actual = ".".join(str(version[field]) for field in ("MajorVersion", "MinorVersion", "PatchVersion"))
        require(actual == self.lock["ueVersion"], "Installed SDK reports a different Unreal version")
        file_stats = (path.lstat() for path in self.sdk.rglob("*"))
        size = sum(info.st_size for info in file_stats if stat.S_ISREG(info.st_mode))
        require(0 < size <= self.lock["maxInstalledBytes"], f"Installed SDK is outside the byte limit: {size}")

    def toolchain(self):
        target, _ = self.installer_target()
        from platforms import configure

        toolchain, _ = configure(target, self.work)
        self.require_toolchain(toolchain)
        sdk_toolchain = json_object(self.work / "sdk-toolchain.json")
        dotnet_bin = shutil.which("dotnet")
        require(dotnet_bin, "The locked .NET SDK is not installed")
        dotnet = Path(dotnet_bin).resolve()
        require(
            subprocess.check_output([dotnet, "--version"], cwd=self.installer, text=True).strip() == self.lock["dotnetSdk"],
            "Active .NET SDK differs from the lock",
        )
        names = ("LINUX_MULTIARCH_ROOT", "DEVELOPER_DIR")
        environment = {key: os.environ[key] for key in names if key in os.environ}
        if self.key.startswith("windows-"):
            actual = tuple(map(int, toolchain["compiler"].split(".")))
            minimum = tuple(map(int, sdk_toolchain["compiler"].split(".")))
            require(actual >= minimum,
                    "Windows compiler is older than the SDK compiler")
            environment.update(UMF_MSVC_VERSION=toolchain["compiler"], UMF_WINDOWS_SDK_VERSION=toolchain["sdk"])
        environment.update(DOTNET_ROOT=str(dotnet.parent), UE_USE_SYSTEM_DOTNET="1")
        write_json(self.work / "toolchain.json", toolchain)
        write_json(self.work / "toolchain-env.json", environment)

    def command(self, name):
        variables = {
            "source": str(self.source), "control": str(CONTROL), "sdk": str(self.sdk), "work": str(self.work),
            "evidence": str(self.evidence), "target": self.slug, "architecture": self.entry["architecture"],
            "ubt_platform": self.entry["ubtPlatform"], "ubt_architecture": self.entry["ubtArchitecture"],
            "configuration": self.lock["configuration"], "commit": self.candidate["commit"], "lock_sha256": self.lock_hash,
            "engine_version": self.lock["ueVersion"], "upstream_commit": self.lock["upstreamCommit"],
            "metadata_target": self.editor, "automation": str(self.report),
            "metadata_payload": str(self.work / "metadata" / "payload"),
            "checkpoint_payload": str(self.work / "checkpoint" / "payload"),
            "toolchain": str(self.work / "toolchain.json"),
        }
        env = {**os.environ, **{f"UMF_{key.upper()}": value for key, value in variables.items()}}
        environment = self.work / "toolchain-env.json"
        if environment.is_file():
            env.update(json_object(environment))
        for token in ("GH_TOKEN", "GITHUB_TOKEN", "PUBLIC_READ_TOKEN", "STATUS_TOKEN", "SDK_TOKEN"):
            env.pop(token, None)
        argv = [argument.format_map(variables) for argument in self.commands[name]]
        subprocess.run(argv, cwd=self.source, env=env, check=True)

    def verified_metadata(self):
        producer = None
        if self.profile == "game":
            producer = handoff_evidence(self.work / "metadata", "metadata", self.transport_identity(self.editor))
        path = self.evidence / "metadata.json"
        path.unlink(missing_ok=True)
        self.command("validate-metadata")
        report = self.record(path, self.editor)
        require(report["bundle_valid"] is True, "Project metadata validation failed")
        if producer is not None:
            require(report == producer, "Metadata validated by the consumer differs from its producer")
        self.metrics_update(metadata="valid")

    def verified_checkpoint(self):
        path = self.evidence / "checkpoint.json"
        path.unlink(missing_ok=True)
        self.command("validate-checkpoint")
        report = self.record(path, self.slug)
        require(
            report["toolchain_sha256"] == sha256(self.work / "toolchain.json") and is_digest(report["build_manifest_sha256"])
            and all(report[key] is True for key in ("paths_valid", "native_tools_runnable", "inputs_valid", "actions_valid"))
            and all(count(report[key]) for key in ("completed_actions", "pending_actions"))
            and report["completed_actions"] > 0 and report["pending_actions"] > 0
            and type(report["metadata_independent_pending_actions"]) is int
            and report["metadata_independent_pending_actions"] == 0,
            "Checkpoint has invalid state or metadata-independent work remaining",
        )
        nonportable = report["nonportable_actions"]
        require(
            isinstance(nonportable, dict) and all(reason.strip() and count(num) and num > 0 for reason, num in nonportable.items())
            and sum(nonportable.values()) <= report["completed_actions"],
            "Checkpoint has unaccounted nonportable actions",
        )
        envelope = self.work / "checkpoint" / "envelope.json"
        if envelope.is_file():
            require(report == json_object(envelope)["evidence"], "Restored checkpoint differs from its producer")
        self.metrics_update(checkpoint="valid", nonportable_actions=nonportable)

    def verified_editor(self):
        sys.path.insert(0, str(CONTROL / "Scripts"))
        from check_automation_report import check_development_report

        self.metrics_update(automation_tests=check_development_report(self.report / "index.json"))

    def run_automation(self):
        editor = {
            "windows": "Win64/UnrealEditor-Cmd.exe",
            "linux": "Linux/UnrealEditor",
            "macos": "Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor",
        }[self.key.split("-", 1)[0]]
        project = self.source / "UnrealModFramework/UnrealModFrameworkDev.uproject"
        executable = self.sdk / "Engine" / "Binaries" / editor
        subprocess.run(
            [str(executable), str(project), "-unattended", "-nullrhi", "-nosound", "-nosplash",
             "-ExecCmds=Automation RunTests StartsWith:Nelaric.;SoftQuit", f"-ReportExportPath={self.report}",
             f"-abslog={self.report / 'automation.log'}", "-stdout", "-FullStdOutLogOutput"],
            cwd=project.parent, check=True,
        )

    def verified_game(self):
        self.command("verify-game")
        game = self.record(self.evidence / "game.json", self.slug)
        checkpoint = json_object(self.evidence / "checkpoint.json")
        metadata = json_object(self.evidence / "metadata.json")
        require(
            game["input_sha256"] == checkpoint["input_sha256"]
            and game["metadata_input_sha256"] == metadata["input_sha256"] and game["link_type"] == "Monolithic",
            "Game build inputs, imported metadata or link type differ from the resumed Development target",
        )
        require(game["link_receipt_valid"] is True, "Host link receipt verification failed")
        verify_reuse(checkpoint, game)

        def checked_file(path, root):
            path = (root / path).resolve(strict=True)
            require(path.is_relative_to(root) and path.is_file() and path.stat().st_size > 0,
                    "Expected a verified file under the CI source or work directory")
            return path

        json_object(checked_file(game["link_receipt"], self.source))
        mbt = checked_file(game["mbt"], self.work)
        binary = checked_file(game["binary"], self.source)
        subprocess.run(
            [str(mbt), "inspect-binary", "--file", str(binary), "--platform", self.entry["ubtPlatform"],
             "--architecture", self.entry["architecture"], "--kind", "Executable"],
            cwd=self.source, check=True,
        )
        for field, label in (("metadata_dependent_seconds", "Metadata Dependent Build"), ("link_seconds", "Final Linking")):
            require(numeric(game[field]), f"Missing measured duration: {label}")
            self.add_time(label, game[field])
        self.metrics_update(reused_actions=game["reused_actions"], reexecuted_actions=game["reexecuted_actions"])

    def seal(self, kind):
        validate_live_pr(self.candidate)
        directory = self.work / kind
        record = json_object(self.evidence / f"{kind}.json")
        write_json(directory / "envelope.json", {
            "sealed_at": time.time(), "identity": self.transport_identity(self.slug), "evidence": record,
        })

    def restore_checkpoint(self):
        evidence = handoff_evidence(self.work / "checkpoint", "checkpoint", self.transport_identity(self.slug))
        require(evidence["toolchain_sha256"] == sha256(self.work / "toolchain.json"),
                "Checkpoint toolchain differs from the toolchain on the resume runner")
        self.command("restore-checkpoint")

    def mark(self, label, end=False):
        path = self.work / f"{label}.time"
        if end:
            self.add_time(label, time.monotonic() - float(path.read_text()))
        else:
            path.write_text(str(time.monotonic()))

    def metrics_update(self, **metrics):
        current = json_object(self.metrics)
        current.update(metrics)
        write_json(self.metrics, current)

    def add_time(self, label, seconds):
        current = json_object(self.metrics)
        timings = current["timings"]
        timings[label] = timings.get(label, 0) + seconds
        write_json(self.metrics, current)

    def step(self, name):
        actions = {
            "configure": self.configure, "install": self.install, "toolchain": self.toolchain,
            "validate-metadata": self.verified_metadata, "validate-checkpoint": self.verified_checkpoint,
            "test-editor": self.run_automation, "verify-editor": self.verified_editor, "verify-game": self.verified_game,
            "seal-metadata": lambda: self.seal("metadata"), "seal-checkpoint": lambda: self.seal("checkpoint"),
            "restore-checkpoint": self.restore_checkpoint,
            "metrics": lambda: output("value", json.dumps(json_object(self.metrics), separators=(",", ":"), ensure_ascii=True)),
        }
        started = time.monotonic()
        if name in {"install", "resume-game"}:
            validate_live_pr(self.candidate)
        if name in actions:
            actions[name]()
        else:
            self.command(name)
        if name in LABELS:
            self.add_time(LABELS[name], time.monotonic() - started)


def test_native_development():
    require(engine_run(), "Native Development protocol tests require an eligible Development run")
    command = build_commands()["test-native-development"]
    source = Path("source").resolve()
    native = Path(os.environ["RUNNER_TEMP"]).resolve() / "umf-native" / "bin" / "Release"
    argv = [arg.format_map({"source": str(source), "control": str(CONTROL), "native": str(native)}) for arg in command]
    subprocess.run(argv, cwd=source, check=True)


def main():
    name = sys.argv[1]
    if name == "test-native-development":
        test_native_development()
        return
    target = Target()
    if name in {"start-timing", "finish-timing"}:
        target.mark(sys.argv[2], end=name == "finish-timing")
    else:
        target.step(name)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
