#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors
"""Compatibility entry point for the Win64 x64 Hook build."""

import sys

from build_hooks import main

if __name__ == "__main__":
    sys.argv[1:1] = ["--platform", "Win64", "--architecture", "x64"]
    sys.exit(main())
