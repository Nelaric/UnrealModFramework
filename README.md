<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](README.zh-CN.md)

# UnrealModFramework

[![Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fapi.github.com%2Frepos%2FNelaric%2FUnrealModFramework%2Fproperties%2Fvalues&query=%24%5B0%5D.value&label=version&color=blue)](https://github.com/Nelaric/UnrealModFramework/custom-properties)
[![UE 5.6+](https://img.shields.io/badge/UE-5.6%2B-313131?logo=unrealengine&logoColor=white)](UnrealModFramework/UnrealModFrameworkDev.uproject)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](CONTRIBUTING.md)

UnrealModFramework is a UE 5.6 development project for a portable game-side mod runtime and an independently built mod authoring editor. The repository currently contains the project and plugin module skeletons. Package loading, content resolution, native mod APIs, and editor workflows remain to be implemented and validated.

Paths below are relative to the repository root. The UE project lives in `UnrealModFramework/`; repository configuration, documentation, and quality scripts live alongside it.

| Path | Purpose |
| --- | --- |
| `UnrealModFramework/UnrealModFrameworkDev.uproject` | Development host project |
| `UnrealModFramework/Plugins/ModRuntime/` | Game-side source plugin; `Source/Runtime/` and `Source/Editor/` group its modules by target |
| `UnrealModFramework/SourcePlugins/ModDevelopmentEditor/` | Independently built authoring plugin |
| `Docs/CodingStandards/` | Project coding and contribution standards |
| `.circleci/config.yml` | UE 5.6.1 Linux build configuration |
| `Scripts/` | Local quality checks |

`Binaries`, `Intermediate`, `Saved`, and `Dist` are generated output and are excluded from Git. The authoring plugin builds with UAT `BuildPlugin` from its descriptor in `UnrealModFramework/SourcePlugins`; use a short temporary `-Package` path on Windows and copy the resulting plugin into a release directory after the build.

See the [coding standards](Docs/CodingStandards/README.md), [contribution guide](CONTRIBUTING.md), and [CircleCI setup](Docs/CircleCI.md) before changing code or build files.

## Contributing

Use the [contribution guide](CONTRIBUTING.md), [Issue forms](https://github.com/Nelaric/UnrealModFramework/issues/new/choose), and [PR template](.github/pull_request_template.md) to prepare a focused change. The [development workflow](Docs/DevelopmentWorkflow.md) explains local checks, trusted PR builds, and documentation publishing.

## API documentation

The [API documentation entry](Docs/API/README.md) and coding standards are built with Doxygen and published at [UnrealModFramework API](https://docs.nelaric.com/UnrealModFramework/API/). Approved updates publish automatically from `main`. See the [organization hosting guide](Docs/Hosting/README.md) for publishing configuration.

## Star History

<a href="https://www.star-history.com/?repos=Nelaric%2FUnrealModFramework&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=Nelaric/UnrealModFramework&type=date&legend=top-left" />
  </picture>
</a>

## License

Project-authored material is licensed under the [MIT License](LICENSE) unless stated otherwise. Unreal Engine and third-party material retain their own terms; see [NOTICE.md](NOTICE.md).

## Thanks

Thanks to everyone contributing to UnrealModFramework, to Epic Games for Unreal Engine, and to CircleCI for the Linux project build service.
