# Copyright (c) 2026 Nelaric Contributors
"""Transfer Development CI handoffs through an explicitly private OCI package."""

from __future__ import annotations

import json
import os
import re
import subprocess
from datetime import datetime, timezone

from development import api, is_digest, require, timestamp

FILES = {"metadata": ("payload",), "checkpoint": ("payload",)}
CREATED = "org.opencontainers.image.created"
TITLE = "org.opencontainers.image.title"
ARTIFACT_TYPE = "application/vnd.nelaric.development-ci."
REUSE_SECONDS = 3600


def producer_attempt(kind, target, commit, name):
    prefix = re.escape(f"{kind}-{target}-{commit}-")
    match = re.fullmatch(prefix + r"([1-9][0-9]*)", name)
    require(match and int(match[1]) <= int(os.environ["GITHUB_RUN_ATTEMPT"]),
            f"{kind}: expected an artifact from a producing attempt of this candidate")
    return int(match[1])


def validate_manifest(document, kind, *, check_age):
    require(document.get("artifactType") == ARTIFACT_TYPE + kind + ".v1", f"{kind}: unexpected artifact type")
    layers = document.get("layers")
    require(isinstance(layers, list), f"{kind}: no OCI payload descriptors")
    names = []
    total = 0
    for layer in layers:
        names.append(layer.get("annotations", {}).get(TITLE))
        require(type(layer.get("size")) is int and layer["size"] > 0, f"{kind}: missing artifact bytes")
        total += layer["size"]
    expected = {"envelope.json", *FILES[kind]}
    require(len(names) == len(expected) and set(names) == expected, f"{kind}: unexpected artifact files")
    if check_age:
        created = timestamp(document["annotations"][CREATED])
        age = (datetime.now(timezone.utc) - created).total_seconds()
        require(0 <= age < REUSE_SECONDS, f"{kind}: the one-hour reuse window expired. Re-run all jobs.")
    return total


class ArtifactStore:
    def __init__(self):
        self.registry = os.environ.get("CI_TRANSFER_PACKAGE", "")
        require(
            re.fullmatch(r"ghcr\.io/nelaric/[a-z0-9]+(?:[._-][a-z0-9]+)*", self.registry),
            "Set UMF_TRANSFER_PACKAGE to a dedicated private Nelaric container package",
        )
        package = self.registry.rsplit("/", 1)[1]
        self.package = f"/orgs/Nelaric/packages/container/{package}"
        self.user = os.environ.get("UMF_TRANSFER_USER", "")
        self.token = os.environ.get("UMF_TRANSFER_TOKEN", "")
        require(self.user and self.token, "Private artifact credentials are missing from the build environment")
        repository_id = os.environ["GITHUB_REPOSITORY_ID"]
        run_id = os.environ["GITHUB_RUN_ID"]
        require(repository_id.isascii() and repository_id.isdecimal()
                and run_id.isascii() and run_id.isdecimal(), "Invalid workflow run identity")
        self.namespace = f"umf-{repository_id}-{run_id}-"

    def verify_private(self):
        info = api(self.package, self.token)
        repository = info.get("repository")
        require(info.get("visibility") == "private" and (repository is None or repository.get("private") is True),
                "Artifact storage must be private and separate from public repository permissions")

    def tag(self, name):
        return self.namespace + name

    def oras(self, *args, cwd=None):
        env = {key: value for key, value in os.environ.items() if key != "UMF_TRANSFER_TOKEN"}
        return subprocess.run(
            ["oras", *args, "--username", self.user, "--password-stdin"],
            input=self.token + "\n", text=True, check=True, stdout=subprocess.PIPE, cwd=cwd, env=env,
        ).stdout

    def inspect(self, kind, target, commit, name, digest, *, check_age=True):
        attempt = producer_attempt(kind, target, commit, name)
        require(isinstance(digest, str) and digest.startswith("sha256:") and is_digest(digest.removeprefix("sha256:")),
                f"{kind}: missing immutable OCI digest")
        self.verify_private()
        tagged = f"{self.registry}:{self.tag(name)}"
        descriptor = json.loads(self.oras("manifest", "fetch", "--descriptor", tagged))
        require(descriptor.get("digest") == digest, f"{kind}: the artifact tag does not match its producing job")
        manifest = json.loads(self.oras("manifest", "fetch", f"{self.registry}@{digest}"))
        return validate_manifest(manifest, kind, check_age=check_age), attempt

    def push(self, directory, kind, target, commit, name):
        require(producer_attempt(kind, target, commit, name) == int(os.environ["GITHUB_RUN_ATTEMPT"]),
                f"{kind}: upload name differs from the producing attempt")
        self.verify_private()
        created = datetime.now(timezone.utc).isoformat(timespec="seconds")
        # Use explicit file layers and avoid capturing recursive directories or unrelated build files.
        response = json.loads(self.oras(
            "push", "--format", "json", "--artifact-type", ARTIFACT_TYPE + kind + ".v1",
            "--annotation", f"{CREATED}={created}", f"{self.registry}:{self.tag(name)}",
            "envelope.json", *FILES[kind], cwd=directory,
        ))
        digest = response["digest"]
        self.inspect(kind, target, commit, name, digest)
        return digest

    def pull(self, directory, kind, target, commit, name, digest):
        _, attempt = self.inspect(kind, target, commit, name, digest)
        self.oras("pull", "--output", str(directory), f"{self.registry}@{digest}")
        return attempt

    def remove(self, transfers):
        self.verify_private()
        pending = {(digest, self.tag(name)) for name, digest in transfers}
        version_ids = []
        for page in range(1, 101):
            versions = api(f"{self.package}/versions?per_page=100&page={page}", self.token)
            for version in versions:
                tags = version["metadata"]["container"]["tags"]
                if len(tags) != 1 or (version["name"], tags[0]) not in pending:
                    continue
                version_id = version["id"]
                require(type(version_id) is int and version_id > 0, "Invalid package version ID")
                version_ids.append(version_id)
                pending.remove((version["name"], tags[0]))
            if not pending or len(versions) < 100:
                break
        require(not pending, "Some consumed artifact versions could not be removed")
        for version_id in version_ids:
            api(f"{self.package}/versions/{version_id}", self.token, method="DELETE")
