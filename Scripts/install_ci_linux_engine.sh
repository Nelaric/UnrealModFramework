#!/usr/bin/env bash
# Copyright (c) 2026 Nelaric Contributors

set -euo pipefail

destination=$1
if [[ "$TRUSTED_SOURCE" != true ]]; then
    echo "::error::A maintainer must review this revision and run it from a source-repository branch to access the private engine."
    exit 1
fi

work=$(mktemp -d "${RUNNER_TEMP}/umf-ghcr.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
export DOCKER_CONFIG="$work/auth"
mkdir -m 700 "$DOCKER_CONFIG"
image=ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1
# Resolve the official tag once, then verify and extract the OCI layers by digest.
url=https://github.com/google/go-containerregistry/releases/download/v0.22.1/go-containerregistry_Linux_x86_64.tar.gz
curl -fsSL --retry 3 "$url" -o "$work/crane.tar.gz"
echo "0ab7a1d6932a213aed964ce97666c3077fe691c8606413674a8b3e0b9ec4cda0  $work/crane.tar.gz" | sha256sum --check --status
tar -xf "$work/crane.tar.gz" -C "$work" crane

if ! printf '%s' "$GHCR_TOKEN" | "$work/crane" auth login ghcr.io --username "$GHCR_USERNAME" --password-stdin ||
   ! digest=$("$work/crane" digest --platform linux/amd64 "$image"); then
    echo "::error::Cannot read Epic's dev-slim-5.6.1 image. Configure the Actions secrets GHCR_USERNAME and GHCR_TOKEN for an Epic-linked account with read:packages access."
    exit 1
fi
unset GHCR_TOKEN
echo "Installing $image ($digest). Runner disk:"
df -h "$RUNNER_TEMP"
mkdir "$destination"
"$work/crane" export --platform linux/amd64 "${image%:*}@$digest" - |
    tar -xf - --no-same-owner --strip-components=3 -C "$destination" home/ue4/UnrealEngine/Engine
test -f "$destination/Engine/Build/Build.version"
