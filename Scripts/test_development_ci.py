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


def pull_request_event(*, number=129, head_repository=ci.REPOSITORY, author="contributor", head="", base=""):
    return {
        "number": number,
        "repository": {"full_name": ci.REPOSITORY},
        "pull_request": {
            "base": {"ref": "main", "sha": base},
            "head": {"repo": {"full_name": head_repository} if head_repository else None, "sha": head},
            "user": {"login": author},
        },
    }


class AuthorizationTests(unittest.TestCase):
    def test_same_repository_prs_start_engine_jobs_automatically(self):
        payload = pull_request_event()
        with patch.dict(os.environ, GITHUB_EVENT_NAME="pull_request", GITHUB_REF="refs/pull/129/merge",
                        GITHUB_ACTOR="contributor"):
            self.assertTrue(ci.engine_run(payload))
            self.assertEqual(ci.event_pr_number(payload), 129)
            for repository in ("Fork/UnrealModFramework", None):
                with self.subTest(repository=repository):
                    self.assertFalse(ci.engine_run(pull_request_event(head_repository=repository)))
            self.assertFalse(ci.engine_run(pull_request_event(author="dependabot[bot]")))
            with patch.dict(os.environ, GITHUB_ACTOR="dependabot[bot]"):
                self.assertFalse(ci.engine_run(payload))
        for event_name in ("workflow_dispatch", "push", "pull_request_target"):
            with patch.dict(os.environ, GITHUB_EVENT_NAME=event_name):
                self.assertFalse(ci.engine_run(payload))
        with patch.dict(os.environ, GITHUB_EVENT_NAME="pull_request", GITHUB_REF="refs/pull/130/merge"):
            with self.assertRaisesRegex(ValueError, "Expected a PR test merge ref"):
                ci.engine_run(payload)

    def test_fork_approval_requires_an_exact_sha_and_a_maintainer(self):
        commit, head, base = "a" * 40, "b" * 40, "c" * 40
        creator = {"type": "User", "id": 42, "login": "maintainer"}
        approval = {
            "repository": {"full_name": ci.REPOSITORY},
            "deployment": {
                "ref": commit, "sha": commit, "environment": ci.APPROVAL_ENVIRONMENT,
                "task": ci.APPROVAL_TASK, "creator": creator,
                "payload": {"pull_request": 129, "base": base, "head": head},
            },
            "sender": creator,
        }
        pr = {
            "state": "open", "merge_commit_sha": commit, "head": {"sha": head},
            "base": {"ref": "main", "sha": base, "repo": {"full_name": ci.REPOSITORY}},
        }
        main = {"object": {"sha": base}}
        with (
            patch.dict(os.environ, GITHUB_REPOSITORY=ci.REPOSITORY, GITHUB_EVENT_NAME="deployment",
                       GITHUB_SHA=commit, PUBLIC_READ_TOKEN="read"),
            patch.object(ci, "event", return_value=approval),
            patch.object(ci, "output") as outputs,
        ):
            with patch.object(ci, "api", side_effect=[{"role_name": "maintain"}, pr, main]) as api:
                ci.resolve_candidate()
                self.assertEqual([call.args[0] for call in api.call_args_list], [
                    f"/repos/{ci.REPOSITORY}/collaborators/maintainer/permission",
                    f"/repos/{ci.REPOSITORY}/pulls/129",
                    f"/repos/{ci.REPOSITORY}/git/ref/heads/main",
                ])
                self.assertEqual(dict(call.args for call in outputs.call_args_list)["engine"], "true")
            outputs.reset_mock()
            with patch.object(ci, "api", return_value={"role_name": "write"}):
                with self.assertRaisesRegex(ValueError, "requires repository maintain access"):
                    ci.resolve_candidate()
            with patch.object(ci, "api", side_effect=[{"role_name": "admin"}, {**pr, "merge_commit_sha": "d" * 40}]):
                with self.assertRaisesRegex(ValueError, "test merge commit has changed"):
                    ci.resolve_candidate()
            approval["deployment"]["ref"] = "main"
            with self.assertRaisesRegex(ValueError, "exact reviewed test merge SHA"):
                ci.resolve_candidate()
            outputs.assert_not_called()

    def test_candidate_resolver_pins_the_live_test_merge(self):
        commit, head, base = "a" * 40, "b" * 40, "c" * 40
        pr = {
            "state": "open", "merge_commit_sha": commit, "head": {"sha": head},
            "base": {"ref": "main", "sha": base, "repo": {"full_name": ci.REPOSITORY}},
        }
        main = {"object": {"sha": base}}
        with (
            patch.dict(os.environ, GITHUB_REPOSITORY=ci.REPOSITORY, GITHUB_EVENT_NAME="pull_request",
                       GITHUB_REF="refs/pull/129/merge", GITHUB_ACTOR="contributor",
                       GITHUB_SHA=commit, PUBLIC_READ_TOKEN="read"),
            patch.object(ci, "event", return_value=pull_request_event(head=head, base=base)) as event,
            patch.object(ci, "output") as outputs,
        ):
            for fork in (False, True):
                event.return_value = pull_request_event(
                    head=head, base=base, head_repository="Fork/UnrealModFramework" if fork else ci.REPOSITORY)
                with patch.object(ci, "api", side_effect=[pr, main]) as api:
                    ci.resolve_candidate()
                    self.assertEqual(
                        [call.args[0] for call in api.call_args_list],
                        [f"/repos/{ci.REPOSITORY}/pulls/129", f"/repos/{ci.REPOSITORY}/git/ref/heads/main"],
                    )
                values = dict(call.args for call in outputs.call_args_list)
                self.assertEqual(values["commit"], commit)
                self.assertEqual(values["engine"], "false" if fork else "true")
                outputs.reset_mock()
            with patch.object(ci, "api", return_value={**pr, "merge_commit_sha": "d" * 40}):
                with self.assertRaisesRegex(ValueError, "test merge commit has changed"):
                    ci.resolve_candidate()
            outputs.assert_not_called()


class ArtifactTests(unittest.TestCase):
    def test_checkpoint_requires_the_same_sdk_release_and_a_fresh_build_record(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            identity = {"source_commit": "a" * 40, "architecture": "arm64", "engine_release": "v1.0.1"}
            contents = {
                "sealed_at": time.time(), "identity": identity, "evidence": {"completed_actions": 8},
            }
            target.write_json(directory / "envelope.json", contents)
            self.assertEqual(target.handoff_evidence(directory, "checkpoint", identity), contents["evidence"])
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                target.handoff_evidence(directory, "checkpoint", {**identity, "engine_release": "v1.0.2"})
            with patch.object(target.time, "time", return_value=contents["sealed_at"] + 3600):
                with self.assertRaisesRegex(ValueError, "one-hour consumption window expired"):
                    target.handoff_evidence(directory, "checkpoint", identity)


class SdkReleaseTests(unittest.TestCase):
    def test_package_selection_is_specific_to_target_and_release(self):
        arm64 = {"target": "windows-arm64-game", "archive": "UE-5.6.1-Windows-arm64-Game.tar.zst"}
        index = {
            "schema_version": 1, "release": "v1.0.1", "ue_version": "5.6.1",
            "packages": [{"target": "source"}, {"target": "windows-x64-editor"}, arm64],
        }
        self.assertEqual(target.sdk_package(index, "v1.0.1", "windows-arm64-game"), arm64)
        with self.assertRaisesRegex(ValueError, "incompatible release index"):
            target.sdk_package(index, "v1.0.2", "windows-arm64-game")
        with self.assertRaisesRegex(ValueError, "exactly one SDK"):
            target.sdk_package(index, "v1.0.1", "linux-x64-game")

    def test_sdk_release_requires_immutable_published_assets(self):
        release = {
            "id": 12, "tag_name": "v1.0.1", "immutable": True, "published_at": "2026-11-28T00:00:00Z",
            "draft": False, "prerelease": False, "assets": [{"name": "v1.0.1-metadata.zip", "id": 7}],
        }
        with patch.object(target, "api", side_effect=[{"private": True}, release]):
            self.assertEqual(target.resolve_sdk_release("v1.0.1", "test"), {
                "id": 12, "assets": {"v1.0.1-metadata.zip": 7},
            })
        with patch.object(target, "api", side_effect=[{"private": True}, {**release, "immutable": False}]):
            with self.assertRaisesRegex(ValueError, "published, immutable"):
                target.resolve_sdk_release("v1.0.1", "test")


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
            patch.object(ci, "validate_live_pr") as check_candidate,
            patch.object(ci, "summary") as report,
            patch.object(ci, "output") as outcome,
        ):
            with patch.dict(os.environ, CI_IDENTITY=json.dumps({**candidate, "engine": False}),
                            NEEDS_JSON=json.dumps(needs)):
                ci.aggregate()
            outcome.assert_called_with("passed", "false")
            self.assertIn("Authorized engine validation is required", "\n".join(report.call_args.args[0]))
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
            check_candidate.assert_not_called()

    def test_result_checks_the_live_candidate_once_and_rejects_a_moved_base(self):
        candidate = {
            "repository": ci.REPOSITORY, "pr": 129, "engine": True,
            "commit": "a" * 40, "head": "b" * 40, "base": "c" * 40,
        }
        pr = {
            "state": "open", "merge_commit_sha": candidate["commit"], "head": {"sha": candidate["head"]},
            "base": {"ref": "main", "sha": candidate["base"], "repo": {"full_name": ci.REPOSITORY}},
        }
        prefix = f"/repos/{ci.REPOSITORY}"
        reads = [(f"{prefix}/pulls/129", "read"), (f"{prefix}/git/ref/heads/main", "read")]
        event = pull_request_event(head=candidate["head"], base=candidate["base"])
        with (
            patch.dict(os.environ, CI_IDENTITY=json.dumps(candidate), PUBLIC_READ_TOKEN="read",
                       GITHUB_EVENT_NAME="pull_request", GITHUB_REF="refs/pull/129/merge",
                       GITHUB_ACTOR="contributor", GITHUB_SHA=candidate["commit"]),
            patch.object(ci, "event", return_value=event),
            patch.object(ci, "summary") as report,
        ):
            base = {"object": {"sha": candidate["base"]}}
            with patch.object(ci, "api", side_effect=[pr, base]) as api:
                ci.verify_current_candidate()
                self.assertEqual([request.args for request in api.call_args_list], reads)
            with patch.object(ci, "api", side_effect=[pr, {"object": {"sha": "d" * 40}}]) as api:
                with self.assertRaisesRegex(ValueError, "Main has moved"):
                    ci.verify_current_candidate()
                self.assertEqual([request.args for request in api.call_args_list], reads)
                self.assertIn("Candidate is stale or unverifiable", report.call_args.args[0][0])
            with patch.dict(os.environ, GITHUB_SHA="e" * 40):
                with self.assertRaisesRegex(ValueError, "does not match the current PR candidate"):
                    ci.verify_current_candidate()


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
