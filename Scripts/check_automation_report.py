# Copyright (c) 2026 Nelaric Contributors
"""Check the result of the project's Unreal automation run."""

from __future__ import annotations

import json
import sys
from pathlib import Path

DEVELOPMENT_TESTS = frozenset({
    "Nelaric.UnrealModFramework.Host.ChangeHealth",
    "Nelaric.UnrealModFramework.Host.ChangeHealthDispatch",
    "Nelaric.UnrealModFramework.Save.PersistentReferences",
})


def check_development_report(report_path: Path) -> int:
    """Validate the headless Development Editor's full Nelaric test report."""
    if not report_path.is_file():
        raise ValueError("Automation report is missing")
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    if not isinstance(report, dict):
        raise ValueError("Automation report must be a JSON object")
    counts = {name: report.get(name) for name in ("succeeded", "succeededWithWarnings", "failed", "notRun")}
    counts["inProcess"] = report.get("inProcess", 0)
    if any(type(value) is not int or value < 0 for value in counts.values()):
        raise ValueError("Automation report has invalid test counts")
    if counts["failed"] or counts["notRun"] or counts["inProcess"]:
        raise ValueError("Automation tests failed or did not finish")
    tests = report.get("tests")
    if not isinstance(tests, list) or not tests or len(tests) != counts["succeeded"] + counts["succeededWithWarnings"]:
        raise ValueError("Automation report does not account for the discovered tests")
    names = set()
    warned = 0
    for test in tests:
        if not isinstance(test, dict):
            raise ValueError("Automation report has an invalid test entry")
        name = test.get("fullTestPath")
        if not isinstance(name, str) or not name.startswith("Nelaric.") or name in names:
            raise ValueError(f"Invalid or duplicate Nelaric test path: {name!r}")
        if test.get("state") != "Success" or type(test.get("errors")) is not int or test["errors"] != 0:
            raise ValueError(f"Automation test did not pass: {name}")
        if type(test.get("warnings")) is not int or test["warnings"] < 0:
            raise ValueError(f"Invalid warning count: {name}")
        warned += test["warnings"] > 0
        names.add(name)
    if warned != counts["succeededWithWarnings"]:
        raise ValueError("Automation warning totals differ from test results")
    missing = DEVELOPMENT_TESTS - names
    if missing:
        raise ValueError("Required automation coverage is missing: " + ", ".join(sorted(missing)))
    print(f"Automation results: {len(tests)} Nelaric test(s) passed, {warned} with warnings")
    return len(tests)


def check_report(report_path: Path, log_path: Path, editor_status: int) -> None:
    if not report_path.is_file():
        if log_path.is_file():
            log = log_path.read_text(encoding="utf-8", errors="replace")
            if "No automation tests matched 'StartsWith:Nelaric'" in log and "**** TEST COMPLETE." in log:
                if editor_status:
                    raise ValueError(f"Unreal Editor exited with status {editor_status}")
                print("No Nelaric automation tests were registered")
                return
        raise ValueError("Automation report is missing")

    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    if not isinstance(report, dict):
        raise ValueError("Automation report must be a JSON object")
    succeeded = report.get("succeeded")
    failed = report.get("failed")
    warnings = report.get("succeededWithWarnings")
    not_run = report.get("notRun")
    in_process = report.get("inProcess", 0)
    if any(not isinstance(value, int) or value < 0 for value in (succeeded, failed, warnings, not_run, in_process)):
        raise ValueError("Automation report has invalid test counts")
    print(
        f"Automation results: {succeeded} succeeded, {warnings} with warnings, "
        f"{failed} failed, {not_run} not run, {in_process} in progress"
    )
    if failed or not_run or in_process:
        raise ValueError("Automation tests failed or did not finish")
    if editor_status:
        raise ValueError(f"Unreal Editor exited with status {editor_status}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: check_automation_report.py REPORT_JSON EDITOR_LOG EDITOR_STATUS")
    check_report(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]))
