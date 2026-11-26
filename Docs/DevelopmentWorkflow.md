<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](DevelopmentWorkflow.zh-CN.md)

# Development and CI Workflow

This page explains contribution checks, desktop builds, and documentation publishing. Start with the [contribution guide](../CONTRIBUTING.md) when preparing a change.

## From a change to `main`

1. Make a focused change on a branch or fork, and run the relevant [local checks](CodingStandards/BuildAndReview.md#local-checks).
2. Open a pull request to `main` using the [PR template](../.github/pull_request_template.md), and link any related issue when applicable.
3. Resolve any check failures and complete maintainer review before merging. Maintainers should configure branch protection to require the applicable checks.
4. Once [API Pages is enabled](Hosting/README.md), merging to `main` publishes the generated API documentation.

## Checks on a pull request

The [Quality workflow](../.github/workflows/quality.yml) checks naming, text, formatting and API documentation. Linux fixtures also exercise automatic Hook compilation and linking for COFF, ELF and Mach-O and cross-file MST analysis with GCC and Clang.

## Desktop build scope

[Desktop CI](../.github/workflows/desktop.yml) runs on pull requests to `main`, pushes to `main` and manual dispatch. Each of the six GitHub-hosted machines runs two independent UE 5.6.1 Development jobs.

| System | x64 runner | ARM64 runner |
| --- | --- | --- |
| Windows | `windows-2022` | `windows-11-arm` |
| macOS | `macos-15-intel` | `macos-15` |
| Linux | `ubuntu-24.04` | `ubuntu-24.04-arm` |

Each job builds the four native Mod tools, inspects their executable architecture, runs the automation-report checks, and compiles its target with MBT. Game jobs cook, stage and launch the game. Editor jobs launch the editor and use MDT to build and verify the independent authoring plugin. Both targets check health changes and C++ Hook dispatch, including Pre, On and Post handlers. Editor also tests calls from a compiled Blueprint. Each successful job records the host link receipt and requires both named tests to pass in a fresh Unreal report. [CI driver](../Scripts/run_desktop_ci.py)

Linux x64 pulls Epic’s official `ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1` on the runner. If the job token lacks access, configure Actions secrets `GHCR_USERNAME` and `GHCR_TOKEN` with an Epic-linked account and a read-only package token. The installer resolves the image digest and streams the engine files to disk. [Epic container quick start](https://dev.epicgames.com/documentation/en-us/unreal-engine/quick-start-guide-for-using-container-images-in-unreal-engine)

For the five other hosts, configure the Actions secret `UE_561_<KEY>_URL` with a private HTTPS download URL and the repository variable `UE_561_<KEY>_SHA256` with the archive digest. The keys are `WINDOWS_X64`, `WINDOWS_ARM64`, `MACOS_X64`, `MACOS_ARM64` and `LINUX_ARM64`. The tar archive must contain `Engine/` at its root, an Editor for cooking (native for Editor jobs), UBT, UAT, bundled .NET, toolchain and Development Game libraries. Allow disk space for the compressed archive, extracted engine, intermediate files and staged game. Actions runner images do not include UE. Windows ARM64 Editor requires a custom engine port. An x64 Editor under emulation cannot validate that target. [GitHub runner specifications](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) · [Epic Windows ARM64 support](https://dev.epicgames.com/documentation/unreal-engine/windows-arm64-support-in-unreal-engine-5-8)

GitHub withholds repository secrets from fork pull requests. Public quality and native-tool checks can run there. For engine validation, a maintainer reviews the submitted code and runs that revision on a branch in the source repository. Missing engine inputs fail the affected jobs. The aggregate check `Desktop build and automation` requires all twelve jobs to pass.

When the matrix passes, replace the three legacy required checks (`ci/linux-game-build`, `ci/linux-editor-build`, `ci/fork-pr-linux-build`) in the `main` ruleset with `Desktop build and automation`. Retain the quality checks. Disable the legacy project push trigger in CircleCI and remove the obsolete Actions webhook secret and enable variable.

## Run the call benchmark

From the repository root, run the [benchmark driver](../Test/Performance/run_benchmark.py) with a UE 5.6 installation and an explicit report path so the checked-in measurement is preserved.

```powershell
python Test/Performance/run_benchmark.py --engine 'C:\UE_5.6' --report '.tools/ue-call-benchmark.local.md'
```

The standalone benchmark runs 200 cases with 1,000 samples per available path. Win64 x64 measures host C++, host Blueprint, and the C++ and Blueprint paths inside a mounted `BenchmarkMod`. Use an engine source build capable of producing a modular Development Game, or pass the matching modular runtime via `--modular-runtime`. Other supported platforms run only the host paths. Unavailable paths are marked as `未测试` in the report. The summary reports the mean of available case means for each column and reflects the tested machine and fixtures; see the [checked-in results](../Test/Performance/ue-call-benchmark.md).

## If a check fails

Open the failed GitHub Actions job and inspect its first failing step. After Unreal starts, the job uploads the available host link receipt, automation log and report index. Include the run link and the failing target when requesting assistance.
