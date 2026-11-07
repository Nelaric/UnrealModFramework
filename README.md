<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

UnrealModFramework is a UE 5.6 development project for a portable game-side mod runtime and an independently built mod authoring editor. The repository currently contains the project and plugin module skeletons. Package loading, content resolution, native mod APIs, and editor workflows remain to be implemented and validated.

| Path | Purpose |
| --- | --- |
| `UnrealModFrameworkDev.uproject` | Development host project |
| `Plugins/ModRuntime/` | Game-side source plugin; `Source/Game/` and `Source/Editor/` group its modules by target |
| `SourcePlugins/ModDevelopmentEditor/` | Independently built authoring plugin |
| `Docs/CodingStandards/` | Project coding and contribution standards |
| `.circleci/config.yml` | UE 5.6.1 Linux build configuration |
| `Scripts/` | Local quality checks |

`Binaries`, `Intermediate`, `Saved`, and `Dist` are generated output and are excluded from Git. The authoring plugin builds with UAT `BuildPlugin` from its descriptor in `SourcePlugins`; use a short temporary `-Package` path on Windows and copy the resulting plugin into a release directory after the build.

See the [coding standards](Docs/CodingStandards/README.md), [contribution guide](CONTRIBUTING.md), and [CircleCI setup](Docs/CircleCI.md) before changing code or build files.
