# Copyright (c) 2026 Nelaric Contributors
"""Regression tests for Unreal automation report validation."""

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
