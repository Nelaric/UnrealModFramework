#!/usr/bin/env sh
# Copyright (c) 2026 Nelaric Contributors

set -eu

image=ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1

for delay in 5 15; do
    if docker pull "$image"; then
        exit 0
    fi
    printf 'Unreal Engine image pull failed. Retrying in %s seconds.\n' "$delay"
    sleep "$delay"
done

exec docker pull "$image"
