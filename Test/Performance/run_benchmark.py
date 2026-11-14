#!/usr/bin/env python3
# Copyright (c) 2026 Nelaric Contributors

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from pathlib import Path


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
FRAMEWORK = REPO / "UnrealModFramework" / "Plugins" / "ModRuntime"
GAME = "PerformanceBenchmark"
EDITOR_TARGET = GAME + "Editor"
TEST = "Nelaric.UnrealModFramework.Performance.CallBenchmark"
DEVELOPMENT_EDITOR = FRAMEWORK / "Tools/ModSDKTool/Resources/DevelopmentEditor"
MOD = "BenchmarkMod"
BUILD_ATTEMPTS = 20
TRACE_FLAGS = ("-NoTraceServer", "-TraceAutoStart=0")
NO_WINDOW = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


@contextmanager
def build_path(work):
    if os.name != "nt":
        yield work
        return
    # UBT and MSVC enforce Windows path limits even with an isolated temp directory.
    for letter in "ZYXWVUTSRQPONMLKJIHGFED":
        drive = f"{letter}:"
        if not Path(drive + "/").exists():
            mounted = subprocess.run(["subst", drive, str(work)], capture_output=True, check=False,
                                     creationflags=NO_WINDOW)
            if mounted.returncode == 0:
                break
    else:
        raise RuntimeError("No free Windows drive letter for the benchmark build")
    try:
        yield Path(drive + "/")
    finally:
        subprocess.run(["subst", drive, "/D"], capture_output=True, check=True, creationflags=NO_WINDOW)


def remove_build(work):
    directory = str(work)
    options = {}
    if os.name == "nt":
        directory = "\\\\?\\" + directory
        options["onerror"] = windows_remove_readonly_link()
    for attempt in range(30):
        try:
            shutil.rmtree(directory, **options)
            return
        except FileNotFoundError:
            if not work.exists():
                return
        except OSError:
            if attempt == 29:
                raise
        time.sleep(1)


def windows_remove_readonly_link():
    import ctypes
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    open_file = kernel.CreateFileW
    open_file.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                          wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    open_file.restype = wintypes.HANDLE
    set_information = kernel.SetFileInformationByHandle
    set_information.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    set_information.restype = wintypes.BOOL
    close_file = kernel.CloseHandle
    close_file.argtypes = [wintypes.HANDLE]
    close_file.restype = wintypes.BOOL

    def remove(operation, path, exc_info):
        error = exc_info[1]
        if operation is not os.unlink or getattr(error, "winerror", None) != 5:
            raise error
        # Delete this hard link without changing the attributes of the installed engine file.
        handle = open_file(path, 0x10000, 7, None, 3, 0x00200000, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            # FileDispositionInfoEx with DELETE, POSIX_SEMANTICS and IGNORE_READONLY_ATTRIBUTE.
            disposition = wintypes.DWORD(0x13)
            if not set_information(handle, 21, ctypes.byref(disposition), ctypes.sizeof(disposition)):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            close_file(handle)

    return remove


def link_or_copy(source, destination):
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)
    return destination


def pe_exports(binary):
    data = binary.read_bytes()

    def unpack(fmt, offset):
        return struct.unpack_from("<" + fmt, data, offset)

    pe = unpack("I", 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0" or unpack("H", pe + 24)[0] != 0x20B:
        raise RuntimeError(f"Expected a PE32+ DLL: {binary}")
    count, _, _, _, optional_size = unpack("HIIIH", pe + 6)
    sections = [unpack("IIII12xI", pe + 24 + optional_size + index * 40 + 8) for index in range(count)]

    def file_offset(rva):
        for virtual_size, address, raw_size, offset, _ in sections:
            if address <= rva < address + max(virtual_size, raw_size):
                return offset + rva - address
        raise RuntimeError(f"Invalid PE address in {binary}")

    directory, size = unpack("II", pe + 24 + 112)
    if not directory:
        return
    _, name_count, functions, names, ordinals = unpack("IIIII", file_offset(directory) + 20)
    function_table = file_offset(functions)
    name_table = file_offset(names)
    ordinal_table = file_offset(ordinals)
    for index in range(name_count):
        name = file_offset(unpack("I", name_table + index * 4)[0])
        ordinal = unpack("H", ordinal_table + index * 2)[0]
        rva = unpack("I", function_table + ordinal * 4)[0]
        is_data = not (directory <= rva < directory + size) and not any(
            address <= rva < address + max(virtual_size, raw_size)
            and flags & 0x20000000
            for virtual_size, address, raw_size, _, flags in sections
        )
        yield data[name:data.index(b"\0", name)].decode("ascii"), is_data


class Unreal:
    def __init__(self, root, work, short_work, runtime_engine=None):
        self.engine = (root / "Engine" if (root / "Engine").is_dir() else root).resolve()
        self.work = work
        self.short_work = short_work
        self.host = work / "Host" / f"{GAME}.uproject"
        self.platform = {"Windows": "Win64", "Linux": "Linux", "Darwin": "Mac"}[platform.system()]
        self.with_mod = self.platform == "Win64" and platform.machine().lower() in {"amd64", "x86_64"}
        self.game_engine = self.engine
        self.modular_runtime = runtime_engine
        self.staged_runtime = None
        self.cook_platform = "Windows" if self.platform == "Win64" else self.platform
        editor = {
            "Win64": "Win64/UnrealEditor.exe",
            "Linux": "Linux/UnrealEditor",
            "Mac": "Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor",
        }
        self.editor = self.engine / "Binaries" / editor[self.platform]
        self.commandlet = self.editor.with_name("UnrealEditor-Cmd.exe") if self.platform == "Win64" else self.editor
        suffix = ".exe" if self.platform == "Win64" else ""
        platform_name = {"Win64": "win", "Linux": "linux", "Mac": "mac"}[self.platform]
        arch = "arm64" if platform.machine().lower() in {"arm64", "aarch64"} else "x64"
        dotnet = self.engine / "Binaries" / "ThirdParty" / "DotNet"
        candidates = sorted(dotnet.glob(f"*/{platform_name}-{arch}/dotnet{suffix}"))
        if not candidates or not self.editor.is_file():
            raise RuntimeError(f"UE Editor and bundled .NET are required under {self.engine}")
        self.dotnet = candidates[-1]
        self.ubt = self.engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
        self.uat = self.engine / "Binaries/DotNET/AutomationTool/AutomationTool.dll"
        self.env = os.environ.copy()
        self.env["uebp_LogFolder"] = str(work / "UATLogs")
        self.env["UE-LocalDataCachePath"] = str(work / "DDC")
        self.env["UE-SharedDataCachePath"] = "None"
        self.env["UE-ZenSharedDataCacheHost"] = "None"

    def prepare_game_engine(self):
        if not self.with_mod or not (self.engine / "Build/InstalledBuild.txt").is_file():
            return
        if self.modular_runtime is None and (self.engine / "Binaries/Win64/UnrealGame.modules").is_file():
            return
        runtime = self.modular_runtime
        if runtime is None:
            releases = REPO / "Dist/NativeTestMod"
            runtime = next(iter(sorted(releases.glob("*/Windows/Engine"))), None)
        if runtime and (runtime / "Engine").is_dir():
            runtime = runtime / "Engine"
        if not runtime or not (runtime / "Binaries/Win64/UnrealGame.modules").is_file():
            raise RuntimeError("Win64 mods require a modular Game engine. Supply a source build with --engine "
                               "or matching modular engine binaries with --modular-runtime.")
        binaries = runtime / "Binaries/Win64"
        version = read_json(binaries / "UnrealGame.version")
        editor_version = read_json(self.engine / "Build/Build.version")
        identity = ("MajorVersion", "MinorVersion", "PatchVersion", "Changelist", "CompatibleChangelist", "BranchName")
        if any(version[key] != editor_version[key] for key in identity):
            raise RuntimeError("The modular Game runtime must match the installed UE build")
        manifest = read_json(binaries / "UnrealGame.modules")
        if version["BuildId"] != manifest["BuildId"]:
            raise RuntimeError("The modular runtime version and module manifest disagree")
        self.staged_runtime = runtime
        self.game_engine = self.short_work / "E/Engine"
        print("[benchmark] stage modular Game engine", flush=True)
        self.stage_engine_files(runtime)
        self.make_import_libraries()

    def stage_engine_files(self, runtime):
        engine = self.game_engine
        engine.mkdir(parents=True)
        # Keep build products and the installed engine's files on separate paths.
        ignored = {"Binaries": "ThirdParty", "Plugins": "Marketplace"}
        for name in ("Binaries", "Build", "Config", "Plugins", "Source"):
            omit = ("Intermediate", "Saved", "*.pdb", "*.obj")
            if name in ignored:
                omit += (ignored[name],)
            shutil.copytree(self.engine / name, engine / name, copy_function=link_or_copy,
                            ignore=shutil.ignore_patterns(*omit))
        for metadata in ("Intermediate/Build/BuildRules", "Intermediate/ScriptModules"):
            shutil.copytree(self.engine / metadata, engine / metadata)
        self.copy_game_build_inputs()
        for name in ("Binaries/ThirdParty", "Content", "Shaders", "DerivedDataCache", "Extras", "Platforms", "Programs"):
            target = self.engine / name
            if target.is_dir():
                self.run(f"link-engine-{Path(name).name}", ["cmd.exe", "/d", "/c", "mklink", "/J", engine / name, target])
        for path in runtime.rglob("UnrealGame*"):
            if not path.is_file() or path.suffix.lower() not in {".dll", ".exe", ".modules", ".version"}:
                continue
            destination = engine / path.relative_to(runtime)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.unlink(missing_ok=True)
            link_or_copy(path, destination)
        build_version = engine / "Build/Build.version"
        build_version.unlink()
        write_json(build_version, read_json(runtime / "Binaries/Win64/UnrealGame.version"))

    def copy_game_build_inputs(self):
        generated = Path("Intermediate/Build/Win64/UnrealGame/Inc")
        locations = [self.engine / generated, *self.engine.glob(f"Plugins/**/{generated.as_posix()}")]
        for location in locations:
            if location.is_dir():
                shutil.copytree(location, self.game_engine / location.relative_to(self.engine),
                                copy_function=link_or_copy, dirs_exist_ok=True)
        debug_views = "Intermediate/Build/Win64/x64/UnrealGame/Development/*/*.natvis"
        for source in [*self.engine.glob(debug_views), *self.engine.glob(f"Plugins/**/{debug_views}")]:
            destination = self.game_engine / source.relative_to(self.engine)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    def make_import_libraries(self):
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        output = subprocess.check_output([vswhere, "-latest", "-products", "*", "-requires",
                                          "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                          "-find", "VC/Tools/MSVC/*/bin/Hostx64/x64/lib.exe"],
                                         text=True, creationflags=NO_WINDOW)
        librarian = next((Path(line) for line in output.splitlines() if Path(line).is_file()), None)
        if not librarian:
            raise RuntimeError("MSVC lib.exe is required for the modular runtime import libraries")
        engine = self.game_engine
        manifests = [engine / "Binaries/Win64/UnrealGame.modules", *engine.glob("Plugins/**/UnrealGame.modules")]
        module_roots = {}
        for manifest in manifests:
            root = manifest.parents[2]
            intermediate = root / "Intermediate/Build/Win64/x64/UnrealGame/Development"
            for module, filename in read_json(manifest)["Modules"].items():
                module_roots[module] = manifest.parent / filename, intermediate / module

        def make_library(entry):
            dll, output_dir = entry
            definition = output_dir / (dll.stem + ".def")
            output_dir.mkdir(parents=True, exist_ok=True)
            symbols = "\n".join(f"  {name}{' DATA' if is_data else ''}" for name, is_data in pe_exports(dll))
            definition.write_text(f"LIBRARY {dll.name}\nEXPORTS\n{symbols}\n", encoding="ascii")
            status = subprocess.run([librarian, "/nologo", "/machine:x64", f"/def:{definition}",
                                     f"/out:{definition.with_suffix('.lib')}"], capture_output=True, text=True,
                                    creationflags=NO_WINDOW)
            if status.returncode:
                raise RuntimeError(f"Could not generate {definition.with_suffix('.lib')}\n{status.stdout}\n{status.stderr}")

        with ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(make_library, module_roots.values()))

    def run(self, stage, command, timeout=3600):
        print(f"[benchmark] {stage}", flush=True)
        log = self.work / f"{stage}.log"
        with log.open("wb") as output:
            proc = subprocess.Popen(
                [str(arg) for arg in command], cwd=self.work, env=self.env,
                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                creationflags=(subprocess.CREATE_NEW_PROCESS_GROUP | NO_WINDOW) if os.name == "nt" else 0,
                start_new_session=os.name != "nt",
            )
            try:
                status = proc.wait(timeout=timeout)
            except BaseException:
                if proc.poll() is None:
                    if os.name == "nt":
                        subprocess.run(
                            ["taskkill", "/T", "/F", "/PID", str(proc.pid)], check=False,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=NO_WINDOW,
                        )
                        if proc.poll() is None:
                            proc.kill()
                    else:
                        os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
                raise
        if status:
            lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
            errors = [line for line in lines if re.search(r"\berror[: ]|fatal|exception|conflicting instance",
                                                         line, re.IGNORECASE)
                      and "[Callstack]" not in line]
            detail = errors[:45] or lines[-30:]
            raise RuntimeError(f"{stage} exited with {status}\n" + "\n".join(detail))
        return log

    def build(self, stage, project, target, *flags):
        engine = self.game_engine if target == GAME else self.engine
        ubt = engine / self.ubt.relative_to(self.engine)
        self.run(
            stage, [self.dotnet, ubt, target, self.platform, "Development",
                    f"-Project={project}", "-NoHotReload", "-NoHotReloadFromIDE", "-WaitMutex", "-NoUBA",
                    f"-Log={self.work / (stage + '-ubt.log')}", *flags],
        )

    def command(self, stage, project, *flags):
        return self.run(stage, [self.commandlet, project, *flags, "-unattended", "-nop4", "-NullRHI",
                                "-NoSound", "-NoSplash", "-DisablePython", "-stdout", "-FullStdOutLogOutput",
                                *TRACE_FLAGS,
                                "-ddc=InstalledNoZenLocalFallback",
                                f"-abslog={self.work / (stage + '-ue.log')}"])

    def prepare_project(self):
        shutil.copytree(PROJECT / "Source", self.host.parent / "Source")
        shutil.copytree(PROJECT / "Config", self.host.parent / "Config")
        shutil.copy2(PROJECT / self.host.name, self.host)
        self.isolate_build(self.host)
        characters = REPO / "UnrealModFramework/Content/Characters/Mannequins"
        shutil.copytree(characters, self.host.parent / "Content/Characters/Mannequins")
        self.copy_mod_runtime(self.host)

    @staticmethod
    def copy_mod_runtime(project):
        plugin = project.parent / "Plugins/ModRuntime"
        plugin.mkdir(parents=True)
        shutil.copy2(FRAMEWORK / "ModRuntime.uplugin", plugin)
        shutil.copytree(
            FRAMEWORK / "Source", plugin / "Source",
            ignore=shutil.ignore_patterns("ModRuntimeUht", "obj", "bin", "*.props.user", "*.csproj.props"),
        )

    def prepare_mod(self):
        author = self.work / "Author" / f"{MOD}.uproject"
        shutil.copytree(PROJECT / "Mod", author.parent)
        self.isolate_build(author)
        shared = PROJECT / "Source/Shared"
        shutil.copytree(shared, author.parent / "Source/Shared")
        for name in ("BenchmarkCalls.h", "BenchmarkNative.inl", "BenchmarkBlueprintSample.h"):
            shutil.copy2(shared / name, author.parent / "Source/BenchmarkMod/Private")

        self.copy_mod_runtime(author)
        development = author.parent / "Plugins/ModDevelopmentSDKEditor"
        shutil.copytree(DEVELOPMENT_EDITOR / "Source", development / "Source")
        write_json(development / "ModDevelopmentSDKEditor.uplugin", {
            "FileVersion": 3, "Version": 1, "CanContainContent": False,
            "Modules": [{"Name": "DevelopmentEditor", "Type": "Editor", "LoadingPhase": "Default"}],
        })
        self.build("mod-editor", author, MOD + "Editor")
        return author

    @staticmethod
    def isolate_build(project):
        config = project.parent / "Saved/UnrealBuildTool/BuildConfiguration.xml"
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text(
            '<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">\n'
            '  <UEBuildConfiguration><bAllowHotReloadFromIDE>false</bAllowHotReloadFromIDE></UEBuildConfiguration>\n'
            '</Configuration>\n', encoding="utf-8")

    def stage_game(self):
        release = self.work / "Releases"
        stage = self.work / "Stage"
        uat = self.game_engine / self.uat.relative_to(self.engine)
        self.run("stage-game", [self.dotnet, uat, "BuildCookRun", f"-project={self.host}",
                                "-nocompile", "-nocompileeditor", "-nop4", "-unattended", "-utf8output",
                                f"-unrealexe={self.commandlet}", "-WaitForUATMutex", "-cook", "-stage", "-pak",
                                "-skipiostore", "-nodebuginfo",
                                f"-platform={self.platform}", "-clientconfig=Development",
                                "-map=/Game/BenchmarkMap+/Game/BenchmarkSublevel+/Engine/Maps/Entry",
                                f"-AdditionalCookerOptions={' '.join(TRACE_FLAGS)} -ddc=InstalledNoZenLocalFallback -DisablePython "
                                "-package=/Game/BP_NativeBenchmark+"
                                "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple+"
                                "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle+"
                                "/Engine/EngineResources/DefaultTexture+"
                                "/Engine/Animation/DefaultAnimBoneCompressionSettings+"
                                "/Engine/Animation/DefaultAnimCurveCompressionSettings+"
                                "/Niagara/DefaultAssets/Templates/Systems/RadialBurst",
                                "-createreleaseversion=1.0.0", f"-createreleaseversionroot={release}",
                                f"-stagingdirectory={stage}"])
        registry = release / "1.0.0" / self.cook_platform / "AssetRegistry.bin"
        if not registry.is_file():
            raise RuntimeError(f"Base release cook did not write {registry}")
        cooked = stage / self.cook_platform
        if self.staged_runtime:
            # UAT uses the installed engine's dependency list. Include the modular build's binary dependencies too.
            binary_paths = [*self.staged_runtime.glob("Binaries/Win64/*.dll"),
                            *self.staged_runtime.glob("Binaries/ThirdParty/**/*")]
            for path in binary_paths:
                if path.is_file():
                    destination = cooked / "Engine" / path.relative_to(self.staged_runtime)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
        return cooked, release, hashlib.sha256(registry.read_bytes()).hexdigest()

    def package_mod(self, author, release):
        mod_file = self.work / f"{MOD}.mod"
        settings = {
            "format": "UMF.ModPackageBuildRequest", "schemaVersion": 2, "modVersion": "1.0.0",
            "gameId": GAME, "baseReleaseVersion": "1.0.0", "baseReleaseDirectory": str(release),
            "targetPlatform": "Win64", "useIoStore": False, "includeEngineContent": False,
            "hostTargetReceipt": str(self.host.parent / f"Binaries/Win64/{GAME}.target"),
            "hostEngineDirectory": str(self.game_engine), "outputFile": str(mod_file), "timeoutMinutes": 55,
        }
        package_dirs = (self.short_work / f"p{index:x}" for index in range(256))
        for attempt in range(BUILD_ATTEMPTS):
            directory = next(path for path in package_dirs if not path.exists())
            request = directory / "Request.json"
            write_json(request, settings)
            try:
                self.command("package-mod", author, "-run=ModPackage", f"-Request={request}")
                break
            except RuntimeError as exc:
                if attempt == BUILD_ATTEMPTS - 1 or "conflicting instance" not in str(exc).lower():
                    raise
                time.sleep(15)
        if not mod_file.is_file():
            raise RuntimeError("ModPackage completed without producing the benchmark archive")
        return mod_file

    def measure(self):
        self.prepare_project()
        with ThreadPoolExecutor(max_workers=2) as pool:
            game_engine = pool.submit(self.prepare_game_engine)
            self.build("host-editor", self.host, EDITOR_TARGET)
            self.command("host-assets", self.host, "-run=/Script/PerformanceBenchmarkEditor.BenchmarkAssetsCommandlet")
            game_engine.result()
        self.build("host-game", self.host, GAME)
        stage, release, registry = self.stage_game()
        mod_arguments = []
        if self.with_mod:
            author = self.prepare_mod()
            self.command("mod-assets", author, "-run=/Script/BenchmarkModEditor.BenchmarkModAssetsCommandlet")
            mod_file = self.package_mod(author, release)
            mod_arguments = [f"-BenchmarkModFile={mod_file}", f"-BenchmarkRegistry={registry}"]
        return self.run_automation(stage, mod_arguments)

    def run_automation(self, stage, mod_arguments):
        project_arguments = []
        if self.platform == "Win64" and self.with_mod:
            # The shared modular executable expects the project name before its map and flags.
            executable = stage / "Engine/Binaries/Win64/UnrealGame.exe"
            project_arguments = [GAME]
        else:
            suffix = {"Win64": ".exe", "Linux": "", "Mac": ".app/Contents/MacOS/" + GAME}[self.platform]
            candidates = [stage / (GAME + suffix), stage / GAME / "Binaries" / self.platform / (GAME + suffix)]
            if self.platform == "Linux":
                candidates.insert(0, stage / (GAME + ".sh"))
            executable = next((path for path in candidates if path.is_file()), candidates[0])
        if not executable.is_file():
            raise RuntimeError(f"Missing standalone benchmark executable in {stage}")
        report = self.work / "report.md"
        report.unlink(missing_ok=True)
        log = self.run("measure", [
            executable, *project_arguments, "/Game/BenchmarkMap", f"-BenchmarkReport={report}", *mod_arguments,
            f"-ExecCmds=Automation RunTests {TEST};SoftQuit", "-unattended", "-NoCrashReports", "-NoSplash",
            "-RenderOffScreen", "-NoVSync", "-Windowed", "-ResX=1280", "-ResY=720",
            *TRACE_FLAGS, "-stdout", "-FullStdOutLogOutput",
            f"-UserDir={self.work / 'GameUser'}",
            f"-abslog={self.work / 'measure-ue.log'}",
        ])
        ue_log = self.work / "measure-ue.log"
        lines = (ue_log if ue_log.is_file() else log).read_text(encoding="utf-8", errors="replace").splitlines()
        if not report.is_file() or not any(f"Result={{Success}} Name={{CallBenchmark}} Path={{{TEST}}}" in line
                                         for line in lines):
            failures = [line for line in lines if "Error:" in line or "Test Completed." in line]
            raise RuntimeError("Automation did not pass and write a complete report\n" + "\n".join(failures[-20:] or lines[-30:]))
        result = report.read_bytes()
        lines = result.decode("utf-8-sig").splitlines()
        rows = [line.split("|")[1:-1] for line in lines if re.match(r"^\| \d{3} \|", line)]
        if len(rows) != 200 or any(len(row) != 6 or int(row[0]) != index
                                   for index, row in enumerate(rows, start=1)):
            raise RuntimeError("Benchmark report does not contain the 200 API rows")
        timings = [[cell.strip() for cell in row[2:]] for row in rows]
        if any(cell != "未测试" and not re.fullmatch(r"[0-9]+\.[0-9]{3}", cell)
               for row in timings for cell in row):
            raise RuntimeError("Benchmark report contains an invalid timing cell")
        if self.with_mod:
            for index, row in enumerate(timings, start=1):
                for native, mod in zip(row[:2], row[2:]):
                    if (native == "未测试") != (mod == "未测试"):
                        raise RuntimeError(f"Host and Mod coverage differs for API {index:03d}")
        return result


def main():
    parser = argparse.ArgumentParser(description="Measure UE calls in the standalone PerformanceBenchmark project")
    parser.add_argument("--engine", required=True, type=Path, help="UE 5.6 installation or Engine directory")
    parser.add_argument("--report", type=Path, default=PROJECT / "ue-call-benchmark.md")
    parser.add_argument("--modular-runtime", type=Path, help="Matching Win64 Development modular Game engine directory")
    args = parser.parse_args()
    base = Path(tempfile.gettempdir()).resolve()
    if base == Path(base.anchor):
        base = REPO / ".tools/tmp"
        base.mkdir(parents=True, exist_ok=True)
    try:
        work = Path(tempfile.mkdtemp(prefix="umfb-", dir=base)).resolve()
        if work.parent != base or not work.name.startswith("umfb-"):
            raise RuntimeError(f"Unexpected temporary build directory: {work}")
        try:
            with build_path(work) as short_work:
                runtime = args.modular_runtime.resolve() if args.modular_runtime else None
                result = Unreal(args.engine, work, short_work, runtime).measure()
        finally:
            print("[benchmark] remove temporary build", flush=True)
            remove_build(work)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes(result)
        print(f"[benchmark] {args.report.resolve()}")
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"[benchmark] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
