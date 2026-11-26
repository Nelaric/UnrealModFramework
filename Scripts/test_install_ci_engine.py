# Copyright (c) 2026 Nelaric Contributors
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import zstandard

import install_ci_engine as installer


def sha(data):
    return hashlib.sha256(data).hexdigest()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dest = Path(self.temp.name) / "installed"
        self.revision = "a" * 40
        self.name = "Engine/Build/Build.version"
        self.content = b"official version"
        tar = io.BytesIO()
        with tarfile.open(fileobj=tar, mode="w") as a:
            header = tarfile.TarInfo(self.name)
            header.size = len(self.content)
            a.addfile(header, io.BytesIO(self.content))
        self.archive = zstandard.compress(tar.getvalue())
        row = {"path": self.name, "kind": "file", "bytes": len(self.content), "reason": "engine version",
               "sha256": sha(self.content)}
        self.index = zstandard.compress((json.dumps(row) + "\n").encode())
        self.parts = [self.archive[:12], self.archive[12:]]
        self.assets = {"files.jsonl.zst": self.index}
        self.manifest = {"format": "Nelaric.UEPackage.v1", "version": "5.6.1", "system": "windows",
                         "architecture": "x64", "target": "Editor", "configuration": "Development",
                         "revision": self.revision, "inventorySha256": sha(self.index), "archiveSha256": sha(self.archive),
                         "files": 1, "bytes": len(self.content), "parts": []}
        for i, part in enumerate(self.parts):
            name = f"engine.tar.zst.{i:03d}"
            self.assets[name] = part
            self.manifest["parts"].append({"name": name, "bytes": len(part), "sha256": sha(part)})

    def install(self, immutable=True):
        self.assets["manifest.json"] = json.dumps(self.manifest).encode()
        self.assets["verification.json"] = json.dumps({"result": "passed",
                                                      "manifestSha256": sha(self.assets["manifest.json"])}).encode()
        data = list(self.assets.values())
        release = {"tag_name": installer.package_tag("windows", "x64", "Editor", self.revision), "draft": False,
                   "immutable": immutable, "assets": [{"name": n, "id": i, "size": len(self.assets[n])}
                                                       for i, n in enumerate(self.assets)]}
        with patch.object(installer, "api", return_value=release), patch.object(
                installer, "fetch_asset", side_effect=lambda asset, path: path.write_bytes(data[asset])):
            installer.install(self.dest, "windows", "x64", "Editor", self.revision)

    def test_verified_split_install(self):
        self.install()
        self.assertEqual((self.dest / self.name).read_bytes(), self.content)

    def test_altered_part_publishes_no_directory(self):
        self.assets["engine.tar.zst.000"] = b"x" * len(self.parts[0])
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.install()
        self.assertFalse(self.dest.exists())

    def test_rejects_wrong_target(self):
        self.manifest["target"] = "Game"
        with self.assertRaisesRegex(ValueError, "identity"):
            self.install()

    def test_unlisted_and_missing_members(self):
        with self.assertRaisesRegex(ValueError, "Unlisted"):
            installer.unpack(io.BytesIO(self.archive), self.dest, {})

    def test_rejects_unverified_mutable_release(self):
        with self.assertRaisesRegex(ValueError, "immutable"):
            self.install(immutable=False)

    def test_drafts_require_the_private_workflow(self):
        with patch.dict(os.environ, {"GITHUB_REPOSITORY": "Nelaric/UnrealModFramework"}):
            with self.assertRaisesRegex(ValueError, "private producer"):
                installer.install(self.dest, "windows", "x64", "Editor", self.revision, 1)
