# Copyright (c) 2026 Nelaric Contributors
"""Summarize simultaneous filesystem samples from a CircleCI build machine."""

import csv
import json
import sys
from itertools import groupby
from operator import itemgetter


with open(sys.argv[1], newline="", encoding="utf-8") as samples:
    snapshots = []
    for utc, rows in groupby(csv.DictReader(samples), key=itemgetter("utc")):
        volumes = list(rows)
        snapshots.append({
            "utc": utc,
            "used_bytes": sum(int(row["used_bytes"]) for row in volumes),
            "available_bytes": sum(int(row["available_bytes"]) for row in volumes),
        })

initial = snapshots[0]
peak = max(snapshots, key=itemgetter("used_bytes"))
print(json.dumps({
    "samples": len(snapshots),
    "initial": initial,
    "peak": peak,
    "increase_bytes": peak["used_bytes"] - initial["used_bytes"],
    "final": snapshots[-1],
}, indent=2))
