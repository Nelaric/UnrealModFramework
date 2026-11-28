# Copyright (c) 2026 Nelaric Contributors

import hashlib
import json
import os
import re
import shutil
import subprocess
from collections import Counter
from functools import cache
from pathlib import Path, PurePosixPath

EARLY_TYPES = {"Compile", "BuildProject", "GatherModuleDependencies", "CompileModuleInterface", "ParseTimingInfo"}
LINK_TYPES = {"Link", "WriteMetadata", "PostBuildStep", "CreateAppBundle", "GenerateDebugInfo"}
NATIVE_OBJECTS = {".obj", ".o"}
PCH_OUTPUTS = {".pch", ".gch", ".pcm", ".ifc"}
DIGEST = re.compile(r"[0-9a-f]{64}")
RESPONSE = re.compile(r'(?<!\S)@"([^"]+)"|(?<!\S)@([^\s"]+)')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def digest(value):
    data = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def relative_path(name):
    path = PurePosixPath(name)
    require(path.parts and not path.is_absolute() and ".." not in path.parts
            and ":" not in name and "\\" not in name and path.as_posix() == name,
            f"Invalid checkpoint path {name!r}")
    return path


def logical_path(root, name):
    path = relative_path(name)
    result = root.joinpath(*path.parts)
    require(result.resolve().is_relative_to(root.resolve()), f"Checkpoint path escapes {root}: {name}")
    return result


def inventory(root):
    files = {}
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), f"Unexpected symbolic link in checkpoint: {path}")
        if path.is_file():
            files[path.relative_to(root).as_posix()] = file_sha(path)
    return files


def verify_inventory(root, expected):
    files = inventory(root)
    files.pop("manifest.json")
    require(expected == files and all(name.startswith(("native/", "objects/")) for name in files),
            "Checkpoint file inventory differs")
    return files


def project_inputs(project):
    # Source inputs come from the checked-out commit. The action graph verifies generated prerequisites.
    tree = subprocess.check_output(["git", "-C", str(project), "ls-tree", "-rz", "--name-only", "HEAD", "--", "."])
    names = [os.fsdecode(name) for name in tree.split(b"\x00") if name]
    require(names, f"No committed build inputs under {project}")
    return digest({name: file_sha(project / name) for name in names})


def source_modules(document):
    # Each selected owner can receive a host binding from C++ or Blueprint. Defer its TUs even
    # when a Hook definition is in a file without the export declaration.
    return {name for name, path in document["umf"]["modules"].items() if Path(path).is_dir()}


def decoded(path):
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig")


def output_path(project, filename):
    path = Path(filename).resolve()
    require(path.is_relative_to(project), f"UBT action would write outside the project: {filename}")
    relative = path.relative_to(project)
    require(any(part in {"Binaries", "Intermediate"} for part in relative.parts[:-1]),
            f"UBT action would write outside a project build directory: {filename}")
    return relative.as_posix()


class ActionGraph:
    def __init__(self, document, project):
        self.document = document
        self.project = project
        self.engine = Path(document["umf"]["engine"]).resolve()
        self.actions = {action["Id"]: action for action in document["Actions"]}
        require(len(self.actions) == len(document["Actions"]), "UBT action IDs are not unique")
        self.outputs = {}
        self.paths = {}
        for action_id, action in self.actions.items():
            outputs = [output_path(project, item) for item in action["ProducedItems"]]
            for item in action.get("DeleteItems", []):
                output_path(project, item)
            require(outputs, f"UBT action has no outputs: {action['StatusDescription']}")
            self.outputs[action_id] = {name: Path(path) for name, path in zip(outputs, action["ProducedItems"])}
            for name in outputs:
                require(name not in self.paths, f"UBT action output has multiple producers: {name}")
                self.paths[name] = action_id
        self.edges = {key: set(action.get("PrerequisiteActions", [])) for key, action in self.actions.items()}
        # Imported execution graphs also connect by prerequisite file.
        for key, action in self.actions.items():
            for path in action.get("PrerequisiteItems", []):
                if (relative := self.project_relative(Path(path))) in self.paths:
                    self.edges[key].add(self.paths[relative])
        require(all(edges <= self.actions.keys() for edges in self.edges.values()), "UBT action has a missing prerequisite")
        self.order = topological(self.edges)
        self.keys = {key: digest([action["Type"], sorted(self.outputs[key])]) for key, action in self.actions.items()}

    def project_relative(self, path):
        try:
            return path.resolve().relative_to(self.project).as_posix()
        except ValueError:
            return None

    def partition(self, root_reason):
        deferred = {}
        for key in self.order:
            reason = root_reason(key, self.actions[key])
            if reason:
                deferred[key] = reason
            elif any(parent in deferred for parent in self.edges[key]):
                deferred[key] = "prerequisite"
        return set(self.actions) - deferred.keys(), deferred

    def without_metadata(self):
        modules = {name.casefold() for name in source_modules(self.document)}
        def reason(key, action):
            if action["Type"] not in EARLY_TYPES:
                return "final-build"
            for output in self.outputs[key]:
                if any(part.casefold() in modules for part in PurePosixPath(output).parts[:-1]):
                    return "export-module"
                if ".umfhost." in output or "ModTools/" in output:
                    return "generated-source"
            return None

        return self.partition(reason)

    def execute(self, selected, destination, execute):
        if not selected:
            return
        ids = {old: new for new, old in enumerate(key for key in self.order if key in selected)}
        actions = []
        for key, new in ids.items():
            actions.append({**self.actions[key], "Id": new,
                            "PrerequisiteActions": sorted(ids[parent] for parent in self.edges[key] if parent in ids)})
        write_json(destination, {**self.document, "Actions": actions})
        execute(destination)

    def check_outputs(self, selected):
        for key in selected:
            action_type = self.actions[key]["Type"]
            for path in self.outputs[key].values():
                # UBT lists a virtual .ran output for a post-build script and emits SARIF only for diagnostics.
                if path.suffix == ".sarif" or (action_type == "PostBuildStep" and path.suffix == ".ran"):
                    continue
                directory = path.suffix in {".app", ".dSYM"} and action_type in LINK_TYPES
                require(path.is_dir() if directory else path.is_file(), f"UBT did not produce {path}")


def topological(edges):
    pending = {key: set(dependencies) for key, dependencies in edges.items()}
    order = []
    while pending:
        ready = sorted(key for key, dependencies in pending.items() if not dependencies)
        require(ready, "UBT action graph contains a cycle")
        order.extend(ready)
        for key in ready:
            del pending[key]
        for dependencies in pending.values():
            dependencies.difference_update(ready)
    return order


@cache
def clang_resource_directory(compiler):
    if not re.fullmatch(r"clang(?:\+\+|-cl)?(?:-\d+)?(?:\.exe)?", compiler.name.lower()):
        return None
    reported = subprocess.check_output([str(compiler), "-print-resource-dir"], text=True).strip()
    resource = Path(reported)
    return resource.resolve() if resource.is_absolute() and resource.is_dir() else None


class ActionInputs:
    def __init__(self, graph, key, file_hashes):
        self.graph = graph
        self.key = key
        self.action = graph.actions[key]
        self.file_hashes = file_hashes
        self.work = Path(self.action["WorkingDirectory"]).resolve()
        self.roots = {"project": graph.project, "engine": graph.engine}
        for root in self.action.get("RootPaths", {}).get("Lookup", []):
            if root["Root"] not in {"Project", "Root"}:
                self.roots[f"toolchain-{root['Root']}"] = Path(root["Path"]).resolve()
        # Runner-provided Clang can resolve outside the installed SDK via a toolchain symlink.
        resource = clang_resource_directory(self.path(self.action["CommandPath"]))
        if resource is not None and not any(resource.is_relative_to(root) for root in self.roots.values()):
            self.roots["toolchain-clang-resources"] = resource

    def path(self, filename):
        path = Path(filename)
        return path.resolve() if path.is_absolute() else (self.work / path).resolve()

    def locate(self, filename):
        path = self.path(filename)
        for name, root in sorted(self.roots.items(), key=lambda item: -len(item[1].parts)):
            if path.is_relative_to(root):
                relative = path.relative_to(root).as_posix()
                return f"{name}/{relative.lower() if os.name == 'nt' else relative}"
        return None

    def normalize(self, value):
        for name, root in sorted(self.roots.items(), key=lambda item: -len(str(item[1]))):
            expression = re.escape(root.as_posix())
            if os.name == "nt":
                expression = expression.replace("/", r"[/\\]")
            value = re.sub(expression + r"(?=[/\\\"'\s;,=]|$)", "$" + name,
                           value, flags=re.IGNORECASE if os.name == "nt" else 0)
        return value

    def unlocate(self, filename):
        name, relative = filename.split("/", 1)
        return logical_path(self.roots[name], relative)

    def hash_input(self, path):
        filename = self.locate(path)
        require(filename is not None, f"No logical root for compiler input {path}")
        # A UHT unity file can contain includes written with the producer's checkout root.
        if filename.startswith("project/") and "/intermediate/" in filename.lower():
            try:
                text = decoded(path)
            except UnicodeDecodeError:
                return self.file_hashes(path)
            return hashlib.sha256(self.normalize(text).encode()).hexdigest()
        return self.file_hashes(path)

    def recipe(self):
        def expanded(arguments, parents):
            def response(match):
                path = self.path(next(value for value in match.groups() if value is not None))
                require(path not in parents, f"Recursive compiler response file {path}")
                return "@" + hashlib.sha256(expanded(decoded(path), parents | {path}).encode()).hexdigest()

            return self.normalize(RESPONSE.sub(response, arguments))

        command = self.path(self.action["CommandPath"])
        return digest([self.file_hashes(command), self.action.get("CommandVersion"), self.normalize(str(self.work)),
                       expanded(self.action["CommandArguments"], set()), self.action.get("ArtifactMode")])

    def absolute_roots(self):
        if "AbsolutePath" not in self.action.get("ArtifactMode", ""):
            return None
        # UBT marks some compiler actions as sensitive to the absolute input/output spelling.
        # Compare an opaque fingerprint, without publishing paths from the producer.
        return digest({name: os.path.normcase(str(root)) for name, root in self.roots.items()})

    def prerequisites(self):
        file = self.path(self.action["DependencyListFile"])
        if file.suffix == ".json":
            data = read_json(file)["Data"]
            paths = [data["Source"], *data["Includes"]]
        elif file.suffix == ".d":
            # GNU Make dependencies emitted by Clang escape whitespace and continue lines with \.
            line = re.sub(r"\\\r?\n", " ", decoded(file)).splitlines()[0]
            rule = re.split(r":(?:\s|$)", line, maxsplit=1)
            require(len(rule) == 2, f"Missing dependency rule in {file}")
            paths = [re.sub(r"\\([ #:\\])", r"\1", token).replace("$$", "$")
                     for token in re.findall(r"(?:\\.|\S)+", rule[1])]
        elif file.suffix == ".txt":
            paths = [line.replace("\\\\", "\\") for line in decoded(file).splitlines()
                     if line and not line.lower().endswith((".tlh", ".tli"))]
        else:
            raise ValueError(f"Unknown compiler dependency format: {file}")
        require(paths, f"Compiler did not record dependencies in {file}")
        result = {}
        for path in set(paths):
            resolved = self.path(path)
            logical = self.locate(resolved)
            require(logical, f"No logical root for compiler input {path}")
            relative = logical.split("/", 1)[-1]
            require(not any(marker in relative.lower() for marker in ("/modtools/", ".umfhost.", ".modgenerated.")),
                    f"Metadata-sensitive input was compiled during Game preparation: {path}")
            if resolved.suffix.lower() in PCH_OUTPUTS:
                producer = self.graph.paths.get(self.graph.project_relative(resolved))
                if (producer in self.graph.edges[self.key]
                        and portable_object(self.graph, producer)[1] == "precompiled-header"):
                    # This compiler output is regenerated. Its producer recipe and inputs are checked on resume.
                    continue
            result[logical] = self.hash_input(resolved)
        return result


def portable_object(graph, key):
    action = graph.actions[key]
    objects = [name for name, path in graph.outputs[key].items() if path.suffix in NATIVE_OBJECTS]
    if action["Type"] != "Compile":
        return None, "sdk-runtime-copy" if action["Type"] == "BuildProject" else "local-tool-output"
    if any(path.suffix.lower() in PCH_OUTPUTS for path in graph.outputs[key].values()):
        return None, "precompiled-header"
    if len(objects) != 1 or not action.get("DependencyListFile"):
        return None, "untracked-compiler-inputs"
    if any(path.suffix not in NATIVE_OBJECTS | {".sarif", ".json", ".d", ".txt"}
           for path in graph.outputs[key].values()):
        return None, "compiler-side-effects"
    if "Enabled" not in action.get("ArtifactMode", ""):
        return None, "uncacheable-compiler-action"
    return objects[0], None


def save_actions(graph, payload):
    early, deferred = graph.without_metadata()
    require(early and deferred, "Game checkpoint needs both completed and pending actions")
    graph.check_outputs(early)
    result = {}
    file_hashes = cache(file_sha)
    for key in graph.order:
        action_key = graph.keys[key]
        row = {"type": graph.actions[key]["Type"], "outputs": sorted(graph.outputs[key]),
               "dependencies": sorted(graph.keys[p] for p in graph.edges[key])}
        result[action_key] = row
        if key in deferred:
            row["pending"] = deferred[key]
            continue
        item, reason = portable_object(graph, key)
        if reason:
            row["replay"] = reason
            # PCH preparation remains useful to the producer, while consumer compilers regenerate it.
            if reason != "precompiled-header":
                continue
            if Path(graph.actions[key]["CommandPath"]).name.lower() == "cl.exe":
                row["pch_objects"] = {name: file_hashes(path) for name, path in graph.outputs[key].items()
                                      if path.suffix == ".obj"}
                require(row["pch_objects"], "MSVC precompiled header has no companion object")
        inputs = ActionInputs(graph, key, file_hashes)
        try:
            row["inputs"] = inputs.prerequisites()
        except KeyError:
            if not reason:
                raise
            row["inputs"] = {}
        row["recipe"] = inputs.recipe()
        row["absolute_roots"] = inputs.absolute_roots()
        if item is not None:
            stored = logical_path(payload, f"objects/{action_key}{PurePosixPath(item).suffix}")
            stored.parent.mkdir(exist_ok=True)
            shutil.copy2(graph.outputs[key][item], stored)
            row["object"] = stored.relative_to(payload).as_posix()
            row["output"] = item
    return result


def validate_actions(actions, files):
    order = topological({key: set(row["dependencies"]) for key, row in actions.items()})
    objects = set()
    for key in order:
        row = actions[key]
        outputs = row["outputs"]
        require(outputs and outputs == sorted(set(outputs)) and key == digest([row["type"], outputs]),
                "Checkpoint action identity differs from its project outputs")
        require(all({"Binaries", "Intermediate"} & set(relative_path(path).parts[:-1]) for path in outputs),
                "Checkpoint action output is outside the build directories")
        state = {"pending", "replay", "object"} & row.keys()
        require(len(state) == 1, f"Invalid checkpoint action state: {key}")
        if "pending" in row:
            require(row["pending"] in {"export-module", "generated-source", "final-build", "prerequisite"},
                    "Unknown checkpoint deferral")
            if row["pending"] == "prerequisite":
                require(any("pending" in actions[parent] for parent in row["dependencies"]),
                        "Metadata-independent action still pending")
            if row["pending"] == "final-build":
                require(row["type"] not in EARLY_TYPES, "Early action deferred without a prerequisite")
        else:
            require(not any("pending" in actions[parent] for parent in row["dependencies"]),
                    "Completed action depends on unfinished work")
        if "object" in row:
            require(row["type"] == "Compile" and row["object"].startswith(f"objects/{key}.")
                    and PurePosixPath(row["object"]).suffix in NATIVE_OBJECTS,
                    "Checkpoint attempts to transfer a non-object file")
            require(row["output"] in outputs and PurePosixPath(row["output"]).suffix == relative_path(row["object"]).suffix,
                    "Invalid checkpoint object destination")
            require(row["inputs"] and DIGEST.fullmatch(row["recipe"]), "Checkpoint has no compiler input proof")
            objects.add(row["object"])
        for name, sha in row.get("inputs", {}).items():
            root, relative = name.split("/", 1)
            require((root in {"project", "engine"} or root.startswith("toolchain-")) and DIGEST.fullmatch(sha),
                    f"Invalid checkpoint compiler input: {name}")
            relative_path(relative)
        for path, sha in row.get("pch_objects", {}).items():
            require(row.get("replay") == "precompiled-header" and path in outputs
                    and relative_path(path).suffix == ".obj" and DIGEST.fullmatch(sha),
                    "Invalid companion object for a precompiled header")
    require(objects == {name for name in files if name.startswith("objects/")}, "Checkpoint object inventory differs")
    pending = sum("pending" in row for row in actions.values())
    reasons = Counter(row["replay"] for row in actions.values() if "replay" in row)
    require(0 < pending < len(actions), "Checkpoint needs completed and pending actions")
    return {"completed_actions": len(actions) - pending, "pending_actions": pending,
            "metadata_independent_pending_actions": 0, "nonportable_actions": dict(reasons)}


def restore_objects(graph, payload, actions):
    residual = set(graph.actions)
    restored = []
    replayed = 0
    fresh = {value: key for key, value in graph.keys.items()}
    file_hashes = cache(file_sha)
    checked_inputs = set()
    checked_pch = set()
    for action_key, row in actions.items():
        if "replay" in row:
            replayed += action_key in fresh
            continue
        if "object" not in row:
            continue
        require(action_key in fresh, f"Resumed graph is missing prepared object {row['output']}")
        key = fresh[action_key]
        inputs = ActionInputs(graph, key, file_hashes)
        require(row["absolute_roots"] == inputs.absolute_roots(),
                f"UBT requires matching absolute compiler roots for {row['output']}")
        require(row["recipe"] == inputs.recipe(), f"Compiler invocation changed for {row['output']}")
        require(set(row["dependencies"]) == {graph.keys[parent] for parent in graph.edges[key]},
                f"Compiler prerequisites changed for {row['output']}")
        for dependency_key, prerequisite in ((parent, actions[parent]) for parent in row["dependencies"]):
            if prerequisite.get("replay") == "precompiled-header" and dependency_key not in checked_pch:
                pch = ActionInputs(graph, fresh[dependency_key], file_hashes)
                require(prerequisite["recipe"] == pch.recipe(), f"PCH invocation changed for {row['output']}")
                check_inputs(pch, prerequisite["inputs"], checked_inputs)
                checked_pch.add(dependency_key)
        check_inputs(inputs, row["inputs"], checked_inputs)
        target = graph.outputs[key][row["output"]]
        artifact = logical_path(payload, row["object"])
        restored.append((artifact, target))
        residual.remove(key)
    for artifact, target in restored:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(artifact, target)
    return residual, len(restored), replayed


def check_inputs(inputs, stored, checked):
    roots = tuple(sorted(inputs.roots.items()))
    for name, sha in stored.items():
        identity = (roots, name, sha)
        if identity in checked:
            continue
        path = inputs.unlocate(name)
        require(path.is_file() and inputs.hash_input(path) == sha, f"Compiler prerequisite changed: {name}")
        checked.add(identity)


def verify_pch_objects(graph, actions):
    # MSVC's COFF object carries the PCH debug type records. Linker success alone can hide a mismatch.
    for key, outputs in graph.outputs.items():
        for name, sha in actions.get(graph.keys[key], {}).get("pch_objects", {}).items():
            require(file_sha(outputs[name]) == sha, f"Regenerated MSVC PCH object differs: {name}")
