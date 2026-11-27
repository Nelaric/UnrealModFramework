<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](BuildAndReview.zh-CN.md)

# Build Scripts, Tooling, and Review

## Copyright notices

Every project-authored file must carry a Nelaric copyright notice whenever its format permits comments. Place the notice on the very first logical line—before any heading, code, configuration key, or prose (a UTF-8 BOM does not count as a line). Use `Copyright (c) <year> Nelaric Contributors`, where `<year>` is the year the file was first published; do not bump the year simply because you edited the file.

Match each format's native comment syntax:

| File format | First line |
| --- | --- |
| C++, C#, and JavaScript source | `// Copyright (c) 2026 Nelaric Contributors` |
| Markdown and HTML | `<!-- Copyright (c) 2026 Nelaric Contributors -->` |
| Python, shell, YAML, and `#`-comment configs | `# Copyright (c) 2026 Nelaric Contributors` |

When an executable script starts with a `#!` shebang, keep the shebang on line 1 and put the copyright notice immediately on line 2. Likewise, if a file format requires another first-line directive, place the notice at the earliest legal comment line. Never inject comments into strict JSON or other formats that forbid them, and never prepend a comment to `LICENSE` or alter its license text. Binary assets, generated outputs, and unmodified third-party files keep their own attribution and are exempt from in-file Nelaric headers. For project-authored files whose format cannot hold comments, record attribution in `LICENSE` or [NOTICE.md](https://github.com/Nelaric/UnrealModFramework/blob/main/NOTICE.md) instead of breaking the file syntax.

Enforce this rule on all new project-authored files and whenever touching existing ones. Reviewers check for either the header or a valid format exception. Always preserve existing third-party notices and never claim Nelaric copyright over external code or assets.

## Unreal build scripts

- **Format C# rules with CSharpier:** Run CSharpier on every `.Build.cs` and `.Target.cs` file—it is just as mandatory for build scripts as `clang-format` is for C++.
- **Keep module dependencies tight:** Declare only the dependencies a module actually uses, splitting `PublicDependencyModuleNames` and `PrivateDependencyModuleNames` according to what its `Public/` headers expose. Never pull third-party vendor SDKs into Runtime modules.
- **Verify against UE 5.6+:** Build affected Game and Editor targets with Unreal Engine 5.6 before requesting review (and Server targets once introduced). Over time, CI will track both the minimum and latest supported engine versions. Today, GitHub Actions validates naming, formatting, text conventions, Doxygen API docs, cross-platform Hook fixtures, and MST source analysis, while CircleCI compiles UE Linux Game and Editor targets, runs the Editor Hook dispatch test, and builds the standalone authoring plugin. When building targets that use automatic C++ Hooks, invoke UBT through MBT so rewritten Hook copies are compiled and linked binaries are verified (see [Linux build scope](https://github.com/Nelaric/UnrealModFramework/blob/main/Docs/DevelopmentWorkflow.md#linux-build-scope)).

## Automation tests

Every independent framework capability must ship with automated tests that verify its observable contract—including meaningful error paths and boundary conditions—and those tests must be updated whenever behavior changes. Register framework-owned Unreal automation tests under the `Nelaric.*` namespace so CI can select them reliably.

Prefer a small number of cohesive, scenario-oriented tests per capability rather than creating a separate test for every helper method, branch, or trivial permutation. Test count and raw line coverage are not goals; reviewers look for clear coverage of real behavior and key failure modes without redundant noise.

## Local checks

Run the applicable checks below from the repository root (next to `UnrealModFramework/`), using the tool versions pinned in `.github/workflows/quality.yml` and `.config/dotnet-tools.json`:

1. `python Scripts/check_text.py` — verifies UTF-8 BOM presence and CRLF/LF line endings.
2. `python Scripts/check_format.py` — checks C++ (`clang-format`) and Unreal build-script (`CSharpier`) formatting after restoring the local `.NET` tool manifest.
3. `python Scripts/check_public_docs.py` — confirms every public header starts with `@file`, checks that consumer methods in each class's first `public:` section carry Doxygen comments, and rejects any `protected:`/`private:` specifier between the first and second `public:` sections.
4. `python Scripts/check_doxygen_style.py` — enforces the 25- and 75-character comment line limits, single-line versus multi-line comment forms, `@` tags at the start of multi-line paragraphs, and aligned enum value comments.
5. `python Scripts/run_doxygen.py` — builds the Doxygen site into `.tools/api-docs/html/index.html` and fails on any warning (requires the pinned Doxygen release).

Run every check relevant to your change before requesting review, and report Game and Editor build results in your PR whenever those targets are affected. Pull requests must pass all automated naming, text, formatting, and API documentation checks. Changes to CI workflows, build scripts, `.uproject`/`.uplugin` descriptors, or native C ABI contracts require maintainer review. Neither `clang-tidy` nor a numeric coverage threshold is enforced at present.

## Commit, PR, and branch naming

Use one of the following prefixes for all commits, PR titles, and working branches:

| Prefix | When to use |
| --- | --- |
| `feat` | Introduce a new capability. |
| `fix` | Fix a bug. |
| `docs` | Update documentation only. |
| `style` | Reformat code with no behavior change. |
| `refactor` | Restructure code with no behavior change. |
| `perf` | Improve runtime or build performance. |
| `test` | Add or update tests. |
| `build` | Change build scripts, toolchains, or dependencies. |
| `ci` | Change CI workflows or automation scripts. |
| `chore` | Routine maintenance not covered above. |
| `revert` | Revert an earlier commit. |

- **Commit subjects and PR titles:** Format as `<prefix>: <short English summary>` or `<prefix>(<scope>): <short English summary>`, using a lowercase scope and starting the summary with a lowercase imperative verb—for example, `feat(session): add reservation support` or `docs: clarify provider boundaries`. Squash-merge titles follow the exact same rule. CI checks the prefix, syntax, and leading lowercase letter across the PR title, source branch, and every commit in the PR; reviewers check that the summary starts with a verb and describes the change accurately.
- **Branch-sync merge exception:** GitHub's "Update branch" merge commits are permitted when—and only when—the commit has two parents in the GitHub API and uses the exact subject `Merge branch 'main' into <current PR source branch>`. Ordinary single-parent commits with that message, merges from other branches, or merges into another target branch are rejected.
- **Working branches:** Format as `<prefix>/<lowercase-kebab-case-description>` (for example, `fix/admission-timeout`), using a prefix from the table above. Only the default branch `main` is exempt; adding a new prefix requires updating this standard first.

## Text encoding and newline rules

- **UTF-8 with BOM + CRLF:** Human-written Markdown (`.md`), `LICENSE`, C++ sources and headers, and Unreal C# build scripts (`.cs`).
- **UTF-8 without BOM + LF:** `.gitattributes`, `.editorconfig`, `.clang-format`, `.csharpierrc`, `.gitignore`, `.json`, `.modexport`, `.ini`, `.uproject`, `.uplugin`, `.yml`, `.yaml`, `.html`, `Doxyfile`, `CNAME`, and `.py` scripts.

`Scripts/check_text.py` enforces both categories across tracked and untracked workspace files.

## Code review checklist

Reviewers evaluate correctness, copyright headers, module boundaries, accuracy of public API contracts, cancellation and error semantics, lifetime/ownership safety, performance, security, and behavior-focused automated tests. CI handles mechanical Doxygen formatting, while human reviewers verify that comments match the implementation and tests cover real risks without bloat.

If a PR requests an exception to a project rule, verify that it documents the rule, rationale, scope, and alternatives, and ensure a maintainer approves it—remembering that PR approval cannot override Epic's coding standard or settle a conflict between the two standards (open an issue instead).

Finally, verify on every C++ change that project-authored `UCLASS` declarations include `MinimalAPI`, omit class-level `*_API` macros, and export only individual cross-module methods as required by [C++ Style and Headers](Cpp.md).
