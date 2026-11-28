# Copyright (c) 2026 Nelaric Contributors

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from game_checkpoint import (
    ActionGraph, file_sha, inventory, logical_path, project_inputs, read_json, restore_objects, save_actions, validate_actions,
    verify_inventory, verify_pch_objects, write_json,
)


class CheckpointTests(unittest.TestCase):
    def test_default_mac_resources_leave_project_inputs_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, engine = root / "Project", root / "Engine"
            relative = Path("Build/Mac/Resources")
            (project / relative).mkdir(parents=True)
            (engine / relative).mkdir(parents=True)
            (project / "Game.uproject").write_text("{}", encoding="utf-8")
            before_link = project_inputs(project, engine)

            for name in ("Info.Template.plist", "Sandbox.Server.entitlements", "Sandbox.NoNet.entitlements"):
                (engine / relative / name).write_text(name, encoding="utf-8")
                (project / relative / name).write_bytes((engine / relative / name).read_bytes())
            self.assertEqual(project_inputs(project, engine), before_link)

            custom = project / relative / "Info.Template.plist"
            custom.write_text("Project-specific info", encoding="utf-8")
            self.assertNotEqual(project_inputs(project, engine), before_link)
            after_customization = project_inputs(project, engine)
            (project / relative / "AppIcon.icns").write_bytes(b"Project icon")
            self.assertNotEqual(project_inputs(project, engine), after_customization)

    def graph(self, root, absolute=False):
        project = root / "Project"
        engine = root / "Engine"
        engine.mkdir(parents=True)
        compiler = engine / "mock-clang"
        compiler.write_text("compiler identity", encoding="utf-8")
        source = project / "Source/Runtime/Independent.cpp"
        source.parent.mkdir(parents=True)
        source.write_text('#include "Independent.h"\n', encoding="utf-8")
        source.with_suffix(".h").write_text("#pragma once\n", encoding="utf-8")
        export = project / "Source/ExportOwner"
        export.mkdir()
        intermediate = project / "Intermediate"
        intermediate.mkdir()
        response = intermediate / "Independent.rsp"
        response.write_text(f'-c "{source}"\n', encoding="utf-8")
        dependencies = intermediate / "Independent.dep.json"
        write_json(dependencies, {"Data": {"Source": str(source), "Includes": [str(source.with_suffix(".h"))]}})
        definitions = intermediate / "Definitions.o"
        exported = intermediate / "ExportOwner/Owner.o"
        binary = project / "Binaries/Game"
        tool = {"CommandPath": str(compiler), "WorkingDirectory": str(engine),
                "CommandVersion": "test", "StatusDescription": "test"}
        actions = [
            {**tool, "Id": 6, "Type": "Compile", "ArtifactMode": "Enabled" + (", AbsolutePath" if absolute else ""),
             "CommandArguments": f'@"{response}"', "DependencyListFile": str(dependencies),
             "ProducedItems": [str(definitions), str(dependencies)], "PrerequisiteActions": []},
            {**tool, "Id": 9, "Type": "Compile", "CommandArguments": "-c exports",
             "ProducedItems": [str(exported)], "PrerequisiteActions": []},
            {**tool, "Id": 20, "Type": "Link", "CommandArguments": "link",
             "ProducedItems": [str(binary)], "PrerequisiteItems": [str(definitions), str(exported)],
             "PrerequisiteActions": [6, 9]},
        ]
        return ActionGraph({"Actions": actions, "umf": {"engine": str(engine),
                           "modules": {"ExportOwner": str(export)}}}, project.resolve()), definitions

    def test_portable_object_and_pending_actions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            producer, output = self.graph(root / "producer")
            early, deferred = producer.without_metadata()
            self.assertEqual(early, {6})
            self.assertEqual(deferred, {9: "export-module", 20: "final-build"})
            output.write_bytes(b"native object")
            payload = root / "checkpoint"
            payload.mkdir()
            records = save_actions(producer, payload)
            counts = validate_actions(records, inventory(payload))
            self.assertEqual((counts["completed_actions"], counts["pending_actions"]), (1, 2))
            self.assertEqual(counts["metadata_independent_pending_actions"], 0)

            consumer, restored = self.graph(root / "resume")
            remaining, reused, replayed = restore_objects(consumer, payload, records)
            self.assertEqual((remaining, reused, replayed), ({9, 20}, 1, 0))
            self.assertEqual(restored.read_bytes(), b"native object")
            action_file = root / "resume.json"
            consumer.execute(remaining, action_file, lambda path: self.assertEqual(
                [action["PrerequisiteActions"] for action in read_json(path)["Actions"]], [[], [0]]))

            (consumer.project / "Source/Runtime/Independent.h").write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Compiler prerequisite changed"):
                restore_objects(consumer, payload, records)

            files = inventory(payload)
            write_json(payload / "manifest.json", {})
            self.assertEqual(verify_inventory(payload, files), files)
            (payload / next(iter(files))).write_bytes(b"different native object")
            with self.assertRaisesRegex(ValueError, "Checkpoint file inventory differs"):
                verify_inventory(payload, files)

    def test_clang_resource_and_binary_prerequisites_are_verified_after_relocation(self):
        def prepare(root):
            graph, compiled = self.graph(root)
            compiler = graph.engine / "clang"
            compiler.write_bytes(b"clang identity")
            action = graph.actions[6]
            action["CommandPath"] = str(compiler)
            resources = root / "system/llvm/lib/clang/18"
            header = resources / "include/arm_bf16.h"
            header.parent.mkdir(parents=True)
            header.write_bytes(b"clang builtin header")
            generated = graph.project / "Intermediate/Input.bin"
            generated.write_bytes(b"\x00CPCH\xc1")
            dependency_list = Path(action["DependencyListFile"])
            info = read_json(dependency_list)
            info["Data"]["Includes"] += [str(header), str(generated)]
            write_json(dependency_list, info)
            return graph, compiled, compiler, resources, header, generated

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            producer, built, producer_compiler, producer_resources, _, _ = prepare(root / "producer")
            consumer, restored, consumer_compiler, consumer_resources, header, generated = prepare(root / "consumer")
            resources = {str(producer_compiler.resolve()): producer_resources,
                         str(consumer_compiler.resolve()): consumer_resources}
            built.write_bytes(b"native object")
            payload = root / "checkpoint"
            payload.mkdir()

            def print_resource_dir(command, *, text):
                self.assertTrue(text)
                self.assertEqual(command[1:], ["-print-resource-dir"])
                return str(resources[command[0]]) + "\n"

            with patch("game_checkpoint.subprocess.check_output", side_effect=print_resource_dir):
                records = save_actions(producer, payload)
                inputs = records[producer.keys[6]]["inputs"]
                self.assertIn("toolchain-clang-resources/include/arm_bf16.h", inputs)
                self.assertIn(file_sha(generated), inputs.values())
                validate_actions(records, inventory(payload))
                self.assertEqual(restore_objects(consumer, payload, records), ({9, 20}, 1, 0))
                self.assertEqual(restored.read_bytes(), b"native object")
                header.write_text("changed compiler header", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Compiler prerequisite changed"):
                    restore_objects(consumer, payload, records)

    def test_generated_precompiled_header_is_verified_by_its_producer(self):
        def prepare(root):
            graph, compiled = self.graph(root)
            pch = graph.project / "Intermediate/Shared.h.gch"
            pch_deps = graph.project / "Intermediate/Shared.json"
            header = graph.project / "Source/Runtime/Independent.h"
            write_json(pch_deps, {"Data": {"Source": str(header), "Includes": []}})
            graph.document["Actions"].append({
                **graph.actions[6], "Id": 7, "CommandArguments": "-c Shared.h",
                "ProducedItems": [str(pch), str(pch_deps)], "DependencyListFile": str(pch_deps),
            })
            graph.actions[6]["PrerequisiteActions"] = [7]
            dependencies = Path(graph.actions[6]["DependencyListFile"])
            record = read_json(dependencies)
            record["Data"]["Includes"].append(str(pch))
            write_json(dependencies, record)
            return ActionGraph(graph.document, graph.project), compiled, pch, header

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            producer, built, generated, _ = prepare(root / "producer")
            consumer, restored, missing_pch, header = prepare(root / "consumer")
            generated.write_bytes(b"CPCH\x00\xc1")
            built.write_bytes(b"native object")
            payload = root / "checkpoint"
            payload.mkdir()
            records = save_actions(producer, payload)
            checkpoint = validate_actions(records, inventory(payload))
            self.assertEqual(checkpoint["nonportable_actions"], {"precompiled-header": 1})
            self.assertEqual(restore_objects(consumer, payload, records), ({7, 9, 20}, 1, 1))
            self.assertFalse(missing_pch.exists())
            self.assertEqual(restored.read_bytes(), b"native object")
            header.write_text("changed PCH input", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Compiler prerequisite changed"):
                restore_objects(consumer, payload, records)

    def test_post_build_virtual_marker_does_not_replace_real_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            graph, _ = self.graph(Path(temporary))
            metadata = graph.project / "Intermediate/Game.target"
            marker = graph.project / "Intermediate/PostBuild-1.sh.ran"
            graph.document["Actions"].append({
                "Id": 21, "Type": "PostBuildStep", "StatusDescription": "test post-build",
                "ProducedItems": [str(marker), str(metadata)],
                "PrerequisiteActions": [20],
            })
            graph = ActionGraph(graph.document, graph.project)
            write_json(metadata, {"TargetType": "Game"})
            binary = graph.project / "Binaries/Game"
            binary.parent.mkdir()
            binary.write_bytes(b"linked game")
            graph.check_outputs({20, 21})
            metadata.unlink()
            with self.assertRaisesRegex(ValueError, "Game.target"):
                graph.check_outputs({20, 21})

    def test_pch_debug_object_has_to_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            graph, output = self.graph(root)
            output.write_bytes(b"producer debug types")
            relative = output.relative_to(graph.project).as_posix()
            actions = {graph.keys[6]: {"pch_objects": {relative: file_sha(output)}}}
            verify_pch_objects(graph, actions)
            output.write_bytes(b"different debug types")
            with self.assertRaisesRegex(ValueError, "Regenerated MSVC PCH object differs"):
                verify_pch_objects(graph, actions)

    def test_absolute_path_sensitive_and_untrusted_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            producer, output = self.graph(root / "producer", absolute=True)
            output.write_bytes(b"native object")
            payload = root / "checkpoint"
            payload.mkdir()
            records = save_actions(producer, payload)
            consumer, restored = self.graph(root / "resume", absolute=True)
            with self.assertRaisesRegex(ValueError, "absolute compiler roots"):
                restore_objects(consumer, payload, records)
            self.assertFalse(restored.exists())
            for unsafe in ("../a", "/a", "c:/a", "a\\..\\b", "a/../b"):
                with self.subTest(unsafe=unsafe), self.assertRaises(ValueError):
                    logical_path(payload, unsafe)

            producer.document["Actions"][0]["DeleteItems"] = [str(producer.engine / "mock-clang")]
            with self.assertRaisesRegex(ValueError, "outside the project"):
                ActionGraph(producer.document, producer.project)

    def test_development_command_configuration(self):
        from development_commands import PHASES

        root = Path(__file__).resolve().parents[1]
        config = read_json(root / ".github/development-commands.json")
        self.assertEqual(config["schemaVersion"], 1)
        self.assertEqual(set(config["commands"]), {*PHASES, "test-native-development"})
        for phase, command in config["commands"].items():
            arguments = ["python", "Scripts/development_commands.py", phase]
            if phase == "test-native-development":
                arguments.append("{native}")
            self.assertEqual(command, arguments)


if __name__ == "__main__":
    unittest.main()
