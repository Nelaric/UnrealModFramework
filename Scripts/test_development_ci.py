# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for Development CI's evidence and run boundaries."""

import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

from check_automation_report import DEVELOPMENT_TESTS, check_development_report

sys.path.insert(0, str(Path(__file__).parent / "CI"))
import build_target as target
import development as ci


class ArtifactTests(unittest.TestCase):
    def test_artifact_can_come_from_a_successful_earlier_attempt_only_in_the_same_run(self):
        commit = "a" * 40
        name = f"metadata-linux-x64-editor-{commit}-1"
        info = {
            "name": name, "workflow_run": {"id": 42}, "size_in_bytes": 10, "expired": False,
            "created_at": datetime.now(timezone.utc).isoformat(),
        }
        with patch.dict(os.environ, GITHUB_RUN_ID="42", GITHUB_RUN_ATTEMPT="2", GITHUB_REPOSITORY="Nelaric/build"):
            with patch.object(ci, "api", return_value=info):
                self.assertEqual(ci.artifact_info("metadata", "linux-x64-editor", commit, "7", name, "")[1], 1)
                info["workflow_run"]["id"] = 41
                with self.assertRaisesRegex(ValueError, "outside this run"):
                    ci.artifact_info("metadata", "linux-x64-editor", commit, "7", name, "")
                info["workflow_run"]["id"] = 42
                info["created_at"] = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
                with self.assertRaisesRegex(ValueError, "consumption window expired"):
                    ci.artifact_info("metadata", "linux-x64-editor", commit, "7", name, "")

    def test_corrupt_payload_or_wrong_identity_cannot_be_restored(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            payload = directory / "payload"
            payload.write_bytes(b"original")
            expected = {"source_commit": "a" * 40, "architecture": "arm64", "run_id": "12"}
            contents = {
                "attempt": 2, "identity": expected, "evidence": {},
                "files": {"payload": {"bytes": payload.stat().st_size, "sha256": ci.sha256(payload)}},
            }
            target.write_json(directory / "envelope.json", contents)
            target.verify_payload(directory, "metadata", expected, 2)
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                target.verify_payload(directory, "metadata", {**expected, "architecture": "x64"}, 2)
            payload.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "corrupt"):
                target.verify_payload(directory, "metadata", expected, 2)

    def test_sdk_restore_preserves_contents_and_blocks_out_of_tree_members(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "sdk.tar.gz"
            with tarfile.open(archive, "w:gz") as stream:
                member = tarfile.TarInfo("./Engine/tool")
                member.mode = 0o755
                member.size = 3
                stream.addfile(member, io.BytesIO(b"SDK"))
            sdk = root / "sdk"
            target.restore_sdk(archive, sdk)
            self.assertEqual((sdk / "Engine/tool").read_bytes(), b"SDK")
            if os.name != "nt":
                self.assertTrue((sdk / "Engine/tool").stat().st_mode & 0o100)
            with tarfile.open(archive, "w:gz") as stream:
                stream.addfile(tarfile.TarInfo("../outside"))
            with self.assertRaises(tarfile.OutsideDestinationError):
                target.restore_sdk(archive, root / "invalid")
            self.assertFalse((root / "outside").exists())


class ResultTests(unittest.TestCase):
    def test_resume_does_not_reexecute_portable_actions(self):
        checkpoint = {"completed_actions": 8, "nonportable_actions": {"absolute compiler path": 1}}
        game = {"reused_actions": 7, "reexecuted_actions": 1, "reexecuted_portable_actions": 0, "pending_actions": 0}
        target.verify_reuse(checkpoint, game)
        for key, value in {"reused_actions": 6, "reexecuted_actions": 2,
                           "reexecuted_portable_actions": 1, "pending_actions": 1}.items():
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "resume must reuse"):
                target.verify_reuse(checkpoint, {**game, key: value})

    def test_aggregate_requires_every_job_and_all_editor_proof(self):
        candidate = {"private": True, "commit": "a" * 40}
        needs = {}
        for job in ci.JOBS:
            values = {}
            if job not in ("code-quality", "native-tools"):
                metrics = {
                    "timings": {phase: 1.5 for phase in ci.required_timings(job)},
                    "automation_tests": 3, "metadata": "valid", "checkpoint": "valid",
                    "nonportable_actions": {}, "reused_actions": 5, "reexecuted_actions": 0,
                }
                values = {"metrics": json.dumps(metrics), "artifact-id": "12", "artifact-name": "from-producer"}
            needs[job] = {"result": "success", "outputs": values}
        durations = dict.fromkeys(ci.JOBS.values(), 1.5)
        with (
            patch.dict(os.environ, CI_IDENTITY=json.dumps(candidate), ARTIFACT_TOKEN=""),
            patch.object(ci, "job_durations", return_value=durations),
            patch.object(ci, "artifact_info", return_value=({"size_in_bytes": 10}, 1)),
            patch.object(ci, "validate_live_pr"),
            patch.object(ci, "summary") as report,
            patch.object(ci, "output") as outcome,
        ):
            for state in ("success", "skipped", "failure", "cancelled"):
                needs["game-linux-arm64"]["result"] = state
                with patch.dict(os.environ, NEEDS_JSON=json.dumps(needs)):
                    ci.aggregate()
                outcome.assert_called_with("passed", "true" if state == "success" else "false")
            needs["game-linux-arm64"]["result"] = "success"
            needs["editor-linux-x64"]["outputs"]["metrics"] = "{}"
            with patch.dict(os.environ, NEEDS_JSON=json.dumps(needs)):
                ci.aggregate()
            outcome.assert_called_with("passed", "false")
            self.assertIn("Missing or Invalid", "\n".join(report.call_args.args[0]))


class EditorReportTests(unittest.TestCase):
    def test_reports_require_real_success_counts_and_critical_coverage(self):
        tests = [{"fullTestPath": name, "state": "Success", "errors": 0, "warnings": 0} for name in DEVELOPMENT_TESTS]
        report = {"succeeded": len(tests), "succeededWithWarnings": 0, "failed": 0, "notRun": 0, "tests": tests}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "index.json"
            with self.assertRaisesRegex(ValueError, "missing"):
                check_development_report(path)
            target.write_json(path, report)
            self.assertEqual(check_development_report(path), len(tests))
            for invalid in (
                {**report, "tests": [], "succeeded": 0},
                {**report, "tests": tests[1:], "succeeded": len(tests) - 1},
                {**report, "tests": [*tests[1:], {**tests[0], "state": "Skipped"}]},
                {**report, "failed": 1},
            ):
                with self.subTest(report=invalid), self.assertRaises(ValueError):
                    target.write_json(path, invalid)
                    check_development_report(path)


if __name__ == "__main__":
    unittest.main()
