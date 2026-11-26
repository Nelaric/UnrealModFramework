# Copyright (c) 2026 Nelaric Contributors
import json
from pathlib import Path
import tempfile
import unittest

from windows_editor_receipt import windows_editor_artifacts


class WindowsEditorReceiptTests(unittest.TestCase):
    def test_resolves_the_requested_cpu_when_both_editors_are_installed(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory)
            root = engine / "Binaries/Win64"
            root.mkdir(parents=True)
            for cpu, suffix in (("x64", ""), ("arm64", "arm64")):
                prefix = "$(EngineDir)/Binaries/Win64/"
                exe = "UnrealEditor" + suffix + "-Cmd.exe"
                registry = "UnrealEditor" + suffix + ".modules"
                (root / exe).touch()
                (root / registry).touch()
                name = "UnrealEditor.target" if cpu == "x64" else "UnrealEditor-Win64-Developmentarm64.target"
                (root / name).write_text(json.dumps({
                    "TargetName": "UnrealEditor", "Platform": "Win64", "Configuration": "Development",
                    "Architecture": cpu, "LaunchCmd": prefix + exe,
                    "BuildProducts": [{"Path": prefix + exe}, {"Path": prefix + registry},
                                      {"Path": "$(EngineDir)/Plugins/Test/Binaries/Win64/" + registry}]
                }), encoding="utf-8")
            self.assertEqual(windows_editor_artifacts(engine, "arm64"),
                             (root / "UnrealEditorarm64-Cmd.exe", root / "UnrealEditorarm64.modules"))
            self.assertEqual(windows_editor_artifacts(engine, "x64"),
                             (root / "UnrealEditor-Cmd.exe", root / "UnrealEditor.modules"))
            (root / "UnrealEditorarm64-Cmd.exe").unlink()
            with self.assertRaises(FileNotFoundError):
                windows_editor_artifacts(engine, "arm64")
