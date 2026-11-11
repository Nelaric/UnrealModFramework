#!/usr/bin/env sh
# Copyright (c) 2026 Nelaric Contributors

set -eu

usable_cmake() {
    version_output=$("$1" --version 2>/dev/null) || return 1
    printf '%s\n' "$version_output" | awk '
        NR == 1 && $1 == "cmake" && $2 == "version" {
            split($3, version, ".")
            usable = version[1] + 0 > 3 || (version[1] + 0 == 3 && version[2] + 0 >= 20)
        }
        END { exit !usable }
    '
}

setup_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd)
manifest="$setup_dir/CMakePackages.txt"
platform=
executable=bin/cmake
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) platform=linux-x86_64 ;;
    Linux:aarch64|Linux:arm64) platform=linux-aarch64 ;;
    Darwin:x86_64|Darwin:arm64)
        platform=macos-universal
        executable=CMake.app/Contents/bin/cmake
        ;;
esac

version=$(awk '$1 == "version" { print $2 }' "$manifest")
package="cmake-$version-$platform"
cache="$(dirname "$setup_dir")/.tools/cmake"
cached_cmake="$cache/$package/$executable"
if [ -x "$cached_cmake" ] && usable_cmake "$cached_cmake"; then
    printf '%s\n' "$cached_cmake"
    exit 0
fi
if system_cmake=$(command -v cmake) && usable_cmake "$system_cmake"; then
    printf '%s\n' "$system_cmake"
    exit 0
fi

sha256=$(awk -v platform="$platform" '$1 == platform { print $2 }' "$manifest")
if [ -z "$version" ] || [ -z "$sha256" ]; then
    echo "No portable CMake package is available for this platform. Put CMake 3.20 or newer on PATH." >&2
    exit 1
fi

mkdir -p "$cache"
work_dir=$(mktemp -d "$cache/.download-XXXXXXXXXX")
trap 'rm -rf -- "$work_dir"' 0
trap 'exit 1' HUP INT TERM
archive="$work_dir/$package.tar.gz"
url="https://cmake.org/files/v${version%.*}/$package.tar.gz"
echo "Downloading CMake $version for $platform..." >&2
if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error --retry 2 --connect-timeout 20 --max-time 300 --output "$archive" "$url"
elif command -v wget >/dev/null 2>&1; then
    wget --quiet --timeout=30 --tries=3 --output-document="$archive" "$url"
else
    echo "Install curl or wget to download workspace CMake." >&2
    exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$archive")
else
    actual=$(shasum -a 256 "$archive")
fi
if [ "${actual%% *}" != "$sha256" ]; then
    echo "CMake archive SHA-256 verification failed." >&2
    exit 1
fi
tar -xzf "$archive" -C "$work_dir"
unpacked_cmake="$work_dir/$package/$executable"
if ! usable_cmake "$unpacked_cmake"; then
    echo "The portable CMake could not run. Put a platform-compatible CMake 3.20 or newer on PATH." >&2
    exit 1
fi

# Rename the complete package on the same filesystem. Concurrent downloads may share the result.
if ! "$unpacked_cmake" -E rename "$work_dir/$package" "$cache/$package" && ! usable_cmake "$cached_cmake"; then
    echo "Could not publish workspace CMake to $cache/$package." >&2
    exit 1
fi
printf '%s\n' "$cached_cmake"
