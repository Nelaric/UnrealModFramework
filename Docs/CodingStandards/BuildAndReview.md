<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](BuildAndReview.zh-CN.md)

# Build Scripts, Tooling, and Review

## Copyright notices

Every project-authored file must carry a Nelaric copyright notice whenever its format permits one. In any text file that supports comments, the notice must be the first logical line, preceding any heading, code, configuration entry, or prose. A UTF-8 BOM does not count as a line. Use `Copyright (c) <year> Nelaric Contributors`, where `<year>` is the file's first publication year; do not update it merely because the file changes.

Use the format's native comment syntax:

| File format | First line |
| --- | --- |
| C++, C#, and JavaScript source | `// Copyright (c) 2026 Nelaric Contributors` |
| Markdown and HTML | `<!-- Copyright (c) 2026 Nelaric Contributors -->` |
| Python, shell, YAML, and hash-comment configuration | `# Copyright (c) 2026 Nelaric Contributors` |

If an executable script needs a shebang, keep `#!...` on line one and place the copyright notice immediately on line two. If a format requires another first-line directive, place the notice at the first legal comment position. Do not add comments to strict JSON or other formats that forbid them. Do not prepend a comment to `LICENSE` or alter its license text; its existing copyright line is authoritative. Binary assets, generated files, and unmodified third-party files retain their own applicable attribution and are exempt from an in-file Nelaric header. For project-authored files that cannot carry an in-file notice, record ownership in the repository license or a nearby notice file rather than breaking the file format.

Apply this rule to every new project-authored file and to existing project-authored files when modifying them. Reviewers must verify either the header or a documented format exception. Keep existing third-party notices intact; do not claim copyright over code or assets that Nelaric does not own.

## Unreal build scripts

- Apply CSharpier to `.Build.cs` and `.Target.cs` files. It is as mandatory as clang-format is for C++.
- Keep module dependencies minimal and classify them as public or private according to the headers that require them. Do not add vendor SDK dependencies to Runtime.
- Target UE 5.6 and later. Before requesting review, build the development host Game and Editor targets with UE 5.6; build a Server target when one is introduced. CI should eventually cover the minimum and latest supported engine releases. GitHub Actions checks naming, formatting, text conventions, API documentation, automatic Hook target fixtures, and MST source analysis. CircleCI builds the UE Linux Game and Editor targets, runs the Editor Hook dispatch automation test, and builds the independent authoring plugin; see [Build coverage](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/DevelopmentWorkflow.md#linux-build-scope) for details.

## Automation tests

Each independent framework capability must have automated tests that exercise its observable behavior. Add or update those tests alongside the capability or any behavior change, including meaningful failure and boundary cases where they affect the contract. Name framework-authored Unreal automation tests under the `Nelaric.*` hierarchy so CI can select them. Group related scenarios into a small number of focused tests per capability. Do not create a separate test for every method, branch, or minor variation, or accumulate large numbers of overlapping tests in a single module. Test count and line coverage are not targets; reviewers judge whether the observable behavior and important risks are adequately covered.

## Local checks

Run these commands from the repository root (alongside the `UnrealModFramework/` project directory), using the tool versions pinned in `.github/workflows/quality.yml` and `.config/dotnet-tools.json`:

1. `python Scripts/check_text.py` checks text encoding and line endings.
2. `python Scripts/check_format.py` checks C++ and UE build-script formatting. Install the pinned clang-format and restore the local .NET tool manifest first.
3. `python Scripts/check_public_docs.py` checks that every public header has an `@file` comment and that methods in each class's first `public:` section have Doxygen comments. It rejects any intervening access specifier between the first and second `public:` sections; the second section is not checked for method comments.
4. `python Scripts/check_doxygen_style.py` checks the 25- and 75-character line limits, single- and multi-line forms, a tag at the start of every multi-line paragraph, and consistent enum comment style and alignment.
5. `python Scripts/run_doxygen.py` builds the API documentation site and fails on Doxygen warnings. Install the pinned Doxygen release first.

Before requesting review, run the local checks relevant to the change and report the Game and Editor build results when those targets are affected. Pull requests must pass the available naming, text, format, and API documentation checks. Changes to CI, build scripts, plugin descriptors, or native ABI contracts require maintainer review. No clang-tidy or numeric code-coverage threshold is required at present.

## Commit and branch names

Use only these prefixes for new commits and working branches:

| Prefix | Purpose |
| --- | --- |
| `feat` | Add a capability. |
| `fix` | Correct a defect. |
| `docs` | Change documentation only. |
| `style` | Change formatting without changing behavior. |
| `refactor` | Restructure code without changing behavior. |
| `perf` | Improve performance. |
| `test` | Add or change tests. |
| `build` | Change build configuration or dependencies. |
| `ci` | Change automation and CI workflows. |
| `chore` | Perform repository maintenance not covered above. |
| `revert` | Revert an earlier change. |

Commit subjects must use `<prefix>: <short English summary>` or `<prefix>(<scope>): <short English summary>`. Use a lowercase scope when present and start the summary with a verb. Squash-merge commit titles must follow the same rule. Examples: `feat(session): add reservation support` and `docs: clarify provider boundaries`.

PR titles must follow the same format as commit subjects. CI validates the PR title, source branch, and every commit subject in the PR for an allowed prefix, valid structure, and an English summary starting with a lowercase letter; reviewers verify that the first word is a verb and that the summary accurately describes the change.

GitHub branch synchronization commits are a limited exception: a commit with exactly two parents may use the exact subject `Merge branch 'main' into <current PR source branch>`. The naming checker reads parent counts from the GitHub API and matches the destination branch against the current PR source branch. Missing parent metadata, ordinary commits with the same text, merges from other branches, and merges into a different destination do not qualify. PR titles, branch names, ordinary commits, and squash-merge titles must still follow the standard format.

Working branches must use `<prefix>/<lowercase-kebab-case-description>`, for example `fix/admission-timeout`. The prefix must come from the table above; `main` is the reserved default-branch exception. Do not introduce another prefix without first updating this standard.

## Text-file exceptions

Human-written Markdown, `LICENSE`, C++ source and headers, and UE C# build scripts use UTF-8 with BOM and CRLF. `.gitattributes`, `.editorconfig`, `.clang-format`, `.csharpierrc`, `.gitignore`, `.json`, `.ini`, `.uproject`, `.uplugin`, `.yml`, `.yaml`, `Doxyfile`, and executable `.py` scripts use UTF-8 without BOM and LF for tool compatibility. The text checker enforces these categories.

## Review

Review correctness, copyright notices, module boundaries, the accuracy of public API documentation, cancellation and failure behavior, ownership, performance, security, and the automated tests for each independent framework capability. CI verifies mechanical Doxygen style; reviewers judge whether comments describe the actual contract and whether tests cover meaningful behavior without unnecessary duplication. A PR seeking an exception to a project guideline must state the rule, reason, affected code, and alternatives, and receive maintainer approval; an exception cannot override an Epic requirement or resolve a conflict between the standards. Report such conflicts in an issue. Current CI checks serve as the automated gate, but passing CI does not replace reviewing the required tests.

Reviewers must verify that every project-authored `UCLASS` explicitly uses `MinimalAPI`, omits any class-level `*_API` macro, and exports only the individual methods needed across modules, as required by [UCLASS exports](Cpp.md#uclass-exports).
