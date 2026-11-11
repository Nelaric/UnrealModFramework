#!/usr/bin/env sh
# Copyright (c) 2026 Nelaric Contributors

set -eu

repo_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
UMF_CMAKE_EXECUTABLE=$(sh "$repo_dir/Setup/FindCMake.sh")
export UMF_CMAKE_EXECUTABLE
exec sh "$repo_dir/UnrealModFramework/Plugins/ModRuntime/Tools/Build.sh" --setup "$@"
