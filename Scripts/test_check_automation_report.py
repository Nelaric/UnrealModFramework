# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for Unreal automation report validation."""

import json
import tempfile
import unittest
from pathlib import Path

from check_automation_report import check_report


class CheckAutomationReportTest(unittest.TestCase):
    def test_no_matching_tests_still_requires_a_successful_editor_exit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "index.json"
            log = Path(directory) / "editor.log"
            log.write_text(
                "No automation tests matched 'StartsWith:Nelaric'\n**** TEST COMPLETE.\n",
                encoding="utf-8",
            )
            check_report(report, log, 0)
            with self.assertRaisesRegex(ValueError, "Unreal Editor exited with status 17"):
                check_report(report, log, 17)

    def test_desktop_run_requires_each_named_test_to_succeed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "index.json"
            log = Path(directory) / "automation.log"
            required = ("Host.ChangeHealth", "Host.ChangeHealthDispatch")
            for name, tests, valid in (
                ("empty", [], False),
                ("partial", [(required[0], "Success")], False),
                ("unrelated", [(required[0], "Success"), ("Other.Test", "Success")], False),
                ("skipped", [(required[0], "Success"), (required[1], "Skipped")], False),
                ("complete", [(test, "Success") for test in required], True),
            ):
                with self.subTest(name=name):
                    report.write_text(json.dumps({
                        "succeeded": len(tests), "succeededWithWarnings": 0, "failed": 0, "notRun": 0,
                        "tests": [{"fullTestPath": path, "state": state} for path, state in tests],
                    }), encoding="utf-8")
                    if valid:
                        check_report(report, log, 0, required_tests=required)
                        with self.assertRaises(ValueError):
                            check_report(report, log, 1, required_tests=required)
                    else:
                        with self.assertRaises(ValueError):
                            check_report(report, log, 0, required_tests=required)

    def test_missing_desktop_report_cannot_pass_from_a_no_match_log(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "automation.log"
            log.write_text("No automation tests matched 'StartsWith:Nelaric'\n**** TEST COMPLETE.\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "report is missing"):
                check_report(Path(directory) / "index.json", log, 0, required_tests=("Host.ChangeHealthDispatch",))
