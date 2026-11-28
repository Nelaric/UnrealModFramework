# Copyright (c) 2026 Nelaric Contributors
set -euo pipefail

install_archive() {
  local name=$1 digest=$2
  shift 2
  local archive="$RUNNER_TEMP/$name.tar.gz"
  local directory="$RUNNER_TEMP/$name"
  mkdir -p "$directory"
  for url in "$@"; do
    if curl -fsSL --retry 3 --retry-delay 2 --retry-all-errors \
      --connect-timeout 15 --max-time 180 "$url" -o "$archive"; then
      printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --status
      tar -xzf "$archive" -C "$directory"
      return
    fi
  done
  printf 'Could not download %s\n' "$name" >&2
  return 1
}

install_archive doxygen 14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90 \
  https://www.doxygen.nl/files/doxygen-1.18.0.linux.bin.tar.gz \
  https://github.com/doxygen/doxygen/releases/download/Release_1_18_0/doxygen-1.18.0.linux.bin.tar.gz
install_archive actionlint 8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8 \
  https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_amd64.tar.gz

doxygen_bin=$(find "$RUNNER_TEMP/doxygen" -type f -name doxygen -print -quit)
test -n "$doxygen_bin"
printf '%s\n' "$(dirname "$doxygen_bin")" "$RUNNER_TEMP/actionlint" >> "$GITHUB_PATH"
