# Copyright (c) 2026 Nelaric Contributors

import argparse
import ctypes
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from game_checkpoint import (
    LINK_TYPES, ActionGraph, file_sha, inventory, logical_path, project_inputs, read_json, require,
    restore_objects, save_actions, validate_actions, verify_inventory, verify_pch_objects, write_json,
)

TOOLS = ("ModBuildTool", "ModHeaderTool", "ModSourceTool")
SUFFIX = ".exe" if os.name == "nt" else ""
CLANG = {"Windows": "bin/libclang.dll", "Darwin": "lib/libclang.dylib", "Linux": "lib/libclang.so"}
PLATFORMS = {"windows": "Win64", "linux": "Linux", "macos": "Mac"}
ARCHITECTURES = {"x64": "x86_64", "arm64": "ARM64"}
ROOT = Path(__file__).resolve().parents[1]


def supplied(name):
    value = os.environ.get(f"UMF_{name.upper()}")
    require(value, f"Missing UMF_{name.upper()}")
    return value


def run(arguments, **options):
    return subprocess.run([str(value) for value in arguments], check=True, **options)


def clang_location(directory):
    library = directory / CLANG[platform.system()]
    resources = sorted(path.parent for path in (directory / "lib/clang").glob("*/include")
                       if (path / "stddef.h").is_file())
    require(library.is_file() and len(resources) == 1, f"Incomplete libclang runtime at {directory}")
    return library, resources[0]


def tool_path(native, name):
    return native / "bin" / (name + SUFFIX)


class Build:
    def __init__(self):
        self.source = Path(supplied("source")).resolve()
        projects = sorted((self.source / "UnrealModFramework").glob("*.uproject"))
        require(len(projects) == 1, "Expected one Development project below UnrealModFramework")
        self.project = projects[0]
        self.tools = self.project.parent / "Plugins/ModRuntime/Tools"
        self.work = Path(supplied("work")).resolve()
        self.sdk = Path(supplied("sdk")).resolve()
        require((self.sdk / "Engine").is_dir(), f"Expected an installed SDK at {self.sdk}")
        self.evidence = Path(supplied("evidence")).resolve()
        self.metadata = Path(supplied("metadata_payload")).resolve()
        self.checkpoint = Path(supplied("checkpoint_payload")).resolve()
        self.native = self.work / "native"
        self.mbt = tool_path(self.native, "ModBuildTool")
        self.target = supplied("target")
        self.producer = supplied("metadata_target")
        self.arch = supplied("ubt_architecture")
        self.ubt_platform = supplied("ubt_platform")
        self.configuration = supplied("configuration")
        self.commit = supplied("commit")
        self.upstream = supplied("upstream_commit")
        self.toolchain = Path(supplied("toolchain")).resolve()
        self.work.mkdir(parents=True, exist_ok=True)
        expected_platform = "LinuxArm64" if self.target.startswith("linux-arm64-") else PLATFORMS[self.target.split("-")[0]]
        require(self.ubt_platform == expected_platform and self.arch == self.target.split("-")[1]
                and supplied("architecture") == ARCHITECTURES[self.arch] and self.configuration == "Development",
                "The CI target does not match the supplied UBT build identity")
        require(all(not location.is_relative_to(self.sdk) for location in
                    (self.source, self.work, self.evidence, self.metadata, self.checkpoint)),
                "Build outputs must be outside the engine SDK")

    def report(self, target, sha):
        return {"target": target, "source_commit": self.commit, "engine_lock_sha256": supplied("lock_sha256"),
                "engine_version": supplied("engine_version"), "configuration": self.configuration,
                "architecture": ARCHITECTURES[target.split("-")[1]], "input_sha256": sha}

    def target_args(self):
        target = self.project.stem + ("Editor" if self.target.endswith("-editor") else "")
        return ["--engine", self.sdk, "--project", self.project, "--target", target,
                "--platform", self.ubt_platform, "--architecture", self.arch,
                "--configuration", self.configuration]

    def metadata_args(self, option):
        return [option, self.metadata, "--commit", self.commit, "--engine-commit", self.upstream]

    def call(self, command, *arguments):
        library, resources = clang_location(self.native / "clang")
        environment = {**os.environ, "UMF_MHT_EXECUTABLE": str(tool_path(self.native, "ModHeaderTool")),
                       "UMF_MOD_SOURCE_TOOL_EXECUTABLE": str(tool_path(self.native, "ModSourceTool")),
                       "UMF_MHT_LIBCLANG": str(library), "UMF_MHT_CLANG_RESOURCE_DIR": str(resources)}
        return run([self.mbt, command, *arguments], cwd=self.source, env=environment)

    def require_game(self):
        require(self.target.endswith("-game"), f"Expected a Game target, received {self.target}")

    def require_editor(self):
        require(self.target.endswith("-editor") and self.producer == self.target,
                f"Expected the assigned Editor producer, received {self.target}")

    def build_native_tools(self):
        staging = self.work / "native-build"
        script = self.tools / ("Build.bat" if os.name == "nt" else "Build.sh")
        command = [script] if os.name == "nt" else ["sh", script]
        run([*command, "--build-dir", staging, "--target", "ModRuntimeUhtTools", "--jobs", "4"], cwd=self.source)
        (self.native / "bin").mkdir(parents=True, exist_ok=True)
        for name in TOOLS:
            shutil.copy2(staging / "bin/Release" / (name + SUFFIX), tool_path(self.native, name))

    def prepare_libclang(self):
        run(["cmake", "-P", self.tools / "SetupLibClang.cmake"], cwd=self.source)
        arch = "arm64" if platform.machine().lower() in {"aarch64", "arm64"} else "x64"
        receipt = self.tools / "build" / f"libclang-{platform.system()}-{arch}.path"
        library = (receipt.parent / receipt.read_text(encoding="utf-8").strip()).resolve(strict=True)
        shutil.copytree(library.parent.parent, self.native / "clang", dirs_exist_ok=True)
        self.check_native(self.native)

    def check_native(self, native):
        for name in TOOLS:
            executable = tool_path(native, name)
            require(executable.is_file() and not executable.is_symlink(), f"Missing native tool {name}")
            executable.chmod(executable.stat().st_mode | 0o100)
            run([executable, "--help"], stdout=subprocess.DEVNULL)
        library, _ = clang_location(native / "clang")
        require(bool(ctypes.CDLL(str(library)).clang_getClangVersion), "libclang is missing its version entry point")

    def prepare_editor_bootstrap(self):
        self.require_editor()
        self.call("prepare-editor-bootstrap", *self.target_args(), *self.metadata_args("--output"))

    def export_metadata(self):
        self.require_editor()
        self.call("export-metadata", *self.target_args(), *self.metadata_args("--output"))

    def validate_metadata(self):
        self.call("validate-metadata", *self.target_args(), *self.metadata_args("--metadata"))
        return self.metadata_identity() | {"bundle_valid": True}

    def metadata_identity(self):
        manifest = self.metadata / "manifest.json"
        platform_name, architecture, profile = self.producer.split("-")
        require(profile == "editor" and read_json(manifest)["producer"] == {
            "name": self.project.stem + "Editor", "platform": PLATFORMS[platform_name],
            "architecture": architecture, "configuration": self.configuration,
        }, "Metadata Bundle was exported by a different Editor producer")
        return self.report(self.producer, file_sha(manifest))

    def record_metadata(self):
        write_json(self.evidence / "metadata.json", self.validate_metadata())

    def build_editor(self):
        self.require_editor()
        self.call("build-ue-target", *self.target_args(), *self.metadata_args("--metadata"), "--mode", "normal")

    def graph(self, phase):
        path = self.work / f"{phase}-graph.json"
        self.call("export-ue-actions", *self.target_args(), "--phase", phase, "--actions", path)
        return self.read_graph(path)

    def read_graph(self, path):
        return ActionGraph(read_json(path), self.project.parent)

    def execute(self, graph_path):
        self.call("execute-ue-actions", *self.target_args(), "--actions", graph_path)

    def prepare_game(self):
        self.require_game()
        ready = self.work / "prepare-complete.json"
        ready.unlink(missing_ok=True)
        graph = self.graph("prepare")
        early, deferred = graph.without_metadata()
        require(early and deferred, "Expected both independent work and Metadata-dependent work")
        print(f"Game Prepare will execute {len(early)} of {len(graph.actions)} actions.", flush=True)
        graph.execute(early, self.work / "prepare-actions.json", self.execute)
        graph.check_outputs(early)
        write_json(ready, {"graph_sha256": file_sha(self.work / "prepare-graph.json"),
                          "source_sha256": project_inputs(self.project.parent)})

    def save_checkpoint(self):
        self.require_game()
        path = self.work / "prepare-graph.json"
        ready = read_json(self.work / "prepare-complete.json")
        require(ready["graph_sha256"] == file_sha(path)
                and ready["source_sha256"] == project_inputs(self.project.parent),
                "Project inputs or the completed Game preparation graph changed")
        self.checkpoint.parent.mkdir(parents=True, exist_ok=True)
        require(not self.checkpoint.exists() or not any(self.checkpoint.iterdir()), "Checkpoint destination must be empty")
        with tempfile.TemporaryDirectory(prefix="checkpoint-", dir=self.checkpoint.parent) as temporary:
            payload = Path(temporary) / "payload"
            shutil.copytree(self.native, payload / "native")
            actions = save_actions(self.read_graph(path), payload)
            files = inventory(payload)
            validate_actions(actions, files)
            write_json(payload / "manifest.json", {
                "schemaVersion": 1, "identity": self.report(self.target, ready["source_sha256"]),
                "toolchain_sha256": file_sha(self.toolchain), "actions": actions, "files": files,
            })
            if self.checkpoint.exists():
                self.checkpoint.rmdir()
            payload.replace(self.checkpoint)

    def load_checkpoint(self):
        self.require_game()
        manifest = self.checkpoint / "manifest.json"
        document = read_json(manifest)
        require(document["schemaVersion"] == 1, "Unsupported Game checkpoint schema")
        require(document["identity"] == self.report(self.target, project_inputs(self.project.parent)),
                "Checkpoint source inputs or build identity differ")
        require(document["toolchain_sha256"] == file_sha(self.toolchain), "Checkpoint toolchain differs")
        files = verify_inventory(self.checkpoint, document["files"])
        counts = validate_actions(document["actions"], files)
        return document, counts

    def record_checkpoint(self):
        document, counts = self.load_checkpoint()
        self.check_native(self.checkpoint / "native")
        write_json(self.evidence / "checkpoint.json", {
            **document["identity"], **counts, "toolchain_sha256": document["toolchain_sha256"],
            "build_manifest_sha256": file_sha(self.checkpoint / "manifest.json"),
            "paths_valid": True, "native_tools_runnable": True, "inputs_valid": True, "actions_valid": True,
        })

    def restore_checkpoint(self):
        document, _ = self.load_checkpoint()
        if self.native.exists():
            require(inventory(self.native) == {name.removeprefix("native/"): sha for name, sha in document["files"].items()
                                               if name.startswith("native/")}, "Restored native tool files differ")
        else:
            with tempfile.TemporaryDirectory(prefix="native-", dir=self.work) as temporary:
                staging = Path(temporary) / "native"
                shutil.copytree(self.checkpoint / "native", staging)
                self.check_native(staging)
                staging.replace(self.native)
        self.check_native(self.native)

    def import_metadata(self):
        self.require_game()
        identity = self.metadata_identity()
        self.call("import-ue-metadata", *self.target_args(), *self.metadata_args("--metadata"))
        write_json(self.work / "imported-metadata.json", identity)

    def resume_game(self):
        document, _ = self.load_checkpoint()
        metadata = read_json(self.work / "imported-metadata.json")
        require(metadata == self.metadata_identity(),
                "Import the validated Metadata Bundle before resuming Game")
        complete = self.work / "resume-complete.json"
        complete.unlink(missing_ok=True)
        started = time.monotonic()
        graph = self.graph("resume")
        remaining, reused, replayed = restore_objects(graph, self.checkpoint, document["actions"])
        prelink, link = graph.partition(lambda _, action: "link" if
                                        action["Type"] in LINK_TYPES else None)
        require(link and any(graph.actions[key]["Type"] == "Link" for key in link.keys() & remaining),
                "Resumed Game has no pending native link action")
        graph.execute(remaining & prelink, self.work / "resumed-compile.json", self.execute)
        graph.check_outputs(remaining & prelink)
        verify_pch_objects(graph, document["actions"])
        compile_time = time.monotonic() - started
        started = time.monotonic()
        graph.execute(remaining & link.keys(), self.work / "resumed-link.json", self.execute)
        graph.check_outputs(remaining & link.keys())
        write_json(complete, {
            "graph_sha256": file_sha(self.work / "resume-graph.json"),
            "checkpoint_sha256": file_sha(self.checkpoint / "manifest.json"),
            "metadata_input_sha256": metadata["input_sha256"], "reused_actions": reused,
            "reexecuted_actions": replayed, "reexecuted_portable_actions": 0, "pending_actions": 0,
            "metadata_dependent_seconds": compile_time, "link_seconds": time.monotonic() - started,
        })

    def verify_game(self):
        document, _ = self.load_checkpoint()
        complete = read_json(self.work / "resume-complete.json")
        graph_path = self.work / "resume-graph.json"
        require(complete.pop("graph_sha256") == file_sha(graph_path)
                and complete.pop("checkpoint_sha256") == file_sha(self.checkpoint / "manifest.json")
                and complete["metadata_input_sha256"] == file_sha(self.metadata / "manifest.json"),
                "Resumed Game inputs have changed")
        outputs = self.read_graph(graph_path).document["umf"]
        contexts, receipt = (Path(outputs[key]).resolve(strict=True) for key in ("contexts", "targetReceipt"))
        require(contexts.is_relative_to(self.project.parent) and receipt.is_relative_to(self.project.parent),
                "Game receipts must belong to the local project")
        target = read_json(receipt)
        args = self.target_args()
        require(all(target[key] == args[args.index(option) + 1] for key, option in (
                    ("TargetName", "--target"), ("Platform", "--platform"), ("Architecture", "--architecture"),
                    ("Configuration", "--configuration"))) and target["TargetType"] == "Game",
                "UBT receipt belongs to a different target")
        self.call("finalize-ue-target", "--contexts", contexts, "--ue-target-receipt", receipt)
        link = (contexts.parent / "host-link.json").resolve(strict=True)
        launch = target["Launch"]
        require(launch.startswith("$(ProjectDir)/"), "Game launch binary is outside the project")
        binary = logical_path(self.project.parent, launch.removeprefix("$(ProjectDir)/"))
        self.call("inspect-binary", "--file", binary, "--platform", self.ubt_platform,
                  "--architecture", ARCHITECTURES[self.arch], "--kind", "Executable")
        write_json(self.evidence / "game.json", {
            **document["identity"], **complete, "link_type": "Monolithic", "link_receipt_valid": True,
            "link_receipt": link.relative_to(self.source).as_posix(), "binary": binary.relative_to(self.source).as_posix(),
            "mbt": self.mbt.relative_to(self.work).as_posix(),
        })


PHASES = {
    "build-native-tools": Build.build_native_tools,
    "prepare-libclang": Build.prepare_libclang,
    "prepare-editor-bootstrap": Build.prepare_editor_bootstrap,
    "export-metadata": Build.export_metadata,
    "validate-metadata": Build.record_metadata,
    "build-editor": Build.build_editor,
    "prepare-game": Build.prepare_game,
    "save-checkpoint": Build.save_checkpoint,
    "validate-checkpoint": Build.record_checkpoint,
    "restore-checkpoint": Build.restore_checkpoint,
    "import-metadata": Build.import_metadata,
    "resume-game": Build.resume_game,
    "verify-game": Build.verify_game,
}


def test_native_development(native):
    native = native.resolve(strict=True)
    run([sys.executable, ROOT / "Scripts/test_metadata_bundle.py", native / ("ModBuildTool" + SUFFIX)], cwd=ROOT)
    run([sys.executable, ROOT / "Scripts/test_game_checkpoint.py"], cwd=ROOT)
    run([sys.executable, ROOT / "Scripts/test_sdk_plan.py", native / ("ModSDKTool" + SUFFIX),
         native / ("ModBuildTool" + SUFFIX)], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description="Run a Development CI build phase")
    commands = parser.add_subparsers(dest="phase", required=True)
    for phase in PHASES:
        commands.add_parser(phase)
    tests = commands.add_parser("test-native-development")
    tests.add_argument("native", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.phase == "test-native-development":
            test_native_development(arguments.native)
        else:
            PHASES[arguments.phase](Build())
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"{arguments.phase}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
