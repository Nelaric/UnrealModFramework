# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for the CircleCI PR build handoff."""

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import fork_pr_build_bot as bot


_SHA = "a" * 40
_RUNS = "https://github.com/Nelaric/UnrealModFramework/actions/runs"
_CURRENT = f"{_RUNS}/100/attempts/2"


def status(owner: str | None) -> dict:
    return {"statuses": [{"context": "ci/fork-pr-linux-build", "target_url": owner}] if owner else []}


class ForkPrBuildBotTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.output = temporary / "outputs"
        self.summary = temporary / "summary"
        self.enterContext(patch.dict(os.environ, {
            "GITHUB_RUN_ID": "100",
            "GITHUB_RUN_ATTEMPT": "2",
            "GITHUB_TOKEN": "test-token",
            "GITHUB_OUTPUT": str(self.output),
            "GITHUB_STEP_SUMMARY": str(self.summary),
            "PR_NUMBER": "7",
            "TARGET_JOB": "build_game_linux",
        }))

    def test_start_emits_handoff_after_claim_becomes_visible(self) -> None:
        reads = iter((None, None, f"{_RUNS}/100/attempts/1"))

        def request(_url, *, token=None, payload=None):
            if payload is not None:
                return {}
            return status(next(reads, _CURRENT))

        with (
            patch.object(bot, "get_pull_request", return_value={"head": {"sha": _SHA}}),
            patch.object(bot, "verify_pull_request"),
            patch.object(bot, "request_json", side_effect=request),
            patch.object(bot, "trigger_circleci") as trigger,
            patch.object(bot.time, "sleep"),
        ):
            bot.start_main()

        outputs = dict(line.split("=", 1) for line in self.output.read_text(encoding="utf-8").splitlines())
        self.assertEqual(outputs["sha"], _SHA)
        self.assertTrue(outputs["nonce"])
        trigger.assert_called_once_with(7, _SHA, outputs["nonce"])

    def test_newer_run_or_attempt_stops_before_writing(self) -> None:
        for owner in (f"{_RUNS}/101/attempts/1", f"{_RUNS}/100/attempts/3"):
            with self.subTest(owner=owner):
                with patch.object(bot, "request_json", return_value=status(owner)) as request:
                    with self.assertRaises(bot.SupersededRun):
                        bot.post_status(_SHA, "success", "Build passed")
                self.assertEqual(request.call_count, 1)

    def test_unconfirmed_claim_fails_without_overwriting_statuses(self) -> None:
        with (
            patch.object(bot, "request_json", return_value=status(None)) as request,
            patch.object(bot.time, "sleep"),
        ):
            with self.assertRaisesRegex(RuntimeError, "did not confirm"):
                bot.post_status(_SHA, "success", "Build passed")
        self.assertGreater(request.call_count, 1)
        self.assertTrue(all(call.kwargs.get("payload") is None for call in request.call_args_list))

    def test_watch_rejects_missing_inputs_without_contacting_circleci(self) -> None:
        for sha, nonce in (("", "identifier"), (_SHA, "")):
            with self.subTest(sha=sha, nonce=nonce):
                with (
                    patch.dict(os.environ, {"PR_SHA": sha, "BUILD_NONCE": nonce}),
                    patch.object(bot, "watch_target", return_value=("success", "success", None)) as watch,
                    patch.object(bot, "post_status") as post,
                ):
                    with self.assertRaisesRegex(RuntimeError, "Missing CircleCI build inputs"):
                        bot.watch_main()
                watch.assert_not_called()
                post.assert_not_called()

    def test_summary_fails_when_the_start_job_provides_no_handoff(self) -> None:
        values = {"PR_SHA": "", "BUILD_NONCE": "", "START_RESULT": "success", "BUILD_RESULT": "success"}
        with patch.dict(os.environ, values):
            with self.assertRaisesRegex(RuntimeError, "One or more Linux builds did not succeed"):
                bot.finish_main()
        self.assertIn("Missing PR SHA or CircleCI build identifier", self.summary.read_text(encoding="utf-8"))
