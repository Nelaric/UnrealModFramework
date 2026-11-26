# Copyright (c) 2026 Nelaric Contributors
"""Resolve the installed Windows editor using the native UBT build receipt."""
import json
from pathlib import Path


def windows_editor_artifacts(engine: Path, architecture: str) -> tuple[Path, Path]:
    binaries = engine / "Binaries/Win64"
    matches = []
    for file in binaries.glob("UnrealEditor*.target"):
        receipt = json.loads(file.read_text(encoding="utf-8-sig"))
        if (receipt.get("TargetName") == "UnrealEditor" and receipt.get("Platform") == "Win64"
                and receipt.get("Configuration") == "Development" and receipt.get("Architecture") == architecture):
            matches.append(receipt)
    if len(matches) != 1:
        raise ValueError(f"Expected one Win64 Development Editor receipt for {architecture}, found {len(matches)}")
    receipt = matches[0]
    prefix = "$(EngineDir)/Binaries/Win64/"

    def root_product(value: str) -> bool:
        return value.startswith(prefix) and Path(value[len(prefix):]).name == value[len(prefix):]

    command = receipt["LaunchCmd"].replace("\\", "/")
    products = {item["Path"].replace("\\", "/") for item in receipt["BuildProducts"]}
    modules = [item for item in products if root_product(item) and item.endswith(".modules")]
    if not root_product(command) or command not in products or len(modules) != 1:
        raise ValueError("The Editor receipt lacks its Win64 command-line executable or root module registry")
    paths = binaries / command[len(prefix):], binaries / modules[0][len(prefix):]
    if not all(path.is_file() for path in paths):
        raise FileNotFoundError("The installed Win64 Editor is missing a registered build product")
    return paths
