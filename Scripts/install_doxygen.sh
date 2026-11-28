#!/usr/bin/env bash
# Copyright (c) 2026 Nelaric Contributors
set -euo pipefail

archive_name=doxygen-1.18.0.linux.bin.tar.gz
archive="$RUNNER_TEMP/$archive_name"
directory="$RUNNER_TEMP/doxygen"
expected_sha256=14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90
urls=(
  "https://github.com/doxygen/doxygen/releases/download/Release_1_18_0/$archive_name"
  "https://www.doxygen.nl/files/$archive_name"
)

for url in "${urls[@]}"; do
  if curl -fsSL --retry 3 --retry-delay 2 --retry-all-errors \
    --connect-timeout 15 --max-time 180 "$url" -o "$archive" &&
    printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum --check; then
    mkdir -p "$directory"
    tar -xzf "$archive" -C "$directory"
    doxygen_bin="$(find "$directory" -type f -name doxygen -print -quit)"
    test -n "$doxygen_bin"
    dirname "$doxygen_bin" >> "$GITHUB_PATH"
    exit 0
  fi
done

echo 'Could not download the verified Doxygen 1.18.0 Linux archive.' >&2
exit 1
