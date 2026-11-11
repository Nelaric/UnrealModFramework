#!/usr/bin/env sh
# Copyright (c) 2026 Nelaric Contributors

set -eu

cmake_command=${UMF_CMAKE_EXECUTABLE:-cmake}
if ! command -v "$cmake_command" >/dev/null 2>&1; then
    echo "CMake 3.20 or newer is required on PATH." >&2
    exit 1
fi

script_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
build_script="$script_dir/Build.cmake"
if [ "${1-}" = --setup ]; then
    build_script="$script_dir/Setup.cmake"
    shift
fi
toolchain_dir=
target=all
jobs=${CMAKE_BUILD_PARALLEL_LEVEL-}
build_dir=
generator=${CMAKE_GENERATOR-}

usage() {
    echo "Usage: sh Build.sh [toolchain-directory] [--target name] [--jobs count] [--build-dir directory] [--generator name]" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --help) exec "$cmake_command" -DHELP=ON -P "$build_script" ;;
        --target|--jobs|--build-dir|--generator)
            [ "$#" -ge 2 ] && [ -n "$2" ] || usage
            case "$1" in
                --target) target=$2 ;;
                --jobs) jobs=$2 ;;
                --build-dir) build_dir=$2 ;;
                --generator) generator=$2 ;;
            esac
            shift 2
            ;;
        -*) usage ;;
        *)
            [ -z "$toolchain_dir" ] || usage
            toolchain_dir=$1
            shift
            ;;
    esac
done

set -- "-DTOOLCHAIN_DIR=$toolchain_dir" "-DTARGET=$target"
if [ -n "$build_dir" ]; then set -- "$@" "-DBUILD_DIR=$build_dir"; fi
if [ -n "$jobs" ]; then set -- "$@" "-DJOBS=$jobs"; fi
if [ -n "$generator" ]; then set -- "$@" "-DGENERATOR=$generator"; fi
exec "$cmake_command" "$@" -P "$build_script"
