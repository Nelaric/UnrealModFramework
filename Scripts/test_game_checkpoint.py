# Copyright (c) 2026 Nelaric Contributors

import tempfile
import unittest
from pathlib import Path

from game_checkpoint import (
    ActionGraph, file_sha, inventory, logical_path, read_json, restore_objects, save_actions, validate_actions,
    verify_inventory, verify_pch_objects, write_json,
)


class CheckpointTests(unittest.TestCase):
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
