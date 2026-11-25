#!/usr/bin/env sh
# Copyright (c) 2026 Nelaric Contributors

set -eu

root=$(CDPATH= cd -P "$(dirname "$0")/.." && pwd)
source="$root/UnrealModFramework/Plugins/ModRuntime"
tools="$source/Tools/build"
system=$(uname -s)
case "$(uname -m)" in
    x86_64|amd64) arch=x64 ;;
    aarch64|arm64) arch=arm64 ;;
    *) echo 'Setup supports x64 and ARM64 here.' >&2; exit 1 ;;
esac
receipt="libclang-$system-$arch.path"

if [ ! -f "$source/ModRuntime.uplugin" ]; then
    echo 'ModRuntime.uplugin is missing from this checkout.' >&2
    exit 1
fi

require_setup_file() {
    if [ ! -f "$1" ]; then
        echo "Missing $1. Run sh Setup.sh in this checkout first." >&2
        exit 1
    fi
}

require_setup_file "$tools/$receipt"
library="$tools/$(cat "$tools/$receipt")"
require_setup_file "$library"
clang_root=$(CDPATH= cd -P "$(dirname "$library")/.." && pwd)
for tool in ModHeaderTool ModBuildTool ModSourceTool ModSDKTool; do
    require_setup_file "$tools/$tool/$tool"
done

dist="$root/Dist"
mkdir -p "$dist"
dist=$(CDPATH= cd -P "$dist" && pwd)
stage=$(mktemp -d "$dist/.ModRuntime-XXXXXXXX")
plugin="$stage/ModRuntime"
output_tools="$plugin/Tools/build"
mkdir "$plugin"

# Copy the plugin tree without existing build caches.
(
    cd "$source"
    find . \( -path './Tools/build' -o -name Binaries -o -name Intermediate -o -name Saved \
        -o -name DerivedDataCache -o -name obj -o -name .vs -o -name .git \) -prune -o \
        -type f ! -name '*.ubtplugin.csproj.props' ! -name '*.modsource-lock' -exec sh -ec '
            plugin=$1
            shift
            for file; do
                mkdir -p "$plugin/${file%/*}"
                cp -p "$file" "$plugin/$file"
            done
        ' sh "$plugin" {} +
)
cp -p "$root/LICENSE" "$root/NOTICE.md" "$plugin/"

for tool in ModHeaderTool ModBuildTool ModSourceTool ModSDKTool; do
    mkdir -p "$output_tools/$tool"
    cp -p "$tools/$tool/$tool" "$output_tools/$tool/"
done
cp -pR "$tools/ModSDKTool/Licenses" "$output_tools/ModSDKTool/"
mkdir "$output_tools/LibClang"
cp -pR "$clang_root" "$output_tools/LibClang/"
printf 'LibClang/%s/lib/%s\n' "${clang_root##*/}" "${library##*/}" > "$output_tools/$receipt"

output="$dist/ModRuntime"
if [ -e "$output" ] || [ -L "$output" ]; then
    if [ ! -f "$output/ModRuntime.uplugin" ]; then
        echo "Cannot replace '$output' because it is missing ModRuntime.uplugin." >&2
        exit 1
    fi
    mv "$output" "$stage/previous"
fi
if ! mv "$plugin" "$dist"; then
    if [ -e "$stage/previous" ] && [ ! -e "$output" ]; then
        mv "$stage/previous" "$output"
    fi
    exit 1
fi
rm -rf "$stage"
printf '%s\n' "$output"
