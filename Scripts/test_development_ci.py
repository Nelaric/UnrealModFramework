# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for Development CI's evidence and run boundaries."""

import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from check_automation_report import DEVELOPMENT_TESTS, check_development_report

sys.path.insert(0, str(Path(__file__).parent / "CI"))
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
    def test_checkpoint_requires_the_locked_engine_and_a_fresh_build_record(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            identity = {"source_commit": "a" * 40, "architecture": "arm64", "engine_manifest_sha256": "b" * 64}
            contents = {
                "sealed_at": time.time(), "identity": identity, "evidence": {"completed_actions": 8},
            }
            target.write_json(directory / "envelope.json", contents)
            self.assertEqual(target.handoff_evidence(directory, "checkpoint", identity), contents["evidence"])
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                target.handoff_evidence(directory, "checkpoint", {**identity, "engine_manifest_sha256": "c" * 64})
            with patch.object(target.time, "time", return_value=contents["sealed_at"] + 3600):
                with self.assertRaisesRegex(ValueError, "one-hour consumption window expired"):
                    target.handoff_evidence(directory, "checkpoint", identity)


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
                values = {"metrics": json.dumps(metrics), "artifact-id": "12"}
            needs[job] = {"result": "success", "outputs": values}
        durations = dict.fromkeys(ci.JOBS.values(), 1.5)
        with (
            patch.dict(os.environ, CI_IDENTITY=json.dumps(candidate), ACTIONS_READ_TOKEN=""),
            patch.object(ci, "job_durations", return_value=durations),
            patch.object(ci, "artifact_sizes", return_value={"12": 10}) as sizes,
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
            needs["editor-linux-x64"]["outputs"]["metrics"] = needs["editor-macos-x64"]["outputs"]["metrics"]
            sizes.return_value = {}
            with patch.dict(os.environ, NEEDS_JSON=json.dumps(needs)):
                ci.aggregate()
            outcome.assert_called_with("passed", "false")
            self.assertIn("Missing or expired Metadata artifact", "\n".join(report.call_args.args[0]))


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
