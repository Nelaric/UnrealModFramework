# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for Development CI's evidence and run boundaries."""

import json
import os
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

from check_automation_report import DEVELOPMENT_TESTS, check_development_report

sys.path.insert(0, str(Path(__file__).parent / "CI"))
import artifact_store as storage
import build_target as target
import development as ci


class AuthorizationTests(unittest.TestCase):
    def test_same_repository_pull_request_enters_engine_validation(self):
        payload = {
            "repository": {"full_name": ci.REPOSITORY, "id": 42},
            "pull_request": {"base": {"ref": "main"}, "head": {"repo": {"id": 42}}},
        }
        with patch.dict(os.environ, GITHUB_EVENT_NAME="pull_request"):
            self.assertTrue(ci.engine_run(payload))
            payload["pull_request"]["head"]["repo"]["id"] = 43
            self.assertFalse(ci.engine_run(payload))
            payload["pull_request"]["head"]["repo"] = None
            self.assertFalse(ci.engine_run(payload))


class ArtifactTests(unittest.TestCase):
    def test_artifact_can_come_from_a_successful_earlier_attempt_only_in_the_same_run(self):
        commit = "a" * 40
        name = f"metadata-linux-x64-editor-{commit}-1"
        digest = "sha256:" + "b" * 64
        manifest = {
            "artifactType": storage.ARTIFACT_TYPE + "metadata.v1",
            "annotations": {storage.CREATED: datetime.now(timezone.utc).isoformat()},
            "layers": [{"size": 5, "annotations": {storage.TITLE: filename}}
                       for filename in ("envelope.json", "payload")],
        }
        with patch.dict(os.environ, GITHUB_RUN_ID="42", GITHUB_RUN_ATTEMPT="2", GITHUB_REPOSITORY_ID="12",
                        CI_TRANSFER_PACKAGE="ghcr.io/nelaric/build", UMF_TRANSFER_USER="test", UMF_TRANSFER_TOKEN="test"):
            store = storage.ArtifactStore()
            with (
                patch.object(storage, "api", return_value={"visibility": "private"}),
                patch.object(store, "oras", side_effect=[json.dumps({"digest": digest}), json.dumps(manifest)]) as oras,
            ):
                self.assertEqual(store.inspect("metadata", "linux-x64-editor", commit, name, digest), (10, 1))
                self.assertEqual(oras.call_args_list[0].args[-1], f"ghcr.io/nelaric/build:umf-12-42-{name}")
                with self.assertRaisesRegex(ValueError, "producing attempt"):
                    store.inspect("metadata", "linux-x64-editor", commit, name[:-1] + "3", digest)
            with (
                patch.object(storage, "api", return_value={"visibility": "private"}),
                patch.object(store, "oras", return_value=json.dumps({"digest": "sha256:" + "c" * 64})),
            ):
                with self.assertRaisesRegex(ValueError, "tag does not match"):
                    store.inspect("metadata", "linux-x64-editor", commit, name, digest)
            manifest["annotations"][storage.CREATED] = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
            with self.assertRaisesRegex(ValueError, "reuse window expired"):
                storage.validate_manifest(manifest, "metadata", check_age=True)

    def test_public_storage_is_rejected_before_upload(self):
        commit = "a" * 40
        name = f"checkpoint-linux-x64-game-{commit}-1"
        with patch.dict(os.environ, GITHUB_RUN_ID="42", GITHUB_RUN_ATTEMPT="1", GITHUB_REPOSITORY_ID="12",
                        CI_TRANSFER_PACKAGE="ghcr.io/nelaric/build", UMF_TRANSFER_USER="test", UMF_TRANSFER_TOKEN="test"):
            store = storage.ArtifactStore()
            with (
                patch.object(storage, "api", return_value={"visibility": "public"}),
                patch.object(store, "oras") as oras,
                self.assertRaisesRegex(ValueError, "storage must be private"),
            ):
                store.push(Path.cwd(), "checkpoint", "linux-x64-game", commit, name)
            oras.assert_not_called()

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

    def test_checkpoint_requires_exact_engine_manifest_without_an_engine_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            payload = directory / "payload"
            payload.write_bytes(b"project objects")
            identity = {"engine_manifest_sha256": "a" * 64}
            target.write_json(directory / "envelope.json", {
                "identity": identity, "attempt": 1, "evidence": {},
                "files": {"payload": {"sha256": ci.sha256(payload), "bytes": payload.stat().st_size}},
            })
            target.verify_payload(directory, "checkpoint", identity, 1)
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                target.verify_payload(directory, "checkpoint", {"engine_manifest_sha256": "b" * 64}, 1)
            (directory / "sdk.tar.gz").touch()
            with self.assertRaisesRegex(ValueError, "unexpected entries"):
                target.verify_payload(directory, "checkpoint", identity, 1)


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
        candidate = {"engine": True, "commit": "a" * 40}
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
            patch.dict(os.environ, CI_IDENTITY=json.dumps(candidate), ACTIONS_READ_TOKEN=""),
            patch.object(ci, "job_durations", return_value=durations),
            patch.object(storage, "ArtifactStore") as artifact_store,
            patch.object(ci, "validate_live_pr"),
            patch.object(ci, "summary") as report,
            patch.object(ci, "output") as outcome,
        ):
            artifact_store.return_value.inspect.return_value = (10, 1)
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
